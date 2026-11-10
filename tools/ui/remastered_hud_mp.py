"""The Remastered multiplayer HUD (ui-visual-design.md 14.9): the mphud.gui peer.

The document receives the stock multiplayer HUD's state and named events plus
the game's Remastered fields (idMultiplayerGame::PublishRemasteredHud: the kill
feed and chat as fields, ordinals, the lead gap, the limit, the clock as
numbers, flags, the zone, alive players, the vote tally, the followed
player and the respawn prompt). Constructions follow the design atlas's multiplayer sheet: compact
sheared HUD plates at black 0.50, never wider than their content, the ◥ marker
and the header-band plate for you.
"""
from __future__ import annotations

import math

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (AND, BOUND_TEXT, NOT, OR, S, U, HudDocument, anchor, beyond_minimal, cancel, fitted_plate_path,
                                 gameplay_root,
                                 layout_state, num, op, play, pts_path, rgb, rounded_rect, sel, stroke, svg_path, tr, transform_value,
                                 u, vec, when)
from remastered_hud_status import MP_ROSTER, full

NOTE, MAR, STR, MAR_T, STR_T = "#FFFF8D", "#6AA42B", "#FF7B04", "#AAE355", "#FF8E00"
VALUE, ERR, OK, BAND, MARKER, NAME, DARK = "#FFBE23", "#E46D56", "#B5C784", "#8B964B", "#E38900", "#E4EBD2", "#10140A"
SPEC_TEXT = "#FFFF8C"
WHITE = [1, 1, 1, 1]
# The kill feed's weapon color code (hud.mtr item colors, Appendix B.4), MP roster order.
FEED_COLOURS = ["#00D9FF", "#FFFF00", "#FF8000", "#0073FF", "#338F12", "#99CCCC", "#FF3300", "#00FF00", "#FFFFBA", "#C400FF",
                "#FFBF40"]
FACE = "lowpixel"
TEAM_GAMES = (3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16)
SLASH, PLUS, MINUS = "#str_231222", "#str_231223", "#str_231224"


# ------------------------------------------------------------------ helpers

def colour_of(cases: list, default: list) -> list:
    """A color binding's four components: the first (condition, rgba) that holds."""
    result = []
    for i in range(4):
        expr = default[i]
        for condition, value in reversed(cases):
            expr = sel(condition, value[i], expr)
        result.append(expr)
    return result


def lerp_colour(a: list, b: list, t) -> list:
    """Components moving from a to b as the expression t runs 0 to 1."""
    return [op("+", a[i], op("*", b[i] - a[i], t)) for i in range(4)]


def label(ident: str, key: str, size: float, colour: list, *, face: str = FACE, line: float | None = None, **props) -> dict:
    """A one-line text node in the flow (sizes in u)."""
    p = {**ui.typeface(face, u(size), u(line if line is not None else size * 1.25), colour), "white-space": ui.keyword("nowrap"),
         "position": ui.keyword("relative"), "display": ui.keyword("block")}
    p.update(props)
    return ui.label(ident, key, p)


def bound_label(doc: HudDocument, ident: str, key_expr, size: float, colour: list, **kw) -> dict:
    node = label(ident, BOUND_TEXT, size, colour, **kw)
    doc.bind(ident + ".text", ident, "text", key_expr)
    return node


def ellipsis(width: float) -> dict:
    """A name cut with an ellipsis at `width` u."""
    return {"max-width": ui.length(u(width)), "overflow": ui.keyword("hidden"), "text-overflow": ui.keyword("ellipsis")}


def plate_vec(ident: str, h: float, alpha: float = 0.5, colour: str = "#000000", *, display: str = "block") -> dict:
    """A sheared HUD plate filling its node (14.9): rounded 1 u, the 0.22 shear."""
    return vec(ident, [fitted_plate_path("plate", u(h), r=u(1), fill=ui.solid(rgb(colour, alpha)))], {"display": ui.keyword(display)})


def tall_plate_path(ident: str, lean: float, r: float, fill) -> dict:
    """A plate whose height follows its node (a wrapped message): the corners
    lean by a fixed `lean` dp, the shear of one line."""
    def p(fx, dx, fy, dy):
        return [{"fraction": fx, "dp": round(dx, 4)}, {"fraction": fy, "dp": round(dy, 4)}]
    s = lean
    cmds = [{"id": "c0", "op": "move", "points": [p(0, s + r, 0, 0)]},
            {"id": "c1", "op": "line", "points": [p(1, s - r, 0, 0)]},
            {"id": "c2", "op": "quadratic", "points": [p(1, s, 0, 0), p(1, s - 0.22 * r, 0, r)]},
            {"id": "c3", "op": "line", "points": [p(1, -s + 0.22 * r, 1, -r)]},
            {"id": "c4", "op": "quadratic", "points": [p(1, -s, 1, 0), p(1, -s - r, 1, 0)]},
            {"id": "c5", "op": "line", "points": [p(0, -s + r, 1, 0)]},
            {"id": "c6", "op": "quadratic", "points": [p(0, -s, 1, 0), p(0, -s + 0.22 * r, 1, -r)]},
            {"id": "c7", "op": "line", "points": [p(0, s - 0.22 * r, 0, r)]},
            {"id": "c8", "op": "quadratic", "points": [p(0, s, 0, 0), p(0, s + r, 0, 0)]},
            {"id": "c9", "op": "close"}]
    return {"id": ident, "commands": cmds, "fill": fill}


def plated_card(ident: str, place: dict, padding: dict, plate: dict, rows: list) -> dict:
    """A card whose rows decide its height, on a plate: a block holding the
    plate and the column of rows. As a direct child of the column the plate
    was laid out before the rows gave the card its height, and stayed a thin
    band across the card's top (the vote, statistics and managed-match cards)."""
    body = ui.group(ident + "-rows", {"position": ui.keyword("relative"), "display": ui.keyword("flex"),
                                      "flex-direction": ui.keyword("column"), **padding}, rows)
    return ui.group(ident, {**place, "display": ui.keyword("none")}, [plate, body])


def marker_vec(ident: str, x: float, y: float) -> dict:
    """The ◥ marker: a 5 u right triangle in #E38900, at (x, y) u in its parent."""
    return vec(ident, [pts_path("m", [(u(x), u(y)), (u(x + 5), u(y)), (u(x + 5), u(y + 5))], fill=ui.solid(rgb(MARKER)))])


def tag_vec(ident: str, colour: list) -> dict:
    """The header-band tag's plate: a cut lower-leading corner, at 0.85."""
    return vec(ident, [{"id": "t", "fill": ui.solid([*colour[:3], 0.85]), "commands": [
        {"id": "a", "op": "move", "points": [[0, 0]]},
        {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
        {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
        {"id": "d", "op": "line", "points": [[u(3), {"fraction": 1}]]},
        {"id": "e", "op": "line", "points": [[0, {"fraction": 1, "dp": -u(3)}]]},
        {"id": "f", "op": "close"}]}])


def tag_node(doc: HudDocument, ident: str, key: str, variants: list, **props) -> dict:
    """A tag in the header-band construction (14.9): a small plate in the
    given color with dark Lowpixel 7.2 u text. `variants` are (suffix, rgba,
    condition) plates, one shown at a time; paint is static."""
    plates = []
    for suffix, colour, condition in variants:
        node = tag_vec(f"{ident}-{suffix}", colour)
        if condition is not None:
            node["properties"]["display"] = ui.keyword("none")
            doc.show(node["id"], condition)
        plates.append(node)
    text = label(ident + "-text", key, 7.2, rgb(DARK), line=10, **{"letter-spacing": ui.length(0.6)})
    p = {"position": ui.keyword("relative"), "display": ui.keyword("block"), "height": ui.length(u(10)),
         "padding-left": ui.length(u(4)), "padding-right": ui.length(u(4))}
    p.update(props)
    return ui.group(ident, p, plates + [text])


def icon_box(ident: str, glyph: str, h: float, *, tint=None, glow: bool = False, **props) -> dict:
    """A glyph in a flow box h u square."""
    p = {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(h)),
         "height": ui.length(u(h))}
    p.update(props)
    return ui.group(ident, p, hud_svg.icon_nodes(glyph, ident + "-g", 0, 0, u(h), tint=tint, glow=glow))


def flex_row(ident: str, children: list, **props) -> dict:
    p = {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("row"),
         "align-items": ui.keyword("center")}
    p.update(props)
    return ui.group(ident, p, children)


def key_chip(doc: HudDocument, ident: str, key_expr, *, size: float = 6, h: float = 10) -> dict:
    """The active control (13.4): its key name in dark ProFont on a white chip
    at 0.85 with a cut lower-leading corner, `h` u tall; a key state shows the
    controller's button in its glyph family instead (glyph_chip)."""
    return glyph_chip(doc, ident, key_expr, h=h, size=size, cut=3, pad=(4, 3), gap=4)


# ------------------------------------------------------------------ prompt glyphs

# The glyph kinds a key state's "<key>.glyph" holds (UserInterfaceRetained.cpp
# PromptGlyph): the documents' contract with the engine.
GLYPH_KEYCAP, GLYPH_LETTER = 0, 1
GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_SQUARE, GLYPH_TRIANGLE = 2, 3, 4, 5
GLYPH_SOUTH, GLYPH_EAST, GLYPH_WEST, GLYPH_NORTH = 6, 7, 8, 9
GLYPH_BUMPER, GLYPH_TRIGGER = 10, 11
GLYPH_DPAD_UP, GLYPH_DPAD_DOWN, GLYPH_DPAD_LEFT, GLYPH_DPAD_RIGHT = 12, 13, 14, 15
GLYPH_STICK, GLYPH_MENU, GLYPH_VIEW, GLYPH_HOME, GLYPH_TOUCHPAD, GLYPH_PADDLE, GLYPH_BUTTON = 16, 17, 18, 19, 20, 21, 22
GLYPH_MOUSE_LEFT, GLYPH_MOUSE_RIGHT, GLYPH_MOUSE_MIDDLE, GLYPH_WHEEL_UP, GLYPH_WHEEL_DOWN, GLYPH_MOUSE_SIDE = 23, 24, 25, 26, 27, 28
GLYPH_INK = [1, 1, 1, 0.92]


def _circle(name: str, cx: float, cy: float, r: float, *, fill=None, stroke_=None) -> dict:
    """A circle as four cubic arcs."""
    k = r * 0.552284749831
    def pt(x, y):
        return [round(x, 2), round(y, 2)]
    quads = [(pt(cx + r, cy + k), pt(cx + k, cy + r), pt(cx, cy + r)), (pt(cx - k, cy + r), pt(cx - r, cy + k), pt(cx - r, cy)),
             (pt(cx - r, cy - k), pt(cx - k, cy - r), pt(cx, cy - r)), (pt(cx + k, cy - r), pt(cx + r, cy - k), pt(cx + r, cy))]
    path = {"id": name, "commands": [{"id": "m", "op": "move", "points": [pt(cx + r, cy)]}] +
            [{"id": f"q{i}", "op": "cubic", "points": list(q)} for i, q in enumerate(quads)] + [{"id": "z", "op": "close"}]}
    path["fill"] = fill if fill is not None else {"type": "none"}
    if stroke_ is not None:
        path["stroke"] = stroke_
    return path


def _rect(x: float, y: float, w: float, h: float) -> list:
    return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]


def _kinds(glyph, kinds) -> dict:
    """Whether the glyph is one of `kinds`: runs of kinds as ranges."""
    runs, start = [], None
    ordered = sorted(set(kinds))
    for index, kind in enumerate(ordered):
        start = kind if start is None else start
        if index + 1 == len(ordered) or ordered[index + 1] != kind + 1:
            runs.append(op("==", glyph, kind) if start == kind else AND(op(">=", glyph, start), op("<=", glyph, kind)))
            start = None
    return runs[0] if len(runs) == 1 else OR(*runs)


def glyph_chip(doc: HudDocument, ident: str, key_expr, *, h: float, size: float, cut: float, pad: tuple, gap: float,
               min_width: float = 0) -> dict:
    """The control a prompt names (13.4, 13.5, 14.8), `h` u tall. A keyboard
    key is a keycap: its name in dark ProFont `size` u on a white plate at
    0.85 with the 45-degree cut. A controller button draws the glyph of the
    family in use, a vector symbol in the HUD's white whose shape and letter
    carry the identity: a face button's letter or the cross, circle, square
    and triangle (by position when the family is unknown), bumper, trigger,
    d-pad arm, stick, menu, view, home, touchpad, paddle or a labelled
    button; the mouse's buttons and wheel draw the mouse. The engine resolves
    the kind into "<key>.glyph" and the label into the key state."""
    plate = vec(ident + "-chipbg", [{"id": "k", "fill": ui.solid([1, 1, 1, 0.85]), "commands": [
        {"id": "a", "op": "move", "points": [[0, 0]]}, {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
        {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
        {"id": "d", "op": "line", "points": [[u(cut), {"fraction": 1}]]},
        {"id": "e", "op": "line", "points": [[0, {"fraction": 1, "dp": -u(cut)}]]}, {"id": "f", "op": "close"}]}])
    cap = ui.group(ident + "-chip", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "height": ui.length(u(h)),
                                     "min-width": ui.length(u(min_width)), "text-align": ui.keyword("center"),
                                     "padding-left": ui.length(u(pad[0])), "padding-right": ui.length(u(pad[1]))},
                   [plate, bound_label(doc, ident + "-key", key_expr, size, rgb(DARK), face="profont", line=h)])
    if not (isinstance(key_expr, dict) and "state" in key_expr):
        cap["properties"]["margin-right"] = ui.length(u(gap))
        return cap
    key = key_expr["state"]
    glyph = S(doc.number(key + ".glyph", GLYPH_KEYCAP))
    doc.show(cap["id"], op("==", glyph, GLYPH_KEYCAP))
    s = u(h)
    sw = round(max(1.0, s * 0.075), 3)
    ink = GLYPH_INK

    # Hundredths of a dp are ample for glyphs this size and keep the document small.
    def outline(name, points, width=sw, closed=True):
        return pts_path(name, [(round(x, 2), round(y, 2)) for x, y in points], stroke=stroke(ink, width, join="round", cap="round"),
                        closed=closed)

    def solid(name, points):
        return pts_path(name, [(round(x, 2), round(y, 2)) for x, y in points], fill=ui.solid(ink))

    def ring(name, x, y, radius, width=sw):
        return _circle(name, x, y, radius, stroke_=stroke(ink, width, join="round", cap="round"))

    def art(name, paths, kinds):
        node = vec(f"{ident}-{name}", paths, {"display": ui.keyword("none")})
        doc.show(node["id"], _kinds(glyph, kinds))
        return node

    def text(name, line_size, kinds):
        node = bound_label(doc, f"{ident}-{name}", key_expr, line_size, ink, face="profont", line=h,
                           **{"position": ui.keyword("absolute"), "left": ui.length(0), "top": ui.length(0),
                              "width": ui.length(100, "%"), "text-align": ui.keyword("center")})
        node["properties"]["display"] = ui.keyword("none")
        doc.show(node["id"], _kinds(glyph, kinds))
        return node

    def box(name, width, children, kinds):
        node = ui.group(f"{ident}-{name}", {"position": ui.keyword("relative"), "display": ui.keyword("none"),
                                           "width": ui.length(round(width, 3)), "height": ui.length(s)}, children)
        doc.show(node["id"], _kinds(glyph, kinds))
        return node

    c = s / 2
    r = s * 0.46
    round_kinds = [GLYPH_LETTER, GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_SQUARE, GLYPH_TRIANGLE, GLYPH_SOUTH, GLYPH_EAST, GLYPH_WEST,
                   GLYPH_NORTH, GLYPH_STICK, GLYPH_HOME]
    k = s * 0.19
    dots = {GLYPH_SOUTH: (c, c + s * 0.25), GLYPH_EAST: (c + s * 0.25, c), GLYPH_WEST: (c - s * 0.25, c), GLYPH_NORTH: (c, c - s * 0.25)}
    roof = [(c - s * 0.24, c + s * 0.02), (c, c - s * 0.22), (c + s * 0.24, c + s * 0.02)]
    round_box = box("pad-round", s, [
        art("ring", [ring("ring", c, c, r)], round_kinds),
        art("cross", [outline("a", [(c - k, c - k), (c + k, c + k)]), outline("b", [(c + k, c - k), (c - k, c + k)])], [GLYPH_CROSS]),
        art("circle", [ring("o", c, c, s * 0.2)], [GLYPH_CIRCLE]),
        art("square", [outline("q", _rect(c - k, c - k, 2 * k, 2 * k))], [GLYPH_SQUARE]),
        art("triangle", [outline("t", [(c, c - s * 0.21), (c + s * 0.21, c + s * 0.15), (c - s * 0.21, c + s * 0.15)])],
            [GLYPH_TRIANGLE]),
        art("dots", [ring(f"d{kind}", x, y, s * 0.075, sw * 0.8) for kind, (x, y) in dots.items()],
            [GLYPH_SOUTH, GLYPH_EAST, GLYPH_WEST, GLYPH_NORTH]),
        *[art(f"dot{kind}", [_circle("d", x, y, s * 0.095, fill=ui.solid(ink))], [kind]) for kind, (x, y) in dots.items()],
        art("stick", [ring("s", c, c, s * 0.31, sw * 0.8)], [GLYPH_STICK]),
        art("home", [outline("roof", roof + [roof[0]]), outline("walls", [(c - s * 0.16, c - s * 0.04), (c - s * 0.16, c + s * 0.2),
                                                                           (c + s * 0.16, c + s * 0.2), (c + s * 0.16, c - s * 0.04)],
                                                         closed=False)],
            [GLYPH_HOME]),
        text("letter", h * 0.6, [GLYPH_LETTER]),
        text("stick-label", h * 0.36, [GLYPH_STICK]),
    ], round_kinds)
    # The bumper: a shoulder shape rounded toward the top.
    bw = s * 1.6
    bumper = rounded_rect(s * 0.06, s * 0.2, bw - s * 0.12, s * 0.62, s * 0.22)
    bumper_box = box("pad-bumper", bw, [art("bumper", [outline("b", bumper)], [GLYPH_BUMPER]),
                                        text("bumper-label", h * 0.42, [GLYPH_BUMPER])], [GLYPH_BUMPER])
    # The trigger: a rounded top over straight sides.
    tw = s * 1.3
    arc = [(tw / 2 + (tw / 2 - s * 0.1) * math.cos(math.pi + math.pi * i / 12),
            s * 0.42 + s * 0.34 * math.sin(math.pi + math.pi * i / 12)) for i in range(13)]
    trigger = [(s * 0.1, s * 0.92)] + arc + [(tw - s * 0.1, s * 0.92)]
    trigger_box = box("pad-trigger", tw, [art("trigger", [outline("t", trigger)], [GLYPH_TRIGGER]),
                                          text("trigger-label", h * 0.42, [GLYPH_TRIGGER])], [GLYPH_TRIGGER])
    # The d-pad: the cross's outline with the bound arm lit.
    a, e = s * 0.15, s * 0.46
    plus = [(c - a, c - e), (c + a, c - e), (c + a, c - a), (c + e, c - a), (c + e, c + a), (c + a, c + a), (c + a, c + e),
            (c - a, c + e), (c - a, c + a), (c - e, c + a), (c - e, c - a), (c - a, c - a)]
    arms = {GLYPH_DPAD_UP: _rect(c - a, c - e, 2 * a, e - a), GLYPH_DPAD_DOWN: _rect(c - a, c + a, 2 * a, e - a),
            GLYPH_DPAD_LEFT: _rect(c - e, c - a, e - a, 2 * a), GLYPH_DPAD_RIGHT: _rect(c + a, c - a, e - a, 2 * a)}
    dpad_box = box("pad-dpad", s, [art("dpad", [outline("p", plus)], list(arms)),
                                   *[art(f"arm{kind}", [solid("a", points)], [kind]) for kind, points in arms.items()]], list(arms))
    # Menu and view: a pill holding three bars, or two overlapping squares.
    pw = s * 1.25
    pill = rounded_rect(s * 0.06, s * 0.18, pw - s * 0.12, s * 0.64, s * 0.32)
    bars = [outline(f"l{i}", [(pw / 2 - s * 0.2, s * (0.38 + 0.12 * i)), (pw / 2 + s * 0.2, s * (0.38 + 0.12 * i))], sw * 0.8)
            for i in range(3)]
    squares = [outline("q0", _rect(pw / 2 - s * 0.2, s * 0.33, s * 0.24, s * 0.2) + [(pw / 2 - s * 0.2, s * 0.33)], sw * 0.8),
               outline("q1", _rect(pw / 2 - s * 0.04, s * 0.45, s * 0.24, s * 0.2) + [(pw / 2 - s * 0.04, s * 0.45)], sw * 0.8)]
    pill_box = box("pad-pill", pw, [art("pill", [outline("p", pill)], [GLYPH_MENU, GLYPH_VIEW]),
                                    art("menu", bars, [GLYPH_MENU]), art("view", squares, [GLYPH_VIEW])], [GLYPH_MENU, GLYPH_VIEW])
    # The touchpad and a paddle.
    tpw = s * 1.6
    touch_box = box("pad-touch", tpw, [art("touch", [outline("t", rounded_rect(s * 0.06, s * 0.2, tpw - s * 0.12, s * 0.6, s * 0.12)),
                                                     _circle("dot", tpw / 2, s * 0.5, s * 0.07, fill=ui.solid(ink))], [GLYPH_TOUCHPAD])],
                    [GLYPH_TOUCHPAD])
    padw = s * 1.3
    paddle_box = box("pad-paddle", padw, [art("paddle", [outline("p", rounded_rect(s * 0.06, s * 0.2, padw - s * 0.12, s * 0.6,
                                                                                    s * 0.14))], [GLYPH_PADDLE]),
                                          text("paddle-label", h * 0.4, [GLYPH_PADDLE])], [GLYPH_PADDLE])
    # Another labelled button: a pill as wide as its label.
    button = ui.group(f"{ident}-pad-button", {"position": ui.keyword("relative"), "display": ui.keyword("none"), "height": ui.length(s),
                                              "padding-left": ui.length(round(s * 0.3, 3)), "padding-right": ui.length(round(s * 0.3, 3))},
                      [vec(f"{ident}-button", [{"id": "p", "stroke": stroke(ink, sw, join="round"), "commands": [
                          {"id": "a", "op": "move", "points": [[round(s * 0.3, 3), round(s * 0.18, 3)]]},
                          {"id": "b", "op": "line", "points": [[{"fraction": 1, "dp": -round(s * 0.3, 3)}, round(s * 0.18, 3)]]},
                          {"id": "c", "op": "line", "points": [[{"fraction": 1, "dp": -1}, round(s * 0.5, 3)]]},
                          {"id": "d", "op": "line", "points": [[{"fraction": 1, "dp": -round(s * 0.3, 3)}, round(s * 0.82, 3)]]},
                          {"id": "e", "op": "line", "points": [[round(s * 0.3, 3), round(s * 0.82, 3)]]},
                          {"id": "f", "op": "line", "points": [[1, round(s * 0.5, 3)]]}, {"id": "g", "op": "close"}]}]),
                       bound_label(doc, f"{ident}-button-label", key_expr, h * 0.42, ink, face="profont", line=h)])
    doc.show(button["id"], op("==", glyph, GLYPH_BUTTON))
    # The mouse: its body with the bound button lit, the wheel's arrow, or a
    # side button's number.
    mw = s * 0.8
    mx, my, mbw, mbh = (mw - s * 0.56) / 2, s * 0.07, s * 0.56, s * 0.86
    split = my + mbh * 0.42
    mouse_kinds = [GLYPH_MOUSE_LEFT, GLYPH_MOUSE_RIGHT, GLYPH_MOUSE_MIDDLE, GLYPH_WHEEL_UP, GLYPH_WHEEL_DOWN, GLYPH_MOUSE_SIDE]
    wheel = _rect(mw / 2 - s * 0.05, my + s * 0.12, s * 0.1, s * 0.16)
    mouse_box = box("pad-mouse", mw, [
        art("mouse", [outline("body", rounded_rect(mx, my, mbw, mbh, s * 0.26)),
                      outline("split", [(mx, split), (mx + mbw, split)], sw * 0.8),
                      outline("seam", [(mw / 2, my), (mw / 2, split)], sw * 0.8)], mouse_kinds),
        art("mouse-left", [solid("l", [(mx + s * 0.08, split - s * 0.04), (mx + s * 0.08, my + s * 0.2), (mw / 2 - s * 0.06, my + s * 0.06),
                                       (mw / 2 - s * 0.06, split - s * 0.04)])], [GLYPH_MOUSE_LEFT]),
        art("mouse-right", [solid("r", [(mw - mx - s * 0.08, split - s * 0.04), (mw - mx - s * 0.08, my + s * 0.2),
                                        (mw / 2 + s * 0.06, my + s * 0.06), (mw / 2 + s * 0.06, split - s * 0.04)])], [GLYPH_MOUSE_RIGHT]),
        art("mouse-middle", [solid("m", wheel)], [GLYPH_MOUSE_MIDDLE, GLYPH_WHEEL_UP, GLYPH_WHEEL_DOWN]),
        art("wheel-up", [solid("u", [(mw / 2, my + mbh * 0.55), (mw / 2 + s * 0.12, my + mbh * 0.72), (mw / 2 - s * 0.12, my + mbh * 0.72)])],
            [GLYPH_WHEEL_UP]),
        art("wheel-down", [solid("d", [(mw / 2, my + mbh * 0.9), (mw / 2 + s * 0.12, my + mbh * 0.73), (mw / 2 - s * 0.12, my + mbh * 0.73)])],
            [GLYPH_WHEEL_DOWN]),
        text("side-label", h * 0.34, [GLYPH_MOUSE_SIDE]),
    ], mouse_kinds)
    return ui.group(ident + "-glyph", {"position": ui.keyword("relative"), "display": ui.keyword("flex"),
                                       "flex-direction": ui.keyword("row"), "align-items": ui.keyword("center"),
                                       "height": ui.length(s), "margin-right": ui.length(u(gap))},
                    [cap, round_box, bumper_box, trigger_box, dpad_box, pill_box, touch_box, paddle_box, button, mouse_box])


def declare(doc: HudDocument, spec: dict) -> None:
    for key, initial in spec.items():
        if isinstance(initial, bool):
            doc.boolean(key, initial)
        elif isinstance(initial, (int, float)):
            doc.number(key, initial)
        else:
            doc.string(key, initial)


def team_text(team_expr) -> list:
    return colour_of([(op("==", team_expr, 1), rgb(STR_T))], rgb(MAR_T))


# ================================================================ kill feed

FEED_FIELDS = {"attacker": "", "victim": "", "weapon": -1, "kind": 0, "quad": False, "team": False, "attackerTeam": -1,
               "victimTeam": -1, "you": False, "youAttacker": False, "youVictim": False, "teamkill": False,
               "hasAttacker": False, "live": False, "alpha": 0}


# Competitive: on a view too narrow for the kill feed's longest line beside
# the match bar's network readout (about 16:10 and narrower at any HUD scale),
# the top-trailing groups step down under the bar by this much.
UNDER_BAR_DROP = 38


def under_match_bar() -> dict:
    """True in Competitive when the trailing anchor's edge lies within
    806 u of the view's center (scaled), so the feed's 204 u line, 3 u in
    from it, would meet the network readout ending 190 u right of center."""
    room = op("/", op("-", S("hud.width"), op("*", 2, S("hud.right"))), S("hud.scale"))
    return AND(op("==", S("hud.layout"), 3), op("<", room, u(806)))


def kill_feed(doc: HudDocument) -> dict:
    """Top trailing, four lines (two in Tourney, under the strip), each on
    its own plate at 0.35 trailing at 633 u; lines involving you on the
    header-band plate at 0.30 with the marker. Attacker, a Quad mark, the
    weapon at 16.5 dp in the weapon color code (a skull for suicides and the
    world, the teal telefrag mark), a TEAM tag on a team kill, the victim;
    names cut with an ellipsis at 84 u, white for you, the team's text color
    in team modes, #FFFF8D otherwise. The game times the lines (2000 ms hold,
    1000 ms fade, one leaves every 3.1 s)."""
    tourney = op("==", S("gametype"), 2)
    rows = []
    for slot in range(4):
        def f(name, slot=slot):
            return f"oq4_feed{slot}_{name}"
        declare(doc, {f(k): v for k, v in FEED_FIELDS.items()})
        lid = f"feed{slot}"
        name_props = {**ellipsis(84)}

        def name_colour(you_key, team_key):
            return colour_of([(S(you_key), WHITE), (AND(S(f("team")), op("==", S(team_key), 1)), rgb(STR_T)),
                              (S(f("team")), rgb(MAR_T))], rgb(NOTE))
        attacker = bound_label(doc, lid + "-a", S(f("attacker")), 12, rgb(NOTE), line=15, **name_props,
                               **{"margin-right": ui.length(u(5))})
        doc.bind(lid + "-a.color", lid + "-a", "color", name_colour(f("youAttacker"), f("attackerTeam")))
        doc.show(lid + "-a", S(f("hasAttacker")))
        quad = icon_box(lid + "-q", "item_quadkill", 11, glow=True, **{"margin-right": ui.length(u(3))})
        quad["properties"]["display"] = ui.keyword("none")
        doc.show(quad["id"], S(f("quad")))
        marks = []
        for index, glyph in enumerate(MP_ROSTER):
            holder = full(f"{lid}-w{index}", hud_svg.icon_nodes(glyph, f"{lid}-w{index}-g", 0, 0, u(11), tint=rgb(FEED_COLOURS[index])),
                          display=ui.keyword("none"))
            doc.show(holder["id"], AND(op("==", S(f("kind")), 0), op("==", S(f("weapon")), index)))
            marks.append(holder)
        for name, glyph, tint, kind in (("skull", "kill_self", WHITE, 1), ("tele", "kill_telefrag", rgb("#00FFA6"), 2)):
            holder = full(f"{lid}-{name}", hud_svg.icon_nodes(glyph, f"{lid}-{name}-g", 0, 0, u(11), tint=tint), display=ui.keyword("none"))
            doc.show(holder["id"], op("==", S(f("kind")), kind))
            marks.append(holder)
        mark = ui.group(lid + "-mark", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(11)),
                                        "height": ui.length(u(11)), "margin-right": ui.length(u(5))}, marks)
        team_tag = tag_node(doc, lid + "-tag", "#str_231001", [("m", rgb(MAR_T), op("!=", S(f("victimTeam")), 1)),
                                                               ("s", rgb(STR_T), op("==", S(f("victimTeam")), 1))],
                            **{"margin-right": ui.length(u(4)), "display": ui.keyword("none")})
        doc.show(team_tag["id"], S(f("teamkill")))
        victim = bound_label(doc, lid + "-v", S(f("victim")), 12, rgb(NOTE), line=15, **name_props)
        doc.bind(lid + "-v.color", lid + "-v", "color", name_colour(f("youVictim"), f("victimTeam")))
        plate = plate_vec(lid + "-plate", 15, 0.35)
        band = plate_vec(lid + "-band", 15, 0.30, BAND, display="none")
        mark_you = marker_vec(lid + "-marker", -7, 3.5)
        mark_you["properties"]["display"] = ui.keyword("none")
        doc.show(plate["id"], NOT(S(f("you"))))
        doc.show(band["id"], S(f("you")))
        doc.show(mark_you["id"], S(f("you")))
        content = flex_row(lid + "-c", [plate, band, mark_you, attacker, quad, mark, team_tag, victim],
                           **{"height": ui.length(u(15)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(4))})
        top = 24 - 11.5 + slot * 15
        row = ui.group(lid, {**ui.absolute(left=0, top=u(top), width=u(637), height=u(15)), "display": ui.keyword("none"),
                             "flex-direction": ui.keyword("row"), "justify-content": ui.keyword("flex-end"), "opacity": num(1)},
                       [content])
        doc.bind(lid + ".display", lid, "display", sel(S(f("live")), "flex", "none"))
        # The Tourney strip pushes the feed down; Competitive takes the strip
        # away and steps the feed under its bar on narrow views.
        competitive = op("==", S("hud.layout"), 3)
        doc.bind(lid + ".top", lid, "top", sel(under_match_bar(), u(top + UNDER_BAR_DROP),
                                                 sel(AND(tourney, NOT(competitive)), u(top + 46), u(top))))
        doc.bind(lid + ".opacity", lid, "opacity", S(f("alpha")))
        rows.append(row)
    return full("feed", rows)


# ======================================================= top-leading group

def standing(doc: HudDocument) -> dict:
    """Deathmatch and Duel (14.9): two 150x18 u plates, 3 u apart, from y 36:
    the ordinal (the leader's in #FFBE23), the name cut at 76 u and the
    score in Chain, tabular and trailing. Row 1 is the leader with the limit
    ("24 /30"); row 2 is you, or 2nd place when you lead or spectate. Your row
    carries the stock band and rims at 0.40 and the marker, and after it your
    signed gap. A tie for the lead adds a TIED tag."""
    declare(doc, {"player1_name": "", "player1_score": "", "player2_name": "", "player2_score": "", "oq4_rank1": "",
                  "oq4_rank2": "", "oq4_row1_you": False, "oq4_row2_you": False, "oq4_limit": 0, "oq4_gap": 0,
                  "oq4_gap_show": False, "oq4_tied": False})
    rows = []
    for i in (1, 2):
        y = 36 + (i - 1) * 21
        you = S(f"oq4_row{i}_you")
        rid = f"standing{i}"
        plate = plate_vec(rid + "-plate", 18)
        k, cy = -0.22, 9.0

        def sheared_rect(pid, x, ry, w, h, colour):
            pts = [(x, ry), (x + w, ry), (x + w, ry + h), (x, ry + h)]
            return pts_path(pid, [(u(px + k * (py - cy)), u(py)) for px, py in pts], fill=ui.solid(colour))
        band = vec(rid + "-band", [sheared_rect("fill", 2.5, 2, 145, 14, rgb(NOTE, 0.16)),
                                   sheared_rect("rim0", 2.5, 2, 145, 1, rgb(NOTE, 0.32)),
                                   sheared_rect("rim1", 2.5, 15, 145, 1, rgb(NOTE, 0.32))], {"display": ui.keyword("none")})
        doc.show(band["id"], you)
        mark = marker_vec(rid + "-marker", -6, 6)
        mark["properties"]["display"] = ui.keyword("none")
        doc.show(mark["id"], you)
        rank = text_at(doc, rid + "-rank", S(f"oq4_rank{i}"), 8.4, WHITE, right=28, baseline=12.6, extra={"letter-spacing": ui.length(0.45)})
        doc.bind(rid + "-rank.color", rid + "-rank", "color",
                 colour_of([(op("==", S(f"oq4_rank{i}"), S("oq4_rank1")), rgb(VALUE))], WHITE))
        name = text_at(doc, rid + "-name", S(f"player{i}_name"), 10.7, rgb(NAME), x=32, baseline=12.8, width=76,
                       extra=ellipsis(76))
        doc.bind(rid + "-name.color", rid + "-name", "color", colour_of([(you, WHITE)], rgb(NAME)))
        limited = AND(op("==", i, 1), op(">", S("oq4_limit"), 0))
        score = text_at(doc, rid + "-score", S(f"player{i}_score"), 13, rgb(NOTE), right=144, baseline=13.4, face="chain")
        doc.bind(rid + "-score.color", rid + "-score", "color", colour_of([(you, WHITE)], rgb(NOTE)))
        children = [plate, band, mark, rank, name, score]
        if i == 1:
            # The limit after the leader's score: the score moves 13 u in for it.
            doc.bind(rid + "-score.left", rid + "-score", "left", sel(limited, u(131) - u(300), u(144) - u(300)))
            limit = flex_row(rid + "-limit", [label(rid + "-slash", SLASH, 8, [1, 1, 1, 0.45], face="chain", line=8),
                                              bound_label(doc, rid + "-limitn", op("numberText", S("oq4_limit")), 8, [1, 1, 1, 0.45],
                                                          face="chain", line=8)],
                             **{"position": ui.keyword("absolute"), "left": ui.length(u(133)), "top": ui.length(u(13 - 8 * 0.81)),
                                "display": ui.keyword("none")})
            doc.bind(rid + "-limit.display", rid + "-limit", "display", sel(limited, "flex", "none"))
            tied = tag_node(doc, rid + "-tied", "#str_231200", [("v", rgb(VALUE), None)],
                            **{"position": ui.keyword("absolute"), "left": ui.length(u(156)), "top": ui.length(u(4)),
                               "display": ui.keyword("none")})
            doc.show(tied["id"], S("oq4_tied"))
            children += [limit, tied]
        # Your signed gap after your plate; a tie for the lead says TIED instead of +0.
        gap_shown = AND(you, S("oq4_gap_show"), NOT(AND(S("oq4_tied"), op("==", S("oq4_gap"), 0))))
        gap_colour = colour_of([(op(">=", S("oq4_gap"), 0), rgb(OK))], rgb(ERR))
        sign = bound_label(doc, rid + "-sign", sel(op(">=", S("oq4_gap"), 0), PLUS, MINUS), 10, rgb(OK), face="chain", line=10)
        amount = bound_label(doc, rid + "-gapn", op("numberText", op("abs", S("oq4_gap"))), 10, rgb(OK), face="chain", line=10)
        doc.bind(rid + "-sign.color", rid + "-sign", "color", gap_colour)
        doc.bind(rid + "-gapn.color", rid + "-gapn", "color", gap_colour)
        gap = flex_row(rid + "-gap", [sign, amount], **{"position": ui.keyword("absolute"), "left": ui.length(u(156)),
                                                         "top": ui.length(u(13 - 10 * 0.81)), "display": ui.keyword("none")})
        doc.bind(rid + "-gap.display", rid + "-gap", "display", sel(gap_shown, "flex", "none"))
        children.append(gap)
        row = ui.group(rid, {**ui.absolute(left=u(6), top=u(y), width=u(150), height=u(18)), "display": ui.keyword("none")}, children)
        doc.show(rid, op("!=", S(f"player{i}_name"), ""))
        rows.append(row)
    group = full("standing", rows, display=ui.keyword("none"))
    doc.show("standing", OR(op("==", S("gametype"), 1), op("==", S("gametype"), 9)))
    return group


def text_at(doc: HudDocument, ident: str, expr, size: float, colour: list, *, baseline: float, x: float | None = None,
            right: float | None = None, center: float | None = None, width: float = 300, face: str = FACE,
            extra: dict | None = None) -> dict:
    """A bound one-line text with its baseline at `baseline` u, leading edge
    at x, trailing edge at `right` or centred on `center` (u, parent box)."""
    from remastered_hud_base import face_metrics
    size_dp = u(size)
    top = u(baseline) - size_dp * face_metrics(face)["ascent"]
    props = {**ui.typeface(face, size_dp, size_dp, colour), "white-space": ui.keyword("nowrap")}
    if center is not None:
        props.update(ui.absolute(left=u(center - width / 2), top=top, width=u(width), height=size_dp))
        props["text-align"] = ui.keyword("center")
    elif right is not None:
        # A name cut at a maximum width keeps its box's trailing edge there.
        span = (extra or {}).get("max-width", {}).get("value", u(300))
        props.update(ui.absolute(left=u(right) - span, top=top, width=span, height=size_dp))
        props["text-align"] = ui.keyword("right")
    else:
        props.update(ui.absolute(left=u(x), top=top, height=size_dp))
    props["display"] = ui.keyword("block")
    if extra:
        props.update(extra)
    node = ui.label(ident, BOUND_TEXT if expr is not None else "#str_230007", props)
    if expr is not None:
        doc.bind(ident + ".text", ident, "text", expr)
    return node


def dashed(data: str, matrix: tuple, on: float, off: float, steps: int = 12) -> list:
    """Path commands for `data` cut into dashes (on/off in dp after matrix):
    the curves flattened, then walked."""
    a, b, c, d, e, f = matrix
    points, start = [], None
    for seg in hud_svg.parse_path(data):
        if seg[0] == "M":
            start = seg[1]
            points.append(seg[1])
        elif seg[0] == "L":
            points.append(seg[1])
        elif seg[0] == "C":
            p0 = points[-1]
            for i in range(1, steps + 1):
                s = i / steps
                points.append(tuple((1 - s) ** 3 * p0[k] + 3 * (1 - s) ** 2 * s * seg[1][k] + 3 * (1 - s) * s * s * seg[2][k]
                                    + s ** 3 * seg[3][k] for k in range(2)))
        elif seg[0] == "Z" and start is not None:
            points.append(start)
    pts = [(a * x + c * y + e, b * x + d * y + f) for x, y in points]
    commands, drawing, left, current = [], True, on, None
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        length = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
        pos = 0.0
        while pos < length - 1e-9:
            step = min(left, length - pos)
            sx, sy = x0 + (x1 - x0) * pos / length, y0 + (y1 - y0) * pos / length
            ex, ey = x0 + (x1 - x0) * (pos + step) / length, y0 + (y1 - y0) * (pos + step) / length
            if drawing:
                if current is None:
                    commands.append({"id": f"m{len(commands)}", "op": "move", "points": [[round(sx, 4), round(sy, 4)]]})
                    current = True
                commands.append({"id": f"l{len(commands)}", "op": "line", "points": [[round(ex, 4), round(ey, 4)]]})
            pos += step
            left -= step
            if left <= 1e-9:
                drawing = not drawing
                left = on if drawing else off
                current = None
    return commands


def flag_paths(ident: str, colour: list, state: str, scale: float = 1.0) -> list:
    """The team flag (atlas flagIcon): a staff and a waving cloth sheared
    0.22; at base at 0.45, taken filled at 0.90, dropped a dashed outline."""
    m = (scale * U, 0, -0.22 * scale * U, scale * U, 1.8 * scale * U, 0)
    cloth = "M3 2C7 0 10 4 15 2V10C10 12 7 8 3 10Z"
    width = round(1.3 * scale * U, 3)

    def line(paint):
        return {"paint": ui.solid(paint), "widthDp": width, "minimumPixels": 0, "join": "round", "cap": "butt"}
    if state == "dropped":
        paths = [{"id": ident + "-c", "commands": dashed(cloth, m, 2 * scale * U, 1.5 * scale * U), "fill": {"type": "none"},
                  "stroke": line([*colour[:3], 0.9])}]
    else:
        paths = [svg_path(ident + "-c", cloth, matrix=m, fill=ui.solid([*colour[:3], 0.45 if state == "base" else 0.9]),
                          stroke=line(colour))]
    paths.append(svg_path(ident + "-s", "M3 1V17", matrix=m, stroke=line(colour)))
    return paths


def flag_node(doc: HudDocument, ident: str, colour: list, state_expr, x: float, y: float, swing: list | None = None) -> dict:
    """A flag at (x, y) u: at base, taken (pulsing 28 to 34 u once a second,
    the stock pulse, under a "!") or dropped (under a "?"). A `swing` color
    (the one flag in its carrier's team color) washes over the taken flag
    with the beat, there over 250 ms and back over 750 ms. Reduced motion
    holds the taken flag at 34 u in the swing color (14.10)."""
    base = vec(ident + "-base", flag_paths("f", colour, "base"), {"display": ui.keyword("none")})
    doc.show(base["id"], op("==", state_expr, 0))
    taken = vec(ident + "-taken", flag_paths("f", colour, "taken"))
    dropped = vec(ident + "-dropped", flag_paths("f", colour, "dropped"), {"display": ui.keyword("none")})
    doc.show(dropped["id"], op("==", state_expr, 2))
    layers, washes = [taken], []
    if swing is not None:
        wash = vec(ident + "-wash", flag_paths("f", swing, "taken"), {"opacity": num(0)})
        steady = vec(ident + "-wash-rest", flag_paths("f", swing, "taken"), {"display": ui.keyword("none")})
        doc.show(steady["id"], S("oq4.reduced"))
        layers += [wash, steady]
        washes.append(tr(wash["id"], "opacity", [(0, num(0)), (250, num(1)), (1000, num(0))]))
    pulse = full(ident + "-pulse", layers, transform=ui.transform())
    # Reduced motion ends the beat at rest; this wrapper holds the peak instead.
    peak = sel(S("oq4.reduced"), 1.2143, 1)
    held = full(ident + "-held", [pulse], transform=ui.transform(), display=ui.keyword("none"))
    doc.bind(held["id"] + ".reduced", held["id"], "transform", [0, 0, peak, peak, 0])
    doc.show(held["id"], op("==", state_expr, 1))
    marks = []
    for name, key, value in (("bang", "#str_231225", 1), ("query", "#str_231226", 2)):
        node = text_at(doc, f"{ident}-{name}", None, 8.5, WHITE, center=9.5, baseline=9.5, width=12)
        node["properties"]["text"] = ui.text(key)
        node["properties"]["display"] = ui.keyword("none")
        doc.show(node["id"], op("==", state_expr, value))
        marks.append(node)
    cx, cy = u(9), u(9)

    def scaled(s):
        return transform_value((cx - u(9)) * (1 - s), (cy - u(9)) * (1 - s), s, s)
    doc.timeline(ident + "-beat", 1000, [tr(ident + "-pulse", "transform", [(0, scaled(1)), (250, scaled(1.2143)), (1000, scaled(1))]),
                                         *washes], iterations=0)
    # The beat runs only while the flag is taken (the game sends oq4_flags on a change).
    step = when(op("==", state_expr, 1), [play(ident + "-beat")], [cancel(ident + "-beat")])
    doc.on("oq4_flags", step)
    doc.joins.append(step)
    return ui.group(ident, {**ui.absolute(left=u(x), top=u(y), width=u(18), height=u(18)), "display": ui.keyword("block")},
                    [base, dropped, held, *marks])


def team_panels(doc: HudDocument) -> dict:
    """Team modes (14.9): two 150x22 u plates, your team first with the
    marker: a team block in the team color at 0.80 holding the faction mark,
    the team name in its text color and the score in Chain 26 dp against the
    limit. Capture the Flag puts each team's flag at the panel's trailing end
    with the carrier's name after it; elimination modes add alive pips; the
    Dead Zone score flashes toward the team's text color on each tick."""
    declare(doc, {"marine_score": 0, "strogg_score": 0, "oq4_my_team": 0, "oq4_limit": 0, "oq4_team_game": False,
                  "marine_pulse_amount": 0, "strogg_pulse_amount": 0, "oq4_pips": False, "oq4_zone": 0})
    for flag in range(3):
        declare(doc, {f"oq4_flag{flag}_state": 0, f"oq4_flag{flag}_carrier": "", f"oq4_flag{flag}_carrier_team": -1})
    for team in range(2):
        declare(doc, {f"oq4_alive{team}": 0, f"oq4_total{team}": 0})
    gametype = S("gametype")
    two_flag = OR(op("==", gametype, 4), op("==", gametype, 6))
    one_flag = OR(op("==", gametype, 5), op("==", gametype, 7))
    panels = []
    for team, (name_key, colour, text_colour, logo, score_key, pulse_key) in enumerate((
            ("#str_200197", MAR, MAR_T, "marinelogo", "marine_score", "marine_pulse_amount"),
            ("#str_200199", STR, STR_T, "strogglogo", "strogg_score", "strogg_pulse_amount"))):
        pid = f"team{team}"
        mine = op("==", S("oq4_my_team"), team)
        plate = plate_vec(pid + "-plate", 22)
        block = vec(pid + "-block", [fitted_plate_path("block", u(22), r=u(1), fill=ui.solid(rgb(colour, 0.8)))],
                    {**ui.absolute(left=0, top=0, width=u(26), height=u(22)), "display": ui.keyword("block")})
        emblem = ui.group(pid + "-logo", {**ui.absolute(left=u(5), top=u(3), width=u(16), height=u(16)), "display": ui.keyword("block")},
                          hud_svg.icon_nodes(logo, pid + "-logo-g", 0, 0, u(16), tint=WHITE))
        mark = marker_vec(pid + "-marker", -6, 8)
        mark["properties"]["display"] = ui.keyword("none")
        doc.show(mark["id"], mine)
        team_name = text_at(doc, pid + "-name", None, 7, rgb(text_colour), x=32, baseline=9.4,
                            extra={**ellipsis(64), "letter-spacing": ui.length(0.9)})
        team_name["properties"]["text"] = ui.text(name_key)
        score = text_at(doc, pid + "-score", op("numberText", S(score_key)), 17, WHITE, right=112, baseline=16.8, face="chain")
        # Dead Zone: the score jumps toward the team's text color on each control
        # tick and relaxes; reduced motion holds the team color for a second.
        hold = doc.boolean(f"oq4.zoneHold{team}")
        score["properties"]["opacity"] = num(1)
        amount = sel(S("oq4.reduced"), sel(S(hold), 1, 0), op("clamp", S(pulse_key), 0, 1))
        doc.bind(pid + "-score.color", pid + "-score", "color", lerp_colour(WHITE, rgb(text_colour), amount))
        doc.timeline(pid + "-zone-hold", 1000, [tr(pid + "-score", "opacity", [(0, num(1)), (1000, num(1))])],
                     complete=pid + "-zoneDone", essential=True)
        doc.on(f"oq4_zonePulse{team}", {"op": "setState", "values": {hold: True}}, play(pid + "-zone-hold"))
        doc.on(pid + "-zoneDone", {"op": "setState", "values": {hold: False}})
        limit = flex_row(pid + "-limit", [label(pid + "-slash", SLASH, 8, [1, 1, 1, 0.45], face="chain", line=8),
                                          bound_label(doc, pid + "-limitn", op("numberText", S("oq4_limit")), 8, [1, 1, 1, 0.45],
                                                      face="chain", line=8)],
                         **{"position": ui.keyword("absolute"), "left": ui.length(u(114)), "top": ui.length(u(16.4 - 8 * 0.81)),
                            "display": ui.keyword("none")})
        doc.bind(pid + "-limit.display", pid + "-limit", "display", sel(op(">", S("oq4_limit"), 0), "flex", "none"))
        children = [plate, block, emblem, mark, team_name, score, limit]
        # The team's flag at the trailing end; the carrier's name follows the panel.
        flag = flag_node(doc, pid + "-flag", rgb(colour), S(f"oq4_flag{team}_state"), 134, 2.5)
        flag["properties"]["display"] = ui.keyword("none")
        doc.show(flag["id"], two_flag)
        carrier = text_at(doc, pid + "-carrier", S(f"oq4_flag{team}_carrier"), 9, rgb(MAR_T), x=156, baseline=14, extra=ellipsis(80))
        carrier["properties"]["display"] = ui.keyword("none")
        doc.bind(pid + "-carrier.color", pid + "-carrier", "color", team_text(S(f"oq4_flag{team}_carrier_team")))
        doc.show(carrier["id"], AND(two_flag, op("==", S(f"oq4_flag{team}_state"), 1)))
        children += [flag, carrier]
        # One flag: the neutral flag rides the carrying team's panel.
        neutral = flag_node(doc, pid + "-neutral", rgb(NOTE), S("oq4_flag2_state"), 134, 2.5, swing=rgb(colour))
        neutral["properties"]["display"] = ui.keyword("none")
        doc.show(neutral["id"], AND(one_flag, op("==", S("oq4_flag2_state"), 1), op("==", S("oq4_flag2_carrier_team"), team)))
        neutral_carrier = text_at(doc, pid + "-ncarrier", S("oq4_flag2_carrier"), 9, rgb(text_colour), x=156, baseline=14,
                                  extra=ellipsis(80))
        neutral_carrier["properties"]["display"] = ui.keyword("none")
        doc.show(neutral_carrier["id"], AND(one_flag, op("==", S("oq4_flag2_state"), 1), op("==", S("oq4_flag2_carrier_team"), team)))
        children += [neutral, neutral_carrier]
        # Alive pips: one per player in the round, lit while standing.
        pips = []
        for k in range(8):
            px = 156 + k * 6
            pts = [(px, 8), (px + 4, 8), (px + 4, 15), (px, 15)]
            pts = [(u(x0 - 0.22 * (y0 - 11.5)), u(y0)) for x0, y0 in pts]
            lit = vec(f"{pid}-pip{k}", [pts_path("p", pts, fill=ui.solid(rgb(text_colour, 0.9)))], {"display": ui.keyword("none")})
            dim = vec(f"{pid}-pipd{k}", [pts_path("p", pts, fill=ui.solid(rgb(text_colour, 0.2)))], {"display": ui.keyword("none")})
            doc.show(lit["id"], AND(S("oq4_pips"), op("<", k, S(f"oq4_alive{team}"))))
            doc.show(dim["id"], AND(S("oq4_pips"), op(">=", k, S(f"oq4_alive{team}")), op("<", k, S(f"oq4_total{team}"))))
            pips += [lit, dim]
        children += pips
        panel = ui.group(pid, {**ui.absolute(left=u(6), top=u(36), width=u(150), height=u(22)), "display": ui.keyword("block")}, children)
        doc.bind(pid + ".top", pid, "top", sel(mine, u(36), u(61)))
        panels.append(panel)
    # One flag at base or dropped: between the panels, in #FFFF8D.
    free = flag_node(doc, "neutral-free", rgb(NOTE), S("oq4_flag2_state"), 140, 47)
    free["properties"]["display"] = ui.keyword("none")
    doc.show(free["id"], AND(one_flag, op("!=", S("oq4_flag2_state"), 1)))
    # Dead Zone: a ZONE plate under the panels naming the holder, CONTESTED or EMPTY.
    zone_rows = []
    for value, key, colour in ((1, "#str_200197", MAR), (2, "#str_200199", STR), (3, "#str_231219", VALUE), (0, "#str_231220", "#FFFFFF")):
        swatch = ui.group(f"zone-sw{value}", {"position": ui.keyword("relative"), "display": ui.keyword("block"),
                                              "width": ui.length(u(7)), "height": ui.length(u(8)), "margin-right": ui.length(u(4)),
                                              "background-color": ui.colour(rgb(colour, 0.0 if value == 0 else 1.0))})
        text_colour = rgb(MAR_T) if value == 1 else rgb(STR_T) if value == 2 else rgb(VALUE) if value == 3 else [1, 1, 1, 0.55]
        zone_rows.append(flex_row(f"zone-v{value}", [swatch, label(f"zone-t{value}", key, 7.4, text_colour, line=10,
                                                                   **{"letter-spacing": ui.length(0.75)})],
                                  display=ui.keyword("none")))
        doc.bind(f"zone-v{value}.display", f"zone-v{value}", "display", sel(op("==", S("oq4_zone"), value), "flex", "none"))
    zone = ui.group("zone", {**ui.absolute(left=u(6), top=u(86)), "display": ui.keyword("none")}, [
        flex_row("zone-c", [plate_vec("zone-plate", 14), label("zone-label", "#str_231218", 7.4, [1, 1, 1, 0.7], line=10,
                                                                **{"letter-spacing": ui.length(0.9), "margin-right": ui.length(u(6))})]
                 + zone_rows, **{"height": ui.length(u(14)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(8))})])
    doc.show("zone", op("==", gametype, 8))
    group = full("teams", panels + [free, zone], display=ui.keyword("none"))
    doc.show("teams", AND(S("oq4_team_game"), op("!=", gametype, 2)))
    return group


def timer_row(doc: HudDocument, prefix: str) -> dict:
    """The timer's plate and readouts (the timer), its node ids under
    `prefix`, with its own last-ten-seconds pulse."""
    declare(doc, {"oq4_state": 0, "oq4_countdown": False, "oq4_timed": False, "oq4_clock_warn": 0, "oq4_clock": "",
                  "showready": False, "readytext": "", "overtime": False, "overtimetext": "", "showround": False,
                  "roundtext": "", "roundtime": "", "showroundtime": False, "infinity": False, "oq4_mb_ready": ""})
    gametype = S("gametype")
    state = S("oq4_state")
    countdown = S("oq4_countdown")
    warmup = AND(op("==", state, 1), NOT(countdown))
    sudden = AND(op("==", state, 4), NOT(countdown))
    review = op(">=", state, 5)
    live = AND(NOT(countdown), NOT(warmup), NOT(sudden), NOT(review))
    untimed = AND(live, NOT(S("oq4_timed")))
    last_ten = AND(live, NOT(S("overtime")), op("==", S("oq4_clock_warn"), 2))
    last_minute = AND(live, NOT(S("overtime")), op(">=", S("oq4_clock_warn"), 1))
    watch = icon_box(f"{prefix}-watch", "icon_stopwatch", 13, tint=rgb(NOTE), **{"margin-right": ui.length(u(4))})
    infinity = icon_box(f"{prefix}-inf", "icon_infinity", 12, tint=WHITE, **{"margin-right": ui.length(u(4)), "display": ui.keyword("none")})
    doc.bind(f"{prefix}-inf.display", f"{prefix}-inf", "display", sel(OR(untimed, sudden, review), "block", "none"))
    labels = []
    for ident, key, condition in ((f"{prefix}-warm", "#str_231206", warmup), (f"{prefix}-starts", "#str_231207", countdown),
                                  (f"{prefix}-sd", "#str_231208", sudden)):
        node = label(ident, key, 7.2, [1, 1, 1, 0.7], line=10, **{"letter-spacing": ui.length(0.75), "margin-right": ui.length(u(4)),
                                                                   "display": ui.keyword("none")})
        doc.show(ident, condition)
        labels.append(node)
    overtime = bound_label(doc, f"{prefix}-ot", S("overtimetext"), 7.2, [1, 1, 1, 0.7], line=10,
                           **{"letter-spacing": ui.length(0.75), "margin-right": ui.length(u(4)), "display": ui.keyword("none")})
    doc.show(f"{prefix}-ot", AND(live, S("overtime")))
    clock = bound_label(doc, f"{prefix}-clock", S("oq4_clock"), 12.5, WHITE, face="chain", line=13)
    doc.bind(f"{prefix}-clock.color", f"{prefix}-clock", "color",
             colour_of([(countdown, rgb(VALUE)), (last_minute, rgb(VALUE)), (untimed, [1, 1, 1, 0.7])], WHITE))
    doc.bind(f"{prefix}-clock.display", f"{prefix}-clock", "display", sel(AND(OR(countdown, live), NOT(last_ten)), "block", "none"))
    # The last ten seconds: #E46D56, pulsing once a second (1 -> 0.45 -> 1, ease-out).
    urgent = bound_label(doc, f"{prefix}-urgent", S("oq4_clock"), 12.5, rgb(ERR), face="chain", line=13,
                         **{"opacity": num(1), "display": ui.keyword("none")})
    doc.show(f"{prefix}-urgent", last_ten)
    ready = bound_label(doc, f"{prefix}-ready", S("oq4_mb_ready"), 9, WHITE, face="chain", line=10, **{"display": ui.keyword("none")})
    doc.show(f"{prefix}-ready", AND(warmup, S("showready"), op("!=", S("oq4_mb_ready"), "")))
    content = flex_row(f"{prefix}-c", [plate_vec(f"{prefix}-plate", 16), watch, infinity, *labels, overtime, clock, urgent, ready],
                       **{"height": ui.length(u(16)), "padding-left": ui.length(u(4)), "padding-right": ui.length(u(8))})
    # The last ten seconds pulse once a second (opacity 1 -> 0.45 -> 1, ease-out).
    doc.timeline(f"{prefix}-pulse", 1000, [tr(f"{prefix}-urgent", "opacity", [(0, num(1)), (300, num(0.45), [0.0, 0.0, 0.58, 1.0]),
                                                                       (1000, num(1))])], iterations=0)
    # The pulse runs only in the last ten seconds (the game sends oq4_clockWarn on a change).
    step = when(op("==", S("oq4_clock_warn"), 2), [play(f"{prefix}-pulse")], [cancel(f"{prefix}-pulse")])
    doc.on("oq4_clockWarn", step)
    doc.joins.append(step)
    return content


def timer(doc: HudDocument) -> dict:
    """The timer plate (14.9) under the standing or panels: the stopwatch in
    #FFFF8D and one readout per state in Chain: M:SS counting down; elapsed
    time beside the infinity mark when untimed; WARM-UP with the ready count;
    STARTS IN and the countdown; #FFBE23 in the last minute and #E46D56 with a
    1 Hz pulse in the last ten seconds; SUDDEN DEATH with the infinity mark;
    the overtime label with the time. The round modes add a ROUND plate with
    the round clock. Tourney sets its own under the message bar (tourney)."""
    content = timer_row(doc, "timer")
    gametype = S("gametype")
    # Round modes: the clock accompanies ROUND n for the entire timed round.
    # The stock showroundtime flag intentionally only covers its last 30 s.
    round_plate = flex_row("round-c", [plate_vec("round-plate", 14),
                                       bound_label(doc, "round-text", S("roundtext"), 7.4, [1, 1, 1, 0.7], line=10,
                                                   **{"letter-spacing": ui.length(0.75)}),
                                       bound_label(doc, "round-clock", S("roundtime"), 10, rgb(VALUE), face="chain", line=10,
                                                   **{"margin-left": ui.length(u(8)), "display": ui.keyword("none")})],
                           **{"height": ui.length(u(14)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(8)),
                              "margin-top": ui.length(u(2)), "display": ui.keyword("none")})
    doc.show("round-clock", op("!=", S("roundtime"), ""))
    doc.bind("round-c.display", "round-c", "display", sel(S("showround"), "flex", "none"))
    column = ui.group("timer", {**ui.absolute(left=u(6), top=u(82)), "display": ui.keyword("flex"),
                                "flex-direction": ui.keyword("column"), "align-items": ui.keyword("flex-start")},
                      [content, round_plate, *match_notes(doc)])
    team = AND(S("oq4_team_game"), op("!=", gametype, 2))
    doc.bind("timer.top", "timer", "top", sel(op("==", gametype, 8), u(107), sel(team, u(90), u(82))))
    # Tourney sets its timer under the message bar (tourney).
    doc.bind("timer.display", "timer", "display", sel(op("==", gametype, 2), "none", "flex"))
    return column


# ============================================================ center column

def center_column(doc: HudDocument) -> dict:
    """Top center (14.9): the frag notice, the main notice, warm-up lines and
    awards stack from y 81 and move down for each other; each line on its own
    plate at 0.35. Notices hold 2000 ms and fade over 500 ms unless they
    persist; a rank notice sets its ordinal in #FFBE23. Spectators get the
    follow card instead of the stock lines, and the countdown is the timer's."""
    declare(doc, {"frag_notice_text": "", "frag_notice_persist": False, "main_notice_text": "", "main_notice_persist": False,
                  "oq4_rank_notice_text": "", "oq4_rank_notice_pre": "", "oq4_rank_notice_lead": "", "oq4_rank_notice_post": "",
                  "spectatetext1": "", "oq4_spectating": False, "oq4.frag": False, "oq4.main": False, "oq4.award": False,
                  "ig_awards": 0, "ig_award": "", "ig_award_num": "", "arena_hudHidden": False})

    def notice(ident, size, children):
        h = size * 1.35
        return flex_row(ident, [plate_vec(ident + "-plate", h, 0.35)] + children,
                        **{"height": ui.length(u(h)), "padding-left": ui.length(u(8)), "padding-right": ui.length(u(8)),
                           "margin-bottom": ui.length(u(3)), "display": ui.keyword("none"), "opacity": num(1)})
    frag = notice("frag", 17.3, [bound_label(doc, "frag-text", S("frag_notice_text"), 17.3, rgb(NOTE), line=17.3 * 1.35)])
    shot = S("arena_hudHidden")
    doc.bind("frag.display", "frag", "display", sel(AND(S("oq4.frag"), op("!=", S("frag_notice_text"), ""), NOT(shot)), "flex", "none"))
    rank_notice = op("==", S("main_notice_text"), S("oq4_rank_notice_text"))
    main = notice("main", 15.4, [
        bound_label(doc, "main-text", S("main_notice_text"), 15.4, rgb(NOTE), line=15.4 * 1.35, **{"display": ui.keyword("none")}),
        bound_label(doc, "main-pre", S("oq4_rank_notice_pre"), 15.4, rgb(NOTE), line=15.4 * 1.35,
                    **{"display": ui.keyword("none"), "white-space": ui.keyword("pre")}),
        bound_label(doc, "main-lead", S("oq4_rank_notice_lead"), 15.4, rgb(VALUE), line=15.4 * 1.35, **{"display": ui.keyword("none")}),
        bound_label(doc, "main-post", S("oq4_rank_notice_post"), 15.4, rgb(NOTE), line=15.4 * 1.35,
                    **{"display": ui.keyword("none"), "white-space": ui.keyword("pre")})])
    doc.show("main-text", NOT(rank_notice))
    for part in ("main-pre", "main-lead", "main-post"):
        doc.show(part, rank_notice)
    doc.bind("main.display", "main", "display", sel(AND(S("oq4.main"), op("!=", S("main_notice_text"), "")), "flex", "none"))
    # Warm-up: the ready reason, breathing (the stock 900 ms cycle); not for spectators.
    warm = notice("warm", 12.5, [bound_label(doc, "warm-text", S("spectatetext1"), 12.5, rgb(SPEC_TEXT), line=12.5 * 1.35,
                                             **{"opacity": num(1)})])
    doc.bind("warm.display", "warm", "display", sel(AND(op("==", S("oq4_state"), 1), NOT(S("oq4_spectating")),
                                                       op("!=", S("spectatetext1"), ""), op("!=", S("gametype"), 2), NOT(shot)),
                                                   "flex", "none"))
    doc.timeline("warm-breathe", 900, [tr("warm-text", "opacity", [(0, num(1)), (495, num(0.69)), (504, num(0.5)), (603, num(1)),
                                                                   (900, num(1))])], iterations=0)
    # Awards: the earned medal once per time earned (up to nine), else one and a count.
    medals = []
    for k in range(10):
        medal = ui.picture(f"award{k}", "gfx/mp/awards/impressive", {"position": ui.keyword("relative"), "display": ui.keyword("none"),
                                                                   "width": ui.length(u(32)), "height": ui.length(u(32))}, fit="contain")
        doc.bind(f"award{k}.image", f"award{k}", "image", S("ig_award"))
        doc.show(f"award{k}", op("<", k, S("ig_awards")))
        medals.append(medal)
    count = bound_label(doc, "award-count", S("ig_award_num"), 18, WHITE, line=20, **{"margin-left": ui.length(u(4))})
    awards = flex_row("awards", medals + [count], **{"display": ui.keyword("none"), "opacity": num(1)})
    doc.bind("awards.display", "awards", "display", sel(AND(S("oq4.award"), op(">", S("ig_awards"), 0), NOT(shot)), "flex", "none"))
    # The lines under each notice ride a wrapper, so they can slide as it comes
    # and goes instead of jumping.
    def stack(ident, children):
        return ui.group(ident, {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("column"),
                                "align-items": ui.keyword("center"), "width": ui.length(100, "%"), "transform": ui.transform()},
                        children)
    column = ui.group("center", {**ui.absolute(left=0, top=u(81), width=u(640)), "display": ui.keyword("flex"),
                                 "flex-direction": ui.keyword("column"), "align-items": ui.keyword("center")},
                      [frag, stack("center-b1", [main, stack("center-b2", [warm, awards])])])
    # Timing: 2000 ms hold, 500 ms fade; a persisting notice stays. The lines
    # below move down for a notice and close up after it with page.enter
    # easing (220 ms, cubic 0.16 1 0.3 1); reduced motion reflows at once.
    page_enter = [0.16, 1.0, 0.3, 1.0]
    for name, persist, below, size in (("frag", "frag_notice_persist", "center-b1", 17.3), ("main", "main_notice_persist", "center-b2", 15.4)):
        step = size * 1.35 + 3
        doc.timeline(below + "-down", 220, [tr(below, "transform", [(0, transform_value(0, u(-step)), page_enter), (220, transform_value())])])
        doc.timeline(below + "-up", 220, [tr(below, "transform", [(0, transform_value(0, u(step)), page_enter), (220, transform_value())])])
        doc.timeline(name + "-hold", 2500, [tr(name, "opacity", [(0, num(1)), (2000, num(1)), (2500, num(0))])],
                     complete=name + "Done", essential=True)
        doc.timeline(name + "-stay", 2, [tr(name, "opacity", [(0, num(1)), (2, num(1))])], essential=True)
        text = op("!=", S(f"{name}_notice_text"), "")
        shown = AND(S(f"oq4.{name}"), text)
        doc.on(f"{name}_notice", when(AND(NOT(shown), text), [play(below + "-down")]),
               {"op": "setState", "values": {f"oq4.{name}": True}},
               when(S(persist), [play(name + "-stay")], [play(name + "-hold")]))
        doc.on(name + "Done", when(shown, [play(below + "-up")]), {"op": "setState", "values": {f"oq4.{name}": False}})
    doc.timeline("award-hold", 3000, [tr("awards", "opacity", [(0, num(1)), (2500, num(1)), (3000, num(0))])],
                 complete="awardDone", essential=True)
    doc.on("giveIGA", {"op": "setState", "values": {"oq4.award": True}}, play("award-hold"))
    doc.on("awardDone", {"op": "setState", "values": {"oq4.award": False}})
    doc.on("clearIGA", {"op": "setState", "values": {"oq4.award": False}}, cancel("award-hold"))
    # The breathing runs only during the warm-up (the game sends oq4_state on a change).
    step = when(op("==", S("oq4_state"), 1), [play("warm-breathe")], [cancel("warm-breathe")])
    doc.on("oq4_state", step)
    doc.joins.append(step)
    return column


# ================================================================ aim text

def aim_text(doc: HudDocument) -> dict:
    """The aimed player's name (and clan) 20 u below the crosshair at 0.80,
    the team's text color in team modes, with a teammate's health and armor;
    in over 200 ms after 100 ms, out over 500 ms (14.9)."""
    declare(doc, {"aim_text": "", "aim_teammate_health": "", "aim_player_team": 0})
    team = S("oq4_team_game")
    name = text_at(doc, "aim-name", S("aim_text"), 10.6, [1, 1, 1, 0.8], center=320, baseline=272, width=400)
    doc.bind("aim-name.color", "aim-name", "color",
             colour_of([(AND(team, op("==", S("aim_player_team"), 1)), rgb(STR_T, 0.8)), (team, rgb(MAR_T, 0.8))], [1, 1, 1, 0.8]))
    health = text_at(doc, "aim-health", S("aim_teammate_health"), 9.6, [1, 1, 1, 0.8], center=320, baseline=285, width=300, face="chain")
    group = full("aim", [name, health], opacity=num(0))
    doc.timeline("aim-in", 300, [tr("aim", "opacity", [(0, num(0)), (100, num(0)), (300, num(1))])], essential=True)
    doc.timeline("aim-out", 500, [tr("aim", "opacity", [(0, num(1)), (500, num(0))])], essential=True)
    doc.on("aim_text", play("aim-in"))
    doc.on("aim_fade", play("aim-out"))
    return group


# ==================================================================== vote

def vote(doc: HudDocument) -> dict:
    """The vote at the leading edge (14.9): the caller, the fields, the Yes
    and No keys with their tallies as cells, and the time left as a tick
    ramp, on a plate. After voting only the tallies remain."""
    declare(doc, {"voteNotice": 0, "oq4_vote_caller": "", "oq4_vote_yes": 0, "oq4_vote_no": 0, "oq4_vote_voted": False,
                  "oq4_vote_left": 0, "oq4_vote_yes_key": "", "oq4_vote_no_key": ""})
    lines = []
    for k in range(1, 7):
        doc.string(f"voteInfo_{k}")
        lines.append(bound_label(doc, f"vote-l{k}", S(f"voteInfo_{k}"), 9.6, WHITE, line=12, **ellipsis(136)))
        doc.show(f"vote-l{k}", op("!=", S(f"voteInfo_{k}"), ""))
    head = flex_row("vote-head", [label("vote-title", "#str_231209", 7.4, rgb(VALUE), line=11, **{"letter-spacing": ui.length(0.9),
                                                                                                  "margin-right": ui.length(u(5))}),
                                  bound_label(doc, "vote-caller", S("oq4_vote_caller"), 7.4, rgb(VALUE), line=11, **ellipsis(90))])

    def side(ident, key_expr, word, colour, tally):
        chip = key_chip(doc, ident, key_expr, size=6.6, h=10)
        doc.bind(ident + ".glyph-display", chip["id"], "display", sel(S("oq4_vote_voted"), "none", "flex"))
        cells = []
        for q in range(8):
            cell = ui.group(f"{ident}-cell{q}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(4)),
                                                 "height": ui.length(u(4)), "margin-right": ui.length(u(2)),
                                                 "background-color": ui.colour(rgb(colour, 0.18))})
            doc.bind(f"{ident}-cell{q}.bg", f"{ident}-cell{q}", "background-color",
                     colour_of([(op("<", q, tally), rgb(colour, 0.9))], rgb(colour, 0.18)))
            cells.append(cell)
        return flex_row(ident, [chip, ui.group(ident + "-col", {"position": ui.keyword("relative"), "display": ui.keyword("flex"),
                                                                "flex-direction": ui.keyword("column")},
                                               [label(ident + "-word", word, 8, rgb(colour), line=10),
                                                flex_row(ident + "-cells", cells)])],
                        **{"width": ui.length(u(68))})
    tallies = flex_row("vote-tally", [side("vote-yes", S("oq4_vote_yes_key"), "#str_231210", OK, S("oq4_vote_yes")),
                                      side("vote-no", S("oq4_vote_no_key"), "#str_231211", ERR, S("oq4_vote_no"))],
                       **{"margin-top": ui.length(u(3))})
    ramp = []
    for k in range(20):
        h = 1 + k * 0.15
        tick = ui.group(f"vote-tick{k}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(1.4)),
                                          "height": ui.length(u(h)), "margin-right": ui.length(u(5.6)),
                                          "background-color": ui.colour(rgb(VALUE, 0.15))})
        doc.bind(f"vote-tick{k}.bg", f"vote-tick{k}", "background-color",
                 colour_of([(op("<", k, S("oq4_vote_left")), rgb(VALUE, 0.8))], rgb(VALUE, 0.15)))
        ramp.append(tick)
    ramp_row = flex_row("vote-ramp", ramp, **{"align-items": ui.keyword("flex-end"), "height": ui.length(u(4)),
                                               "margin-top": ui.length(u(4))})
    plate = vec("vote-plate", [tall_plate_path("p", u(0.22 * 8), u(1), ui.solid([0, 0, 0, 0.5]))])
    card = plated_card("vote", ui.absolute(left=u(6), top=u(150), width=u(150)),
                       {"padding-left": ui.length(u(6)), "padding-top": ui.length(u(4)), "padding-bottom": ui.length(u(5))},
                       plate, [head, *lines, tallies, ramp_row])
    doc.bind("vote.display", "vote", "display", sel(op("!=", S("voteNotice"), 0), "block", "none"))
    # Competitive: the top-leading corner the standing left (36 u from the top
    # inset, the card's cl anchor rebased onto tl's), clear of the team status.
    rebased = op("+", op("/", op("-", S("hud.top"), op("/", S("hud.height"), 2)), S("hud.scale")), u(240) + u(36))
    doc.bind("vote.top", "vote", "top", sel(op("==", S("hud.layout"), 3), rebased, u(150)))
    return card


# =================================================================== voice

def voice(doc: HudDocument) -> list:
    """Talker plates above the chat box (14.9): YOU on the header-band plate
    with a microphone while you transmit, the talker's name with the speaker
    symbol, each with a scrolling preset trace."""
    declare(doc, {"oq4.voiceSelf": False, "oq4.voiceOther": False, "audio_name": "", "oq4_audio_team": -1, "oq4_my_team": 0,
                  "oq4_team_game": False})
    rows = []
    for ident, you in (("voice-self", True), ("voice-other", False)):
        # The trace in the talker's team text color, #FFFF8D outside team modes.
        team = S("oq4_my_team") if you else S("oq4_audio_team")
        trace_colour = colour_of([(AND(S("oq4_team_game"), op("==", team, 1)), rgb(STR_T, 0.9)),
                                  (AND(S("oq4_team_game"), op("==", team, 0)), rgb(MAR_T, 0.9))], rgb(NOTE, 0.9))
        from hud_waveform import waveform
        graph = waveform(doc, ident+"-graph", 26, 12, rgb(NOTE, .9))
        doc.bind(ident+"-graph.tint", graph["id"], "image-color", trace_colour)
        trace = flex_row(ident + "-trace", [graph], **{"height": ui.length(u(12)), "width": ui.length(u(26)),
                                                       "margin-right": ui.length(u(4)),"overflow":ui.keyword("hidden")})
        if you:
            # Your own transmission: a microphone, its capsule in the cradle
            # on a stem and foot, in the speaker symbol's 10 u box.
            mic = vec(ident + "-sym-g", [
                svg_path("capsule", "M3.6 1.8A1.4 1.4 0 0 1 6.4 1.8V5.4A1.4 1.4 0 0 1 3.6 5.4Z", fill=ui.solid(WHITE)),
                svg_path("cradle", "M2 4.6A3 3 0 0 0 8 4.6M5 7.6V9.2M3 9.2H7",
                         stroke=stroke(WHITE, u(0.9), join="round", cap="round"))])
            symbol = ui.group(ident + "-sym", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(10)),
                                               "height": ui.length(u(10)), "margin-right": ui.length(u(3))}, [mic])
        else:
            symbol = icon_box(ident + "-sym", "icon_speaker", 10, tint=rgb(NOTE), **{"margin-right": ui.length(u(3))})
        name = label(ident + "-name", "#str_231221", 9.6, WHITE, line=12) if you else \
            bound_label(doc, ident + "-name", S("audio_name"), 9.6, rgb(NOTE), line=12, **ellipsis(100))
        plate = plate_vec(ident + "-plate", 15, 0.30 if you else 0.5, BAND if you else "#000000")
        row = flex_row(ident, [plate, trace, symbol, name], **{"height": ui.length(u(15)), "padding-left": ui.length(u(4)),
                                                               "padding-right": ui.length(u(8)), "margin-top": ui.length(u(2)),
                                                               "display": ui.keyword("none")})
        doc.bind(ident + ".display", ident, "display", sel(S("oq4.voiceSelf" if you else "oq4.voiceOther"), "flex", "none"))
        rows.append(row)
    doc.on("show_transmit_self", {"op": "setState", "values": {"oq4.voiceSelf": True}})
    doc.on("hide_transmit_self", {"op": "setState", "values": {"oq4.voiceSelf": False}})
    doc.on("show_transmit", {"op": "setState", "values": {"oq4.voiceOther": True}})
    doc.on("hide_transmit", {"op": "setState", "values": {"oq4.voiceOther": False}})
    return rows


# ================================================================ chat lines

CHAT_FIELDS = {"name": "", "text": "", "kind": 0, "team": -1, "you": False, "live": False, "alpha": 0}
# The open input's history (up to eight messages), the stock message limit
# (the message-mode edit control's maxchars) and the line pitch, 20 dp.
CHAT_SLOTS, CHAT_LIMIT, CHAT_PITCH = 8, 128, 13.3
# Under the lines while the input is open (atlas mpChat): a 7 u gap, the
# prompt row (9 u, or two rows 4 u apart where the box is too narrow for the
# prompts; the peer measures it, oq4.chatPrompts in canvas dp), 4 u, then the
# 18 u field.
CHAT_INPUT = 38


def chat_geometry() -> tuple:
    """The chat box's bottom above the bottom edge and its width, in the
    bottom-leading anchor's dp (see chat_box)."""
    from remastered_hud_base import status_scale
    scale = S("hud.scale")
    bar_scale = status_scale(True)
    beside = lambda half: op("/", op("-", op("-", op("/", S("hud.width"), 2), op("*", u(half), bar_scale)), S("hud.left")), scale)
    room = op("-", beside(219), u(16))
    wide = op(">=", room, u(180))
    competitive = op("==", S("hud.layout"), 3)
    strip_room = op("clamp", op("-", beside(165), u(14)), u(120), u(200))
    width = sel(wide, op("min", room, u(300)), sel(competitive, strip_room, u(200)))
    # Its bottom clears the status bar's top (51 u, 15% higher in Competitive) by 8 u.
    clearance = op("+", op("/", op("*", u(51), bar_scale), scale), u(8))
    return sel(competitive, clearance, sel(wide, u(59), u(150))), width


def chat_history_room() -> tuple:
    """The room for history above the open input (14.9: up to eight
    messages, as many as the space above allows), up to 6 u under the leading
    group above the box: Competitive's team status (its rows end 16.5 u apart
    from y 150 of the center-leading canvas) or the vote card (45 u and 12 u
    a field from its top). The center-leading canvas centers on the view; the
    bottom-leading one stands on the bottom inset; both scale by the HUD scale.
    Returns the room's height (dp) and the messages of one line it holds."""
    from remastered_hud_competitive import competitive, team_mode
    half = op("/", op("-", op("/", S("hud.height"), 2), S("hud.bottom")), S("hud.scale"))
    far = u(10000)
    spectating, mine = S("oq4_spectating"), S("oq4_my_team")
    rows = sel(OR(spectating, op("==", mine, 0)), S("oq4_ts0_count"), S("oq4_ts1_count"))
    team_shown = AND(competitive(), team_mode(), op(">", rows, 0))
    team_top = op("-", op("+", half, u(92)), op("*", rows, u(16.5)))
    fields = u(0)
    for k in range(1, 7):
        fields = op("+", fields, sel(op("!=", S(f"voteInfo_{k}"), ""), u(12), 0))
    rebased = op("+", op("/", op("-", S("hud.top"), op("/", S("hud.height"), 2)), S("hud.scale")), u(240) + u(36))
    vote_top = sel(competitive(), rebased, u(150))
    vote_bottom = op("-", half, op("-", op("+", op("+", vote_top, u(45)), fields), u(240)))
    limit = op("min", sel(team_shown, team_top, far), sel(op("!=", S("voteNotice"), 0), vote_bottom, far))
    bottom, _ = chat_geometry()
    prompts = op("+", u(CHAT_INPUT - 9), op("/", S("oq4.chatPrompts"), S("hud.scale")))
    space = op("max", 0, op("-", op("-", op("-", limit, bottom), prompts), u(6)))
    return space, op("clamp", op("floor", op("/", space, u(CHAT_PITCH))), 0, CHAT_SLOTS)


def chat_lines(doc: HudDocument) -> list:
    """The chat box's lines (14.9): four messages at rest, newest at the
    bottom, Lowpixel 16 dp on a 20 dp pitch; each on its own plate at 0.35
    as wide as its text; a team message opens with the TEAM tag and takes the
    team's text color; all-chat is #FFFF8D with white names; server and vote
    lines carry the information symbol in white at 0.70; your lines use the
    header-band plate at 0.30 and the marker. A long message wraps in the box
    and keeps its lines together. While the input is open the game sends up
    to eight messages of history at full strength (oq4_chat_count): the
    newest that fit the room above the input show, and the lines stand in a
    column from its bottom (the newest first, reversed) that the room clips,
    so wrapped history never reaches the group above."""
    declare(doc, {"oq4_chatting": False, "oq4_chat_count": 0, "oq4_spectating": False, "oq4_my_team": 0, "voteNotice": 0,
                  "oq4_ts0_count": 0, "oq4_ts1_count": 0, "oq4_team_game": False, "oq4.chatPrompts": u(9), "oq4.chatFirst": 0})
    for k in range(1, 7):
        doc.string(f"voteInfo_{k}")
    room, fitting = chat_history_room()
    first_shown = op("-", S("oq4_chat_count"), fitting)
    messages = []
    for slot in range(CHAT_SLOTS):
        def f(name, slot=slot):
            return f"oq4_chat{slot}_{name}"
        declare(doc, {f(k): v for k, v in CHAT_FIELDS.items()})
        cid = f"chat{slot}"
        kind = S(f("kind"))
        team_line = op("==", kind, 1)
        server = op("==", kind, 2)
        text_colour = colour_of([(server, [1, 1, 1, 0.7]), (AND(team_line, op("==", S(f("team")), 1)), rgb(STR_T)),
                                 (team_line, rgb(MAR_T))], rgb(NOTE))
        name_colour = colour_of([(S(f("you")), WHITE), (AND(team_line, op("==", S(f("team")), 1)), rgb(STR_T)),
                                 (team_line, rgb(MAR_T))], WHITE)
        tag = tag_node(doc, cid + "-tag", "#str_231001", [("m", rgb(MAR_T), op("!=", S(f("team")), 1)),
                                                          ("s", rgb(STR_T), op("==", S(f("team")), 1))],
                       **{"display": ui.keyword("none"), "margin-right": ui.length(u(4)), "margin-top": ui.length(u(2))})
        doc.show(tag["id"], team_line)
        info = ui.group(cid + "-info", {"position": ui.keyword("relative"), "display": ui.keyword("none"), "width": ui.length(u(9)),
                                        "height": ui.length(u(9)), "margin-right": ui.length(u(3)), "margin-top": ui.length(u(2))},
                        [vec(cid + "-info-v", [
                            {"id": "ring", "commands": hud_svg.commands("M4.5 0.5A4 4 0 1 1 4.49 0.5Z", (U, 0, 0, U, 0, 0)),
                             "fill": {"type": "none"}, "stroke": {"paint": ui.solid([1, 1, 1, 0.7]), "widthDp": u(1), "minimumPixels": 0,
                                                                  "join": "round", "cap": "round"}},
                            {"id": "dot", "commands": hud_svg.commands("M4.5 2.2V2.5", (U, 0, 0, U, 0, 0)), "fill": {"type": "none"},
                             "stroke": {"paint": ui.solid([1, 1, 1, 0.7]), "widthDp": u(1.1), "minimumPixels": 0, "join": "round",
                                        "cap": "round"}},
                            {"id": "bar", "commands": hud_svg.commands("M4.5 3.8V7.2", (U, 0, 0, U, 0, 0)), "fill": {"type": "none"},
                             "stroke": {"paint": ui.solid([1, 1, 1, 0.7]), "widthDp": u(1.1), "minimumPixels": 0, "join": "miter",
                                        "cap": "butt"}}])])
        doc.show(info["id"], server)
        name = ui.label(cid + "-name", BOUND_TEXT, {**ui.typeface(FACE, u(10.7), u(13.3), WHITE), "display": ui.keyword("inline")})
        doc.bind(cid + "-name.text", cid + "-name", "text", S(f("name")))
        doc.bind(cid + "-name.color", cid + "-name", "color", name_colour)
        colon = ui.label(cid + "-colon", "#str_231227", {**ui.typeface(FACE, u(10.7), u(13.3), WHITE), "display": ui.keyword("inline"),
                                                         "white-space": ui.keyword("pre")})
        doc.bind(cid + "-colon.color", cid + "-colon", "color", name_colour)
        doc.bind(cid + "-name.display", cid + "-name", "display", sel(server, "none", "inline"))
        doc.bind(cid + "-colon.display", cid + "-colon", "display", sel(server, "none", "inline"))
        body = ui.label(cid + "-text", BOUND_TEXT, {**ui.typeface(FACE, u(10.7), u(13.3), rgb(NOTE)), "display": ui.keyword("inline")})
        doc.bind(cid + "-text.text", cid + "-text", "text", S(f("text")))
        doc.bind(cid + "-text.color", cid + "-text", "color", text_colour)
        words = ui.group(cid + "-words", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "flex-shrink": ui.number(1),
                                          "white-space": ui.keyword("normal"), "word-break": ui.keyword("break-word")},
                         [name, colon, body])
        plate = vec(cid + "-plate", [tall_plate_path("p", u(0.22 * 11.3 / 2), u(1), ui.solid([0, 0, 0, 0.35]))])
        band = vec(cid + "-band", [tall_plate_path("p", u(0.22 * 11.3 / 2), u(1), ui.solid(rgb(BAND, 0.30)))], {"display": ui.keyword("none")})
        you_mark = marker_vec(cid + "-marker", -6, 1.5)
        you_mark["properties"]["display"] = ui.keyword("none")
        doc.show(plate["id"], NOT(S(f("you"))))
        doc.show(band["id"], S(f("you")))
        doc.show(you_mark["id"], S(f("you")))
        message = ui.group(cid, {"position": ui.keyword("relative"), "display": ui.keyword("none"), "flex-direction": ui.keyword("row"),
                                 "align-items": ui.keyword("flex-start"), "padding-left": ui.length(u(4)), "padding-right": ui.length(u(4)),
                                 "padding-top": ui.length(u(0)), "margin-top": ui.length(u(2)), "max-width": ui.length(100, "%"),
                                 "opacity": num(1)},
                           [plate, band, you_mark, tag, info, words])
        fits = OR(NOT(S("oq4_chatting")), op(">=", slot, first_shown))
        doc.bind(cid + ".display", cid, "display", sel(AND(S(f("live")), fits), "flex", "none"))
        # A line the room cuts at its top fades out (the peer reads which is
        # the first inside), so the history shows whole messages.
        doc.bind(cid + ".opacity", cid, "opacity", sel(op("<", slot, S("oq4.chatFirst")), 0, S(f("alpha"))))
        messages.append(message)
    # RmlUi clips an overflow: hidden box only once its content overflows
    # toward its trailing or bottom side; the history overflows upward, so an
    # empty strut always reaches past the bottom.
    strut = ui.group("chatlog-strut", {**ui.absolute(left=0, top=0, width=u(1), height=ui.length(200, "%")),
                                       "display": ui.keyword("block"), "pointer-events": ui.keyword("none")})
    log = ui.group("chatlog", {"position": ui.keyword("relative"), "display": ui.keyword("flex"),
                               "flex-direction": ui.keyword("column-reverse"), "align-items": ui.keyword("flex-start"),
                               "width": ui.length(100, "%"), "max-height": ui.length(u(10000)), "overflow": ui.keyword("hidden")},
                   list(reversed(messages)) + [strut])
    doc.bind("chatlog.max-height", "chatlog", "max-height", op("min", room, u(8 * CHAT_PITCH)))
    return [log]


def chat_box(doc: HudDocument, children: list) -> dict:
    """The leading-side box (14.9). Its bottom clears the status bar by 8 u
    (59 u above the bottom edge); where at least 180 u lie between the
    leading edge and the status bar it spans that space, up to 300 u;
    otherwise it is 200 u wide and rises to 150 u above the bottom edge,
    clear of the weapon strip. The voice plates sit directly above it.

    Competitive grows the status bar, the strip and the pickups by 15% and
    puts the team status at the leading edge from y 150, where a risen box
    would meet them: the box keeps its bottom 8 u above the enlarged status
    bar and, where it is not wide, is 200 u wide or as wide as the space
    beside the widest strip (eleven weapons), down to 120 u."""
    # Anchor-local dp (the bl anchor is scaled by hud.scale about its corner).
    bottom, width = chat_geometry()
    box = ui.group("chatbox", {**ui.absolute(left=u(8), width=u(300), height=u(400)), "bottom": ui.length(u(59)), "display": ui.keyword("flex"),
                               "flex-direction": ui.keyword("column"), "justify-content": ui.keyword("flex-end"),
                               "align-items": ui.keyword("flex-start")}, children)
    doc.bind("chatbox.width", "chatbox", "width", width)
    doc.bind("chatbox.bottom", "chatbox", "bottom", bottom)
    return box


# ============================================================ chat input

# The channel tag, the prompts and their keys (ENTER, TAB and ESCAPE in the
# engine's localized key names).
ALL_TAG, SEND_WORD, CHANNEL_WORD, CANCEL_WORD = "#str_231254", "#str_231255", "#str_231256", "#str_42936"
KEY_ENTER, KEY_TAB, KEY_ESCAPE = "#str_107019", "#str_107018", "#str_107020"


def rect_commands() -> list:
    """A rectangle filling its node."""
    return [{"id": "a", "op": "move", "points": [[0, 0]]}, {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
            {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
            {"id": "d", "op": "line", "points": [[0, {"fraction": 1}]]}, {"id": "e", "op": "close"}]


def chat_chip(ident: str, key: str) -> dict:
    """A chat prompt's key (atlas mpChat): its name in dark ProFont 6.6 u on
    a 9 u white chip at 0.78 with a 2.5 u cut at the lower-leading corner."""
    return ui.group(ident, {"position": ui.keyword("relative"), "display": ui.keyword("block"), "height": ui.length(u(9)),
                            "padding-left": ui.length(u(3)), "padding-right": ui.length(u(3))},
                    [vec(ident + "-bg", [{"id": "k", "fill": ui.solid([1, 1, 1, 0.78]), "commands": [
                        {"id": "a", "op": "move", "points": [[0, 0]]}, {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
                        {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
                        {"id": "d", "op": "line", "points": [[u(2.5), {"fraction": 1}]]},
                        {"id": "e", "op": "line", "points": [[0, {"fraction": 1, "dp": -u(2.5)}]]}, {"id": "f", "op": "close"}]}]),
                     label(ident + "-name", key, 6.6, rgb(DARK), face="profont", line=9)])


def chat_prompt(ident: str, key: str, word: str) -> dict:
    """The chip, then the action in Lowpixel 7.6 u white at 0.80 with the
    stock 1 u drop shadow, 9 u before the next prompt."""
    words = ui.group(ident + "-words", {"position": ui.keyword("relative"), "display": ui.keyword("block"),
                                        "margin-left": ui.length(u(3))},
                     [label(ident + "-shadow", word, 7.6, [0, 0, 0, 0.8], line=9, **ui.absolute(left=u(1), top=u(1))),
                      label(ident + "-word", word, 7.6, [1, 1, 1, 0.8], line=9)])
    return flex_row(ident, [chat_chip(ident + "-key", key), words], **{"margin-right": ui.length(u(9))})


def chat_input(doc: HudDocument) -> dict:
    """The chat input under the lines while message mode is open (14.9,
    atlas mpChat): the prompts, Send, Channel (where team chat exists) and
    Cancel, then the field, the stock chat plate recessed: black at 0.80 easing
    to 0.55 at the trailing end, with #CCCC51 rails down the leading side,
    along the lower-leading cut and the first 70% of the bottom. In the field
    the channel tag (ALL in #FFFF8D, TEAM in your team's text color), then the
    entry in Lowpixel 10.7 u #FFFF8D with the caret (a bar, or an underline
    while overstriking) blinking once a second, steady under reduced motion.
    The entry scrolls sideways to keep the caret 3 u inside its view; text
    clipped before the view fades under a #FFFF8D mark, and from 80% of the
    128-character limit the characters left show at the trailing end."""
    declare(doc, {"oq4_chatin_before": "", "oq4_chatin_after": "", "oq4_chatin_left": CHAT_LIMIT, "oq4_chatin_team": False,
                  "oq4_chatin_channels": False, "oq4_chatin_overstrike": False, "oq4_my_team": 0, "oq4.chatClipped": False})
    team = S("oq4_chatin_team")
    strogg = op("==", S("oq4_my_team"), 1)
    channel = chat_prompt("chatin-pr-channel", KEY_TAB, CHANNEL_WORD)
    channel["properties"]["display"] = ui.keyword("none")
    doc.bind("chatin-pr-channel.display", "chatin-pr-channel", "display", sel(S("oq4_chatin_channels"), "flex", "none"))
    prompts = flex_row("chatin-prompts", [chat_prompt("chatin-pr-send", KEY_ENTER, SEND_WORD), channel,
                                          chat_prompt("chatin-pr-cancel", KEY_ESCAPE, CANCEL_WORD)],
                       **{"flex-wrap": ui.keyword("wrap"), "row-gap": ui.length(u(4)), "margin-top": ui.length(u(7))})
    plate = vec("chatin-plate", [
        {"id": "fill", "fill": ui.linear((0, 0), ({"fraction": 1}, 0), [(0, [0, 0, 0, 0.8]), (0.7, [0, 0, 0, 0.8]), (1, [0, 0, 0, 0.55])]),
         "commands": [{"id": "a", "op": "move", "points": [[0, 0]]}, {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
                      {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
                      {"id": "d", "op": "line", "points": [[u(9), {"fraction": 1}]]},
                      {"id": "e", "op": "line", "points": [[0, u(9)]]}, {"id": "f", "op": "close"}]},
        {"id": "rail", "fill": {"type": "none"}, "stroke": ui.stroke(ui.solid(rgb("#CCCC51", 0.8)), u(1), minimum=0),
         "commands": [{"id": "a", "op": "move", "points": [[u(0.5), 0]]}, {"id": "b", "op": "line", "points": [[u(0.5), u(9)]]},
                      {"id": "c", "op": "line", "points": [[u(9), {"fraction": 1, "dp": -u(0.5)}]]},
                      {"id": "d", "op": "line", "points": [[{"fraction": 0.7}, {"fraction": 1, "dp": -u(0.5)}]]}]}])
    tag_all = tag_node(doc, "chatin-all", ALL_TAG, [("c", rgb(NOTE), None)], display=ui.keyword("none"))
    doc.bind("chatin-all.display", "chatin-all", "display", sel(team, "none", "block"))
    tag_team = tag_node(doc, "chatin-team", "#str_231001", [("m", rgb(MAR_T), NOT(strogg)), ("s", rgb(STR_T), strogg)],
                        display=ui.keyword("none"))
    doc.bind("chatin-team.display", "chatin-team", "display", sel(team, "block", "none"))
    entry = {**ui.typeface(FACE, u(10.7), u(18), rgb(NOTE)), "white-space": ui.keyword("pre")}
    clear = [0, 0, 0, 0]
    # The text before the caret three times: an invisible spacer in the view's
    # flow, which lays out as wide as the text but no wider than the view
    # less 3 u and so places the caret; the visible text, set flush against
    # the caret in a wide box, so a longer entry grows toward the leading edge
    # and beyond under the view's clip; and an invisible probe free to be as
    # wide as the text, which the peer compares with the view (the mark).
    spacer = ui.label("chatin-before", BOUND_TEXT, {**entry, "color": ui.colour(clear), "position": ui.keyword("relative"),
                                                    "display": ui.keyword("block")})
    doc.bind("chatin-before.text", "chatin-before", "text", S("oq4_chatin_before"))
    shown = ui.label("chatin-shown", BOUND_TEXT, {**entry, **ui.absolute(right=0, top=0, width=u(1400), height=u(18)),
                                                  "display": ui.keyword("block"), "text-align": ui.keyword("right")})
    doc.bind("chatin-shown.text", "chatin-shown", "text", S("oq4_chatin_before"))
    after = ui.label("chatin-after", BOUND_TEXT, {**entry, **ui.absolute(left=0, top=0), "display": ui.keyword("block")})
    doc.bind("chatin-after.text", "chatin-after", "text", S("oq4_chatin_after"))
    probe_text = ui.label("chatin-probe-text", BOUND_TEXT, {**entry, "color": ui.colour(clear), "position": ui.keyword("relative"),
                                                            "display": ui.keyword("block"), "padding-right": ui.length(u(3))})
    doc.bind("chatin-probe-text.text", "chatin-probe-text", "text", S("oq4_chatin_before"))
    probe = flex_row("chatin-probe", [probe_text], **{**ui.absolute(left=0, top=0, width=u(1400), height=u(18)),
                                                      "pointer-events": ui.keyword("none")})
    carets = []
    over = S("oq4_chatin_overstrike")
    for cid, steady in (("chatin-caret", False), ("chatin-caret-steady", True)):
        caret = ui.group(cid, {**ui.absolute(left=u(0.2), top=u(4), width=u(1.2), height=u(10)), "display": ui.keyword("block"),
                               "background-color": ui.colour(rgb(NOTE)), "opacity": num(1)})
        doc.bind(cid + ".width", cid, "width", sel(over, u(5), u(1.2)))
        doc.bind(cid + ".height", cid, "height", sel(over, u(1.2), u(10)))
        doc.bind(cid + ".top", cid, "top", sel(over, u(13.8), u(4)))
        doc.show(cid, S("oq4.reduced") if steady else NOT(S("oq4.reduced")))
        carets.append(caret)
    at = ui.group("chatin-at", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(0),
                                "height": ui.length(u(18)), "flex-shrink": ui.number(0)}, [shown, after, *carets])
    line = ui.group("chatin-line", {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("row"),
                                    "align-items": ui.keyword("flex-start"), "width": ui.length(100, "%"), "height": ui.length(u(18)),
                                    "padding-right": ui.length(u(3)), "box-sizing": ui.keyword("border-box")},
                    [spacer, at])
    fade = vec("chatin-fade", [{"id": "f", "fill": ui.linear((0, 0), ({"fraction": 1}, 0), [(0, [0, 0, 0, 0.8]), (1, [0, 0, 0, 0])]),
                                "commands": rect_commands()}],
               {**ui.absolute(left=0, top=0, width=u(8), height=u(18)), "display": ui.keyword("none")})
    more = vec("chatin-more", [pts_path("m", [(u(1), u(9)), (u(4), u(6)), (u(4), u(12))], fill=ui.solid(rgb(NOTE, 0.7)))],
               {**ui.absolute(left=0, top=0, width=u(5), height=u(18)), "display": ui.keyword("none")})
    doc.show("chatin-fade", S("oq4.chatClipped"))
    doc.show("chatin-more", S("oq4.chatClipped"))
    view = ui.group("chatin-view", {"position": ui.keyword("relative"), "display": ui.keyword("block"),
                                    "flex-grow": ui.number(1), "flex-shrink": ui.number(1), "min-width": ui.length(0),
                                    "height": ui.length(u(18)), "margin-left": ui.length(u(6)), "margin-right": ui.length(u(8)),
                                    "overflow": ui.keyword("hidden")},
                    [probe, line, fade, more])
    left = S("oq4_chatin_left")
    counting = op("<=", left, CHAT_LIMIT * 0.2)
    doc.bind("chatin-view.margin", "chatin-view", "margin-right", sel(counting, 0, u(8)))
    count = bound_label(doc, "chatin-count", op("numberText", op("max", left, 0)), 7.5, [1, 1, 1, 0.6], face="profont", line=18,
                        **{"margin-left": ui.length(u(4)), "margin-right": ui.length(u(5)), "display": ui.keyword("none")})
    doc.bind("chatin-count.display", "chatin-count", "display", sel(counting, "block", "none"))
    doc.bind("chatin-count.color", "chatin-count", "color", colour_of([(op("<=", left, 0), rgb(ERR, 0.6))], [1, 1, 1, 0.6]))
    row = flex_row("chatin-row", [tag_all, tag_team, view, count],
                   **{**ui.absolute(left=0, top=0, width=ui.length(100, "%"), height=u(18)), "padding-left": ui.length(u(6)),
                      "box-sizing": ui.keyword("border-box")})
    field = ui.group("chatin-field", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(100, "%"),
                                      "height": ui.length(u(18)), "margin-top": ui.length(u(4))}, [plate, row])
    group = ui.group("chatin", {"position": ui.keyword("relative"), "display": ui.keyword("none"), "width": ui.length(100, "%")},
                     [prompts, field])
    doc.bind("chatin.display", "chatin", "display", sel(S("oq4_chatting"), "block", "none"))
    # Opening the input, typing and moving the caret restart the blink on.
    doc.timeline("chatin-blink", 1000, [tr("chatin-caret", "opacity", [(0, num(1)), (499, num(1)), (500, num(0)), (999, num(0)),
                                                                        (1000, num(1))])], iterations=0)
    doc.on("oq4_chatType", play("chatin-blink"))
    return group


# ========================================================== match context

def match_context(doc: HudDocument) -> dict:
    """openQ4's managed-match card under the kill feed (14.9): the phase
    heading, then series, timeouts, readiness and proposals, on a plate with
    an orange edge. Live play uses just the phase and one prioritized summary;
    the scoreboard keeps the full record. The adapter supplies localized lines."""
    keys = ["match_context_phase", "match_context_role", "match_context_pause", "match_context_readiness", "match_context_timeouts",
            "match_context_proposal", "match_context_series", "match_context_items"]
    declare(doc, {"match_context_visible": 0, "match_context_compact": False, "match_context_summary": "", **{k: "" for k in keys}})
    rows = [bound_label(doc, "ctx-phase", S("match_context_phase"), 8.4, rgb("#FFF0C2"), line=12, **{"letter-spacing": ui.length(0.6)})]
    rows.append(bound_label(doc, "ctx-summary", S("match_context_summary"), 7.4, rgb("#C8D6B0"), line=10, **ellipsis(150)))
    doc.show("ctx-summary", AND(S("match_context_compact"), op("!=", S("match_context_summary"), "")))
    colours = {"match_context_role": rgb("#F2B840"), "match_context_series": rgb("#F2B840")}
    for key in keys[1:]:
        ident = "ctx-" + key.split("_")[-1]
        rows.append(bound_label(doc, ident, S(key), 7.4, colours.get(key, rgb("#C8D6B0")), line=10, **ellipsis(150)))
        doc.show(ident, AND(NOT(S("match_context_compact")), op("!=", S(key), "")))
    edge = vec("ctx-plate", [{"id": "p", "fill": ui.solid([0, 0, 0, 0.72]),
                              "stroke": {"paint": ui.solid(rgb(MARKER, 0.55)), "widthDp": u(0.8), "minimumPixels": 1, "join": "miter",
                                         "cap": "butt"},
                              "commands": [{"id": "a", "op": "move", "points": [[0, 0]]},
                                           {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
                                           {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
                                           {"id": "d", "op": "line", "points": [[u(8), {"fraction": 1}]]},
                                           {"id": "e", "op": "line", "points": [[0, {"fraction": 1, "dp": -u(8)}]]},
                                           {"id": "f", "op": "close"}]}])
    card = plated_card("ctx", ui.absolute(left=u(470), top=u(90), width=u(164)),
                       {"padding-left": ui.length(u(8)), "padding-top": ui.length(u(3)), "padding-bottom": ui.length(u(4))}, edge, rows)
    doc.bind("ctx.display", "ctx", "display", sel(op("==", S("match_context_visible"), 1), "block", "none"))
    tourney = AND(op("==", S("gametype"), 2), op("!=", S("hud.layout"), 3))
    doc.bind("ctx.top", "ctx", "top", sel(under_match_bar(), u(90 + UNDER_BAR_DROP), sel(tourney, u(106), u(90))))
    return card


# ============================================================ follow card

def follow_card(doc: HudDocument) -> dict:
    """Spectators (14.9): directly above the status bar, a 236x30 u plate at
    0.58 with the followed player's team color at its leading end, FOLLOWING
    over the name, their place in #FFBE23 and score in Chain, chevrons at the
    card's ends and the controls below; free-flying, the card drops toward the
    bottom edge and reads SPECTATING over Free camera."""
    declare(doc, {"oq4_spectating": False, "oq4_following": False, "oq4_follow_name": "", "oq4_follow_team": -1,
                  "oq4_follow_rank": "", "oq4_follow_score": "", "oq4_key_attack": "", "oq4_key_jump": "", "oq4_key_scores": "",
                  "oq4_key_prev": ""})
    following = S("oq4_following")
    block_m = vec("follow-team-m", [fitted_plate_path("b", u(30), r=u(1), fill=ui.solid(rgb(MAR, 0.9)))],
                  {**ui.absolute(left=0, top=0, width=u(7), height=u(30)), "display": ui.keyword("none")})
    block_s = vec("follow-team-s", [fitted_plate_path("b", u(30), r=u(1), fill=ui.solid(rgb(STR, 0.9)))],
                  {**ui.absolute(left=0, top=0, width=u(7), height=u(30)), "display": ui.keyword("none")})
    doc.show("follow-team-m", AND(following, op("==", S("oq4_follow_team"), 0)))
    doc.show("follow-team-s", AND(following, op("==", S("oq4_follow_team"), 1)))
    heading_f = text_at(doc, "follow-head", None, 6.4, [1, 1, 1, 0.58], x=15, baseline=10.2, extra={"letter-spacing": ui.length(1.35)})
    heading_f["properties"]["text"] = ui.text("#str_231212")
    doc.show("follow-head", following)
    heading_s = text_at(doc, "spec-head", None, 6.4, [1, 1, 1, 0.58], x=15, baseline=10.2, extra={"letter-spacing": ui.length(1.35)})
    heading_s["properties"]["text"] = ui.text("#str_231213")
    doc.show("spec-head", NOT(following))
    name = text_at(doc, "follow-name", S("oq4_follow_name"), 13, WHITE, x=15, baseline=24.5, extra=ellipsis(150))
    doc.show("follow-name", following)
    free = text_at(doc, "spec-free", None, 13, WHITE, x=15, baseline=24.5)
    free["properties"]["text"] = ui.text("#str_231214")
    doc.show("spec-free", NOT(following))
    score = text_at(doc, "follow-score", S("oq4_follow_score"), 15, WHITE, right=226, baseline=24, face="chain")
    doc.show("follow-score", following)
    rank = text_at(doc, "follow-rank", S("oq4_follow_rank"), 8.4, rgb(VALUE), right=204, baseline=23.4, extra={"letter-spacing": ui.length(0.45)})
    doc.show("follow-rank", following)
    chevrons = vec("follow-chevrons", [
        {"id": "l", "fill": {"type": "none"}, "commands": hud_svg.commands("M-6 9L-12 15L-6 21", (U, 0, 0, U, 0, 0)),
         "stroke": {"paint": ui.solid([1, 1, 1, 0.7]), "widthDp": u(1.6), "minimumPixels": 0, "join": "round", "cap": "round"}},
        {"id": "r", "fill": {"type": "none"}, "commands": hud_svg.commands("M242 9L248 15L242 21", (U, 0, 0, U, 0, 0)),
         "stroke": {"paint": ui.solid([1, 1, 1, 0.7]), "widthDp": u(1.6), "minimumPixels": 0, "join": "round", "cap": "round"}}],
        {"display": ui.keyword("none")})
    doc.show("follow-chevrons", following)
    card = ui.group("follow-card", {**ui.absolute(left=u(320 - 118), top=0, width=u(236), height=u(30)), "display": ui.keyword("block")},
                    [plate_vec("follow-plate", 30, 0.58), block_m, block_s, heading_f, heading_s, name, free, rank, score, chevrons])

    def prompt(ident, key_expr, word_key):
        return flex_row(ident, [key_chip(doc, ident, key_expr), label(ident + "-word", word_key, 8.4, [1, 1, 1, 0.85], line=10)],
                        **{"margin-left": ui.length(u(7)), "margin-right": ui.length(u(7))})
    follow_prompts = flex_row("follow-prompts", [prompt("pr-next", S("oq4_key_attack"), "#str_231215"),
                                                 prompt("pr-prev", S("oq4_key_prev"), "#str_42885"),
                                                 prompt("pr-free", S("oq4_key_jump"), "#str_231214")], display=ui.keyword("none"))
    doc.bind("follow-prompts.display", "follow-prompts", "display", sel(following, "flex", "none"))
    spec_prompts = flex_row("spec-prompts", [prompt("pr-follow", S("oq4_key_attack"), "#str_231216"),
                                             prompt("pr-scores", S("oq4_key_scores"), "#str_231217")], display=ui.keyword("none"))
    doc.bind("spec-prompts.display", "spec-prompts", "display", sel(following, "none", "flex"))
    prompts = ui.group("follow-pr", {**ui.absolute(left=0, top=u(35), width=u(640)), "display": ui.keyword("flex"),
                                     "justify-content": ui.keyword("center")}, [follow_prompts, spec_prompts])
    group = ui.group("follow", {**ui.absolute(left=0, top=u(360), width=u(640), height=u(50)), "display": ui.keyword("none")}, [card, prompts])
    doc.show("follow", S("oq4_spectating"))
    doc.bind("follow.top", "follow", "top", sel(following, u(360), u(404)))
    return group


# ================================================================== name tags

TAG_ROWS = 8


def name_tags(doc: HudDocument) -> dict:
    """Free camera (14.9): the players in view carry name tags, nearest first:
    a sheared plate 13 u tall at 0.50 with a 4 u team-color block at its
    leading end (none in deathmatch) and the name in Lowpixel 9 u, 10 u in and
    8 u from the trailing end, and a 6 u pointer at its foot touching the point
    above the player's head. The game projects and traces the players
    (oq4_tag{i}_x/_y as fractions of the view); the tag centers on its point.
    Competitive adds a health line under the plate."""
    declare(doc, {"oq4_tags": 0})
    free = AND(S("oq4_spectating"), NOT(S("oq4_following")))
    tags = []
    for i in range(TAG_ROWS):
        prefix = f"oq4_tag{i}"
        declare(doc, {prefix + "_x": 0, prefix + "_y": 0, prefix + "_name": "", prefix + "_team": -1, prefix + "_health": 0})
        ident = f"tag{i}"
        blocks = []
        for team, colour in ((0, MAR), (1, STR)):
            block = vec(f"{ident}-team{team}", [fitted_plate_path("b", u(13), r=u(1), fill=ui.solid(rgb(colour, 0.9)))],
                        {**ui.absolute(left=0, top=0, width=u(4), height=u(13)), "display": ui.keyword("none")})
            doc.show(block["id"], op("==", S(prefix + "_team"), team))
            blocks.append(block)
        name = bound_label(doc, f"{ident}-name", S(prefix + "_name"), 9, WHITE, line=13, **ellipsis(120))
        pointer = vec(f"{ident}-point", [{"id": "p", "fill": ui.solid([0, 0, 0, 0.5]), "commands": [
            {"id": "a", "op": "move", "points": [[{"fraction": 0.5, "dp": -u(3)}, 0]]},
            {"id": "b", "op": "line", "points": [[{"fraction": 0.5, "dp": u(3)}, 0]]},
            {"id": "c", "op": "line", "points": [[{"fraction": 0.5}, u(4)]]}, {"id": "d", "op": "close"}]}],
                      {**ui.absolute(left=0, top=u(13), height=u(4)), "width": ui.length(100, "%")})
        # Competitive adds a health line under the plate: a white track at 0.20
        # filled to the health at 0.85, in the team list's colors (#E46D56 at
        # 25 and below, #FFBE23 above 100).
        health = S(prefix + "_health")
        fill = ui.group(f"{ident}-hp-fill", {"position": ui.keyword("absolute"), "left": ui.length(0), "top": ui.length(0),
                                              "width": ui.length(0, "%"), "height": ui.length(100, "%"), "display": ui.keyword("block"),
                                              "background-color": ui.colour([1, 1, 1, 0.85])})
        doc.bind(f"{ident}-hp-fill.width", fill["id"], "width", op("clamp", health, 0, 100))
        doc.bind(f"{ident}-hp-fill.bg", fill["id"], "background-color",
                 colour_of([(op("<=", health, 25), rgb(ERR, 0.85)), (op(">", health, 100), rgb(VALUE, 0.85))], [1, 1, 1, 0.85]))
        track = ui.group(f"{ident}-hp", {"position": ui.keyword("absolute"), "left": ui.length(u(4)), "right": ui.length(u(4)),
                                         "top": ui.length(u(15)), "height": ui.length(u(2)), "display": ui.keyword("none"),
                                         "background-color": ui.colour([1, 1, 1, 0.2])}, [fill])
        doc.show(track["id"], op("==", S("hud.layout"), 3))
        plate = ui.group(f"{ident}-plate", {"position": ui.keyword("relative"), "display": ui.keyword("flex"),
                                            "flex-direction": ui.keyword("row"), "align-items": ui.keyword("center"),
                                            "height": ui.length(u(13)), "padding-left": ui.length(u(10)),
                                            "padding-right": ui.length(u(8))},
                         [plate_vec(f"{ident}-back", 13, 0.5), *blocks, name, pointer, track])
        # A wide box centered on the point holds the tag, centered in it.
        holder = ui.group(ident, {**ui.absolute(left=0, top=0, width=u(240), height=u(17)), "display": ui.keyword("none"),
                                  "flex-direction": ui.keyword("row"), "justify-content": ui.keyword("center"),
                                  "transform": ui.transform()}, [plate])
        doc.bind(f"{ident}.show", ident, "display", sel(AND(free, op(">", S("oq4_tags"), i)), "flex", "none"))
        doc.bind(f"{ident}.place", ident, "transform",
                 [op("-", op("*", S(prefix + "_x"), S("hud.width")), u(120)), op("-", op("*", S(prefix + "_y"), S("hud.height")), u(17)),
                  1, 1, 0])
        tags.append(holder)
    return ui.group("tags", {**ui.FULL, "display": ui.keyword("block")}, tags)


# ================================================================== respawn

def respawn_prompt(doc: HudDocument) -> dict:
    """While you lie dead and the rules let you back in (14.9 Remastered
    additions): Respawn with the attack control, centered in the lower third
    on a HUD plate at 0.50; while the server holds the respawn, the seconds
    left in Chain accompany the control and label. The game
    sends oq4_respawn as the prompt appears or goes; it fades in over 200 ms."""
    declare(doc, {"oq4_respawn": 0, "oq4_respawn_in": 0, "oq4_key_attack": ""})
    respawn = S("oq4_respawn")
    ready = flex_row("respawn-ready", [key_chip(doc, "respawn", S("oq4_key_attack"), size=7.6, h=13),
                                       label("respawn-word", "#str_231233", 11.5, WHITE, line=13)],
                     **{"display": ui.keyword("none")})
    doc.bind("respawn-ready.display", "respawn-ready", "display", sel(op(">", respawn, 0), "flex", "none"))
    held = flex_row("respawn-held", [
        bound_label(doc, "respawn-in", op("numberText", S("oq4_respawn_in")), 13, rgb(VALUE), face="chain", line=13)],
        **{"display": ui.keyword("none"), "margin-left": ui.length(u(8))})
    doc.bind("respawn-held.display", "respawn-held", "display", sel(op("==", respawn, 2), "flex", "none"))
    content = flex_row("respawn-c", [plate_vec("respawn-plate", 21), ready, held],
                       **{"height": ui.length(u(21)), "padding-left": ui.length(u(8)), "padding-right": ui.length(u(9)),
                          "opacity": num(1)})
    group = ui.group("respawn", {**ui.absolute(left=0, top=u(318), width=u(640)), "display": ui.keyword("none"),
                                 "justify-content": ui.keyword("center")}, [content])
    doc.bind("respawn.display", "respawn", "display", sel(op(">", respawn, 0), "flex", "none"))
    doc.timeline("respawn-rise", 200, [tr("respawn-c", "opacity", [(0, num(0)), (200, num(1))])])
    step = when(op(">", respawn, 0), [play("respawn-rise")], [cancel("respawn-rise")])
    doc.on("oq4_respawn", step)
    return group


# ================================================================== Tourney

ROUND_TITLES = ("#str_200943", "#str_200944", "#str_200945", "#str_200946")
ROUND_NAMES = ("#str_200286", "#str_200287", "#str_200288", "#str_200289")
AQUA, FINISHED = "#5EB987", "#3E57B7"


def tourney(doc: HudDocument) -> tuple:
    """Tourney (14.9): the bracket strip across the top of the center screen
    on plates at 0.55 (8, 4, 2 and 1 cells), names cut with an ellipsis before
    the score, your row aqua #5EB987 at 0.40 with the marker, a finished
    arena's winner checked and its loser at 0.50 after a blue settle; the
    message bar with its 45-degree end at the top leading; round banners that
    slide 34 u in, hold and slide out over 500 ms. Returns (strip, bar, banners)."""
    declare(doc, {"round": 0, "bracket": 0, "empty": False, "winner": 0, "sel": 0, "spectatetext0": "",
                  "oq4_tourney_label": ""})
    gametype = op("==", S("gametype"), 2)
    rounds = []
    init, done, focus = [], [], []
    for r in range(1, 5):
        cells = 8 >> (r - 1)
        width = 640 / cells
        nodes = []
        for b in range(1, cells + 1):
            cid = f"t{r}b{b}"
            key = f"oq4.{cid}"
            declare(doc, {f"name1_round{r}_bracket{b}": "", f"score1_round{r}_bracket{b}": "", f"name2_round{r}_bracket{b}": "",
                          f"score2_round{r}_bracket{b}": "", key + ".empty": True, key + ".done": False, key + ".winner": 0,
                          key + ".sel": -1})
            here = AND(op("==", S("round"), r), op("==", S("bracket"), b))
            init.append(when(here, [{"op": "setState", "values": {key + ".empty": S("empty"), key + ".done": False, key + ".winner": 0,
                                                                    key + ".sel": -1}}]))
            done.append(when(here, [{"op": "setState", "values": {key + ".done": True, key + ".winner": S("winner")}},
                                    play(cid + "-settle")]))
            focus.append(when(here, [{"op": "setState", "values": {key + ".sel": S("sel")}}]))
            w = width - 2
            children = [plate_vec(cid + "-plate", 30, 0.55)]
            # The finished arena's blue settle from 1.00 to 0.40 under the rows.
            settle = vec(cid + "-blue", [fitted_plate_path("b", u(30), r=u(1), fill=ui.solid(rgb(FINISHED, 1.0)))],
                         {"display": ui.keyword("none"), "opacity": num(0.4)})
            doc.show(settle["id"], S(key + ".done"))
            doc.timeline(cid + "-settle", 500, [tr(settle["id"], "opacity", [(0, num(1)), (500, num(0.4))])])
            children.append(settle)
            for row in (1, 2):
                y = 13 if row == 1 else 27
                mine = OR(op("==", S(key + ".sel"), row), op("==", S(key + ".sel"), 0))
                lose = AND(S(key + ".done"), op("!=", S(key + ".winner"), row))
                win = AND(S(key + ".done"), op("==", S(key + ".winner"), row))
                pts = [(1.5, y - 11), (w - 1.5, y - 11), (w - 1.5, y + 2), (1.5, y + 2)]
                band = vec(f"{cid}-sel{row}", [pts_path("a", [(u(x - 0.22 * (yy - (y - 4.5))), u(yy)) for x, yy in pts],
                                                        fill=ui.solid(rgb(AQUA, 0.4)))], {"display": ui.keyword("none")})
                doc.show(band["id"], AND(mine, NOT(S(key + ".empty"))))
                mark = marker_vec(f"{cid}-mark{row}", -5, y - 8)
                mark["properties"]["display"] = ui.keyword("none")
                doc.show(mark["id"], AND(op("==", S(key + ".sel"), row), NOT(S(key + ".empty"))))
                name = text_at(doc, f"{cid}-n{row}", S(f"name{row}_round{r}_bracket{b}"), 9.3, WHITE, x=4, baseline=y,
                               extra=ellipsis(w - 22))
                doc.bind(f"{cid}-n{row}.color", f"{cid}-n{row}", "color", colour_of([(lose, [1, 1, 1, 0.5])], WHITE))
                score = text_at(doc, f"{cid}-s{row}", S(f"score{row}_round{r}_bracket{b}"), 10, rgb("#FF8000"), right=w - 3,
                                baseline=y, face="chain")
                doc.bind(f"{cid}-s{row}.color", f"{cid}-s{row}", "color",
                         colour_of([(lose, [1, 1, 1, 0.5]), (S(key + ".done"), WHITE)], rgb("#FF8000")))
                check = vec(f"{cid}-check{row}", [{"id": "c", "fill": {"type": "none"},
                                                   "commands": hud_svg.commands(f"M{w - 20} {y - 5}l2.5 2.5 5-5", (U, 0, 0, U, 0, 0)),
                                                   "stroke": {"paint": ui.solid(rgb(OK)), "widthDp": u(1.4), "minimumPixels": 0,
                                                              "join": "round", "cap": "round"}}], {"display": ui.keyword("none")})
                doc.show(check["id"], win)
                for node in (name, score):
                    doc.show(node["id"], NOT(S(key + ".empty")))
                children += [band, mark, name, score, check]
            empty = text_at(doc, f"{cid}-empty", None, 9.3, [1, 1, 1, 0.4], center=w / 2, baseline=19, width=w)
            empty["properties"]["text"] = ui.text("#str_200952")
            doc.show(empty["id"], S(key + ".empty"))
            children.append(empty)
            nodes.append(ui.group(cid, {**ui.absolute(left=u((b - 1) * width + 2), top=u(2), width=u(w), height=u(30)),
                                       "display": ui.keyword("block")}, children))
        group = full(f"round{r}", nodes, display=ui.keyword("none"))
        doc.show(group["id"], AND(gametype, op("==", S("round"), r)))
        rounds.append(group)
    doc.on("arenaInit", *init)
    doc.on("arenaDone", *done)
    doc.on("arenaFocus", *focus)
    strip = full("tourney-strip", rounds)
    # The message bar: black 0.60 with the 45-degree end, the round named once.
    bar = ui.group("tourney-bar", {**ui.absolute(left=0, top=0, width=u(640), height=u(60)), "display": ui.keyword("none")}, [
        vec("tourney-bar-plate", [pts_path("p", [(0, u(35)), (u(434), u(35)), (u(416), u(54)), (0, u(54))],
                                           fill=ui.solid([0, 0, 0, 0.6]))]),
        text_at(doc, "tourney-bar-text", S("oq4_tourney_label"), 10.6, WHITE, x=8, baseline=48, extra=ellipsis(400))])
    doc.show("tourney-bar", AND(gametype, op("!=", S("oq4_tourney_label"), "")))
    # Under the bar, within the center screen: the timer and the ROUND and
    # ARENA plate (the standing's timer gives way in Tourney).
    declare(doc, {"oq4_tourney_round": "", "oq4_mb_sub": ""})
    place = flex_row("tplace-c", [plate_vec("tplace-plate", 16),
                                  bound_label(doc, "tplace-round", S("oq4_tourney_round"), 7.4, [1, 1, 1, 0.7], line=10,
                                              **{"letter-spacing": ui.length(0.75)}),
                                  bound_label(doc, "tplace-arena", S("oq4_mb_sub"), 7.4, [1, 1, 1, 0.7], line=10,
                                              **{"letter-spacing": ui.length(0.75), "margin-left": ui.length(u(8))})],
                     **{"height": ui.length(u(16)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(8)),
                        "margin-left": ui.length(u(4))})
    under = ui.group("tourney-under", {**ui.absolute(left=u(6), top=u(58)), "display": ui.keyword("none"),
                                       "flex-direction": ui.keyword("row"), "align-items": ui.keyword("center")},
                     [timer_row(doc, "ttimer"), place])
    doc.bind("tourney-under.display", "tourney-under", "display", sel(gametype, "flex", "none"))
    instructions = []
    for i in (1, 2):
        doc.string(f"spectatetext{i}")
        node = bound_label(doc, f"tourney-help{i}", S(f"spectatetext{i}"), 9.6, [1, 1, 1, 0.7], line=13,
                           **{"width": ui.length(u(624)), "white-space": ui.keyword("normal"),
                              "word-break": ui.keyword("break-word")})
        doc.show(node["id"], AND(gametype, op("!=", S(f"spectatetext{i}"), "")))
        instructions.append(node)
    # Instructions use the center screen's available width. Localized text
    # wraps in normal flow, so a long first instruction moves the next down.
    help_block = ui.group("tourney-help", {**ui.absolute(left=u(8), top=u(77), width=u(624)),
                                         "display": ui.keyword("none")}, instructions)
    doc.show("tourney-help", gametype)
    bar = full("tourney-top", [bar, under, help_block])
    # Round banners: the black bar slides 34 u down over 500 ms, holds and leaves.
    banner_titles = []
    for r in range(1, 5):
        title = text_at(doc, f"banner-t{r}", None, 19.2, WHITE, right=318, baseline=51, extra={"display": ui.keyword("none")})
        title["properties"]["text"] = ui.text(ROUND_TITLES[r - 1])
        name = text_at(doc, f"banner-n{r}", None, 19.2, rgb("#FF8000"), x=322, baseline=51, extra={"display": ui.keyword("none")})
        name["properties"]["text"] = ui.text(ROUND_NAMES[r - 1])
        doc.show(title["id"], op("==", S("round"), r))
        doc.show(name["id"], op("==", S("round"), r))
        banner_titles += [title, name]
    banner = ui.group("banner", {**ui.absolute(left=0, top=u(-62), width=u(640), height=u(62)), "display": ui.keyword("block"),
                                 "transform": ui.transform()},
                      [ui.group("banner-fill", {**ui.FULL, "display": ui.keyword("block"), "background-color": ui.colour([0, 0, 0, 0.6])}),
                       ui.group("banner-text", {**ui.absolute(left=0, top=0, width=u(640), height=u(62)), "display": ui.keyword("block")},
                                banner_titles)])
    doc.timeline("banner-run", 3500, [tr("banner", "transform", [(0, transform_value(0, 0)), (500, transform_value(0, u(34))),
                                                                 (3000, transform_value(0, u(34))), (3500, transform_value(0, 0))])],
                 essential=False)
    warm_title = text_at(doc, "warmbar-text", None, 19.2, WHITE, center=320, baseline=51, width=640)
    warm_title["properties"]["text"] = ui.text("#str_200953")
    warm = ui.group("warmbar", {**ui.absolute(left=0, top=u(-62), width=u(640), height=u(62)), "display": ui.keyword("block"),
                                "transform": ui.transform()},
                    [ui.group("warmbar-fill", {**ui.FULL, "display": ui.keyword("block"), "background-color": ui.colour([0, 0, 0, 0.6])}),
                     ui.group("warmbar-c", {**ui.absolute(left=0, top=0, width=u(640), height=u(62)), "display": ui.keyword("block")},
                              [warm_title])])
    doc.timeline("warmbar-in", 500, [tr("warmbar", "transform", [(0, transform_value(0, 0)), (500, transform_value(0, u(34)))])],
                 essential=False)
    doc.timeline("warmbar-out", 500, [tr("warmbar", "transform", [(0, transform_value(0, u(34))), (500, transform_value(0, 0))])],
                 essential=False)
    doc.on("roundTransitionIn", play("banner-run"))
    doc.on("warmupTransitionIn", play("warmbar-in"))
    doc.on("warmupTransitionOut", play("warmbar-out"))
    banners = full("tourney-banners", [banner, warm], display=ui.keyword("none"))
    doc.show("tourney-banners", gametype)
    return strip, bar, banners


# =============================================================== statistics

STAT_WEAPONS = ("item_machinegun", "item_shotgun", "item_hyperblaster", "item_grenade", "item_nailgun", "item_rocket", "item_railgun",
                "item_lightning", "item_darkmatter", "item_fire")
STAT_AWARDS = ("excellent", "impressive", "humiliation", "combo_kill", "rampage", "holy_shit")
CTF_AWARDS = ("capture", "assist", "defense")


def statistics(doc: HudDocument) -> dict:
    """Held statistics (14.9) on a soft card at the trailing edge: kills and
    deaths in Chain, accuracy per weapon in the weapon color code, and the
    in-game awards earned with their counts (Capture the Flag adds its own)."""
    declare(doc, {"stat_visible": 0, "stat_frags": "", "stat_deaths": "", "ctf_awards": 0})
    rows = []
    for ident, key, value in (("st-kills", "#str_200201", "stat_frags"), ("st-deaths", "#str_200202", "stat_deaths")):
        rows.append(ui.group(ident, {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("column"),
                                     "width": ui.length(u(66))},
                             [label(ident + "-l", key, 7.2, [1, 1, 1, 0.7], line=10, **{"letter-spacing": ui.length(0.75)}),
                              bound_label(doc, ident + "-v", S(value), 17, rgb(NOTE), face="chain", line=19)]))
    head = flex_row("st-head", rows)
    accuracy = [label("st-acc", "#str_200203", 7.2, [1, 1, 1, 0.7], line=10, **{"letter-spacing": ui.length(0.75), "margin-top": ui.length(u(5))})]
    grid = []
    for index, glyph in enumerate(STAT_WEAPONS):
        key = f"stat_{index + 1}_pct"
        doc.string(key)
        grid.append(flex_row(f"st-w{index}", [icon_box(f"st-w{index}-i", glyph, 9, tint=rgb(FEED_COLOURS[index + 1]),
                                                       **{"margin-right": ui.length(u(4))}),
                                               bound_label(doc, f"st-w{index}-v", S(key), 9.6, WHITE, line=12)],
                             **{"width": ui.length(u(66)), "height": ui.length(u(13))}))
    weapons = ui.group("st-grid", {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("row"),
                                   "flex-wrap": ui.keyword("wrap"), "width": ui.length(u(134))}, grid)
    awards = [label("st-aw", "#str_200204", 7.2, [1, 1, 1, 0.7], line=10, **{"letter-spacing": ui.length(0.75), "margin-top": ui.length(u(5))})]
    medals = []
    for name in STAT_AWARDS + CTF_AWARDS:
        doc.string(name)
        medal = flex_row(f"st-a-{name}", [ui.picture(f"st-a-{name}-m", f"gfx/mp/awards/{name}",
                                                     {"position": ui.keyword("relative"), "display": ui.keyword("block"),
                                                      "width": ui.length(u(16)), "height": ui.length(u(16)), "margin-right": ui.length(u(3))},
                                                     fit="contain"),
                                          bound_label(doc, f"st-a-{name}-v", S(name), 9.6, WHITE, line=12)],
                         **{"width": ui.length(u(44)), "height": ui.length(u(18))})
        shown = op("!=", S(name), "")
        if name in CTF_AWARDS:
            shown = AND(shown, op("!=", S("ctf_awards"), 0))
        doc.bind(medal["id"] + ".display", medal["id"], "display", sel(shown, "flex", "none"))
        medals.append(medal)
    medal_grid = ui.group("st-medals", {"position": ui.keyword("relative"), "display": ui.keyword("flex"), "flex-direction": ui.keyword("row"),
                                        "flex-wrap": ui.keyword("wrap"), "width": ui.length(u(134))}, medals)
    card = plated_card("stats", ui.absolute(left=u(488), top=u(96), width=u(146)),
                       {"padding-left": ui.length(u(8)), "padding-top": ui.length(u(6)), "padding-bottom": ui.length(u(8))},
                       vec("stats-plate", [tall_plate_path("p", u(0.22 * 8), u(1.5), ui.solid([0, 0, 0, 0.55]))]),
                       [head, *accuracy, weapons, *awards, medal_grid])
    doc.bind("stats.display", "stats", "display", sel(op("!=", S("stat_visible"), 0), "block", "none"))
    return card


# ============================================================ Arena ceremony

def arena_presentation(doc: HudDocument) -> dict:
    """The single-player Arena campaign's introduction, entrance and victory
    presentation (mphud.gui): letterbox bars sized from the view's aspect
    toward a 16:9 frame, orange rules, and the cards with the stock timings;
    it covers the whole view, not one anchor. oq4.arena is 1 for the countdown
    entrance, 2 for the victory tableau and 3 for an opponent introduction."""
    declare(doc, {"arena_presentKicker": "", "arena_presentTitle": "", "arena_presentSubtitle": "", "arena_presentScore": "",
                  "arena_presentVictor": "", "arena_presentHasVictor": False, "oq4.arena": 0})
    # Bar height: clamp((1 - aspect * 9/16) / 2, 34/480, 0.11) of the view height (canvas dp).
    frac = op("clamp", op("*", op("-", 1, op("*", op("/", S("hud.width"), S("hud.height")), 0.5625)), 0.5), 0.0708333, 0.11)
    bar_h = op("*", frac, S("hud.height"))
    dark = [0.005, 0.008, 0.005, 0.98]
    slide = 0.11 * 720 + 1
    top = ui.group("arena-top", {**ui.absolute(left=0, top=0), "width": ui.length(100, "%"), "height": ui.length(51),
                                 "overflow": ui.keyword("hidden"), "display": ui.keyword("block")},
                   [ui.group("arena-top-bar", {**ui.absolute(left=0, bottom=0), "width": ui.length(100, "%"), "height": ui.length(slide),
                                               "display": ui.keyword("block"), "background-color": ui.colour(dark), "transform": ui.transform()})])
    bottom = ui.group("arena-bottom", {**ui.absolute(left=0, bottom=0), "width": ui.length(100, "%"), "height": ui.length(51),
                                       "overflow": ui.keyword("hidden"), "display": ui.keyword("block")},
                      [ui.group("arena-bottom-bar", {**ui.absolute(left=0, top=0), "width": ui.length(100, "%"), "height": ui.length(slide),
                                                     "display": ui.keyword("block"), "background-color": ui.colour(dark),
                                                     "transform": ui.transform()})])
    doc.bind("arena-top.height", "arena-top", "height", bar_h)
    doc.bind("arena-bottom.height", "arena-bottom", "height", bar_h)
    shade = ui.group("arena-shade", {**ui.FULL, "display": ui.keyword("block"), "background-color": ui.colour([0, 0, 0, 1]), "opacity": num(0)})
    # Rules 6 u inside each bar's inner edge across the 4:3 canvas (64-576 u).
    rules = []
    for name in ("top", "bottom"):
        rule = ui.group(f"arena-rule-{name}", {**ui.absolute(left=u(64), top=0), "width": ui.length(u(512)), "height": ui.length(u(1)),
                                               "display": ui.keyword("block"), "background-color": ui.colour(rgb("#F27D0F", 0.9)),
                                               "opacity": num(0)})
        doc.bind(f"arena-rule-{name}.top", rule["id"], "top", op("+", bar_h, u(6)) if name == "top" else op("-", op("-", S("hud.height"), bar_h), u(7)))
        rules.append(rule)
    canvas = ui.group("arena-canvas", {"position": ui.keyword("absolute"), "left": ui.length(50, "%"), "margin-left": ui.length(-480), "top": ui.length(0),
                                       "width": ui.length(960), "height": ui.length(100, "%"), "display": ui.keyword("block")}, rules)

    def card_text(ident, key, expr, size, colour, face, baseline, **kw):
        node = text_at(doc, ident, expr, size, colour, center=320, baseline=baseline, width=544, face=face,
                       extra={"opacity": num(0), **kw})
        if key:
            node["properties"]["text"] = ui.text(key)
        return node
    entrance = full("arena-entrance", [
        card_text("arena-e-kicker", None, S("arena_presentKicker"), 15.8, rgb("#F2B840"), FACE, 166),
        card_text("arena-e-title", None, S("arena_presentTitle"), 37.4, rgb("#FFF7D6"), "marine", 220),
        card_text("arena-e-sub", None, S("arena_presentSubtitle"), 15.8, rgb("#C7D6B0"), FACE, 260),
        card_text("arena-e-ready", "#str_42086", None, 13, rgb("#F27D0F"), FACE, 302)], display=ui.keyword("none"))
    doc.show("arena-entrance", OR(op("==", S("oq4.arena"), 1), op("==", S("oq4.arena"), 3)))
    doc.show("arena-e-ready", op("==", S("oq4.arena"), 1))
    victory_top = ui.group("arena-v-top", {**ui.absolute(left=0, top=0, width=u(640), height=u(160)), "display": ui.keyword("block")}, [
        card_text("arena-v-kicker", "#str_42085", None, 15.8, rgb("#F2B840"), FACE, 48),
        card_text("arena-v-title", None, S("arena_presentTitle"), 37.4, rgb("#FFF7D6"), "marine", 100),
        card_text("arena-v-score", None, S("arena_presentScore"), 20.2, rgb("#F2B840"), FACE, 138)])
    doc.bind("arena-v-top.top", "arena-v-top", "top", bar_h)
    victor = ui.group("arena-victor", {**ui.absolute(left=u(160), top=0, width=u(320), height=u(76)), "display": ui.keyword("none"),
                                       "opacity": num(0)}, [
        vec("arena-victor-card", [{"id": "c", "fill": ui.solid([0.015, 0.022, 0.014, 0.78]),
                                    "stroke": {"paint": ui.solid(rgb("#F27D0F", 0.9)), "widthDp": u(1), "minimumPixels": 1, "join": "miter",
                                               "cap": "butt"},
                                    "commands": [{"id": "a", "op": "move", "points": [[0, 0]]},
                                                 {"id": "b", "op": "line", "points": [[{"fraction": 1}, 0]]},
                                                 {"id": "d", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
                                                 {"id": "e", "op": "line", "points": [[0, {"fraction": 1}]]}, {"id": "f", "op": "close"}]}]),
        card_text("arena-victor-label", "#str_42084", None, 12.2, rgb("#ABBF8F"), FACE, 24,
                  **{"left": ui.length(u(-112)), "opacity": num(1)}),
        card_text("arena-victor-name", None, S("arena_presentVictor"), 22.3, rgb("#FFF0C2"), "marine", 52,
                  **{"left": ui.length(u(-112)), "opacity": num(1)})])
    doc.bind("arena-victor.top", "arena-victor", "top", op("-", op("-", S("hud.height"), bar_h), u(118)))
    doc.show("arena-victor", S("arena_presentHasVictor"))
    recording = card_text("arena-v-rec", "#str_42087", None, 10.8, rgb("#ABBF8F"), FACE, 0)
    doc.bind("arena-v-rec.top", "arena-v-rec", "top", op("-", op("-", S("hud.height"), bar_h), u(31)))
    victory = ui.group("arena-victory", {"position": ui.keyword("absolute"), "left": ui.length(50, "%"), "margin-left": ui.length(-480),
                                         "top": ui.length(0), "width": ui.length(960), "height": ui.length(100, "%"),
                                         "display": ui.keyword("none")}, [victory_top, victor, recording])
    doc.show("arena-victory", op("==", S("oq4.arena"), 2))
    entrance_canvas = ui.group("arena-e-canvas", {"position": ui.keyword("absolute"), "left": ui.length(50, "%"), "margin-left": ui.length(-480),
                                                  "top": ui.length(50, "%"), "margin-top": ui.length(-360), "width": ui.length(960),
                                                  "height": ui.length(720), "display": ui.keyword("block")}, [entrance])
    layer = full("arena", [shade, top, bottom, canvas, entrance_canvas, victory], display=ui.keyword("none"))
    doc.show("arena", op("!=", S("oq4.arena"), 0))

    def fade(node, keys):
        return tr(node, "opacity", [(at, num(v)) for at, v in keys])

    def bars(keys):
        return [tr("arena-top-bar", "transform", [(at, transform_value(0, 0 if v else -slide)) for at, v in keys]),
                tr("arena-bottom-bar", "transform", [(at, transform_value(0, 0 if v else slide)) for at, v in keys])]
    def entrance_copy():
        return [fade("arena-e-kicker", [(0, 0), (220, 0), (540, 1), (3100, 1), (3450, 0)]),
                fade("arena-e-title", [(0, 0), (520, 0), (940, 1), (3100, 1), (3450, 0)]),
                fade("arena-e-sub", [(0, 0), (520, 0), (940, 1), (3100, 1), (3450, 0)]),
                fade("arena-e-ready", [(0, 0), (1050, 0), (1300, 1), (3100, 1), (3450, 0)])]
    doc.timeline("arena-entrance-run", 4000, [
        fade("arena-shade", [(0, 0), (400, 0.42), (3500, 0.42), (3950, 0)]), *bars([(0, 0), (400, 1), (3500, 1), (3950, 0)]),
        fade("arena-rule-top", [(0, 0), (500, 0.9), (3500, 0.9), (3850, 0)]),
        fade("arena-rule-bottom", [(0, 0), (500, 0.9), (3500, 0.9), (3850, 0)]),
        *entrance_copy()], complete="arenaEntranceDone", essential=True)
    # The entrance over a frame the introductions left open: every track starts
    # at its first key, so the open frame must be authored open or it would
    # snap shut and reopen.
    doc.timeline("arena-entrance-held-run", 4000, [
        fade("arena-shade", [(0, 0.3), (400, 0.42), (3500, 0.42), (3950, 0)]), *bars([(0, 1), (3500, 1), (3950, 0)]),
        fade("arena-rule-top", [(0, 0.9), (3500, 0.9), (3850, 0)]),
        fade("arena-rule-bottom", [(0, 0.9), (3500, 0.9), (3850, 0)]),
        *entrance_copy()], complete="arenaEntranceDone", essential=True)
    # An introduction: the first combatant opens the frame at a lighter shade
    # than the entrance so the fighter reads clearly; every combatant fades its
    # own copy in. Nothing closes the frame - the entrance takes it over.
    doc.timeline("arena-intro-open", 500, [
        fade("arena-shade", [(0, 0), (400, 0.3)]), *bars([(0, 0), (400, 1)]),
        fade("arena-rule-top", [(0, 0), (500, 0.9)]), fade("arena-rule-bottom", [(0, 0), (500, 0.9)])], essential=True)
    doc.timeline("arena-intro-card", 460, [
        fade("arena-e-kicker", [(0, 0), (60, 0), (320, 1)]),
        fade("arena-e-title", [(0, 0), (140, 0), (460, 1)]),
        fade("arena-e-sub", [(0, 0), (140, 0), (460, 1)])], essential=True)
    doc.timeline("arena-intro-clear-run", 400, [
        fade("arena-shade", [(0, 0.3), (350, 0), (400, 0)]), *bars([(0, 1), (350, 0), (400, 0)]),
        fade("arena-rule-top", [(0, 0.9), (300, 0), (400, 0)]), fade("arena-rule-bottom", [(0, 0.9), (300, 0), (400, 0)]),
        fade("arena-e-kicker", [(0, 1), (250, 0), (400, 0)]), fade("arena-e-title", [(0, 1), (250, 0), (400, 0)]),
        fade("arena-e-sub", [(0, 1), (250, 0), (400, 0)])], complete="arenaClearDone", essential=True)
    doc.timeline("arena-victory-run", 2950, [
        fade("arena-shade", [(0, 0), (500, 0.18), (2950, 0.18)]), *bars([(0, 0), (450, 1), (2950, 1)]),
        fade("arena-rule-top", [(0, 0), (550, 0.9), (2950, 0.9)]), fade("arena-rule-bottom", [(0, 0), (550, 0.9), (2950, 0.9)]),
        fade("arena-v-kicker", [(0, 0), (220, 0), (520, 1), (2950, 1)]), fade("arena-v-title", [(0, 0), (220, 0), (640, 1), (2950, 1)]),
        fade("arena-v-score", [(0, 0), (620, 0), (920, 1), (2950, 1)]), fade("arena-victor", [(0, 0), (900, 0), (1350, 1), (2950, 1)]),
        fade("arena-v-rec", [(0, 0), (2600, 0), (2950, 0.9)])], essential=True)
    doc.timeline("arena-clear-run", 400, [
        fade("arena-shade", [(0, 0.42), (350, 0), (400, 0)]), *bars([(0, 1), (350, 0), (400, 0)]),
        fade("arena-rule-top", [(0, 0.9), (300, 0), (400, 0)]), fade("arena-rule-bottom", [(0, 0.9), (300, 0), (400, 0)]),
        fade("arena-v-kicker", [(0, 1), (250, 0), (400, 0)]), fade("arena-v-title", [(0, 1), (250, 0), (400, 0)]),
        fade("arena-v-score", [(0, 1), (250, 0), (400, 0)]), fade("arena-victor", [(0, 1), (250, 0), (400, 0)]),
        fade("arena-v-rec", [(0, 0.9), (250, 0), (400, 0)])], complete="arenaClearDone", essential=True)
    # Each card plays its run first, so the properties it shares with an older
    # run move to it, then cancels the older runs: a clear still fading out
    # would otherwise complete underneath the new card and hide the layer.
    introducing = op("==", S("oq4.arena"), 3)
    doc.on("arenaCampaignIntroduce", when(op("!=", S("oq4.arena"), 3), [play("arena-intro-open")]),
           {"op": "setState", "values": {"oq4.arena": 3}}, play("arena-intro-card"),
           cancel("arena-entrance-run"), cancel("arena-entrance-held-run"), cancel("arena-clear-run"), cancel("arena-intro-clear-run"))
    doc.on("arenaCampaignEntrance", when(introducing, [play("arena-entrance-held-run")], [play("arena-entrance-run")]),
           {"op": "setState", "values": {"oq4.arena": 1}},
           cancel("arena-intro-open"), cancel("arena-intro-card"), cancel("arena-clear-run"), cancel("arena-intro-clear-run"))
    doc.on("arenaCampaignVictory", {"op": "setState", "values": {"oq4.arena": 2}}, play("arena-victory-run"),
           cancel("arena-intro-open"), cancel("arena-intro-card"), cancel("arena-entrance-run"), cancel("arena-entrance-held-run"),
           cancel("arena-clear-run"), cancel("arena-intro-clear-run"))
    doc.on("arenaCampaignPresentationClear", when(introducing, [play("arena-intro-clear-run")], [play("arena-clear-run")]),
           cancel("arena-intro-open"), cancel("arena-intro-card"))
    doc.on("arenaEntranceDone", {"op": "setState", "values": {"oq4.arena": 0}})
    doc.on("arenaClearDone", {"op": "setState", "values": {"oq4.arena": 0}})
    return layer


# ======================================================= credits, connection

def match_notes(doc: HudDocument) -> list:
    """Buy-mode credits and the Dead Zone status line, as plates in the
    timer's column; nothing shows while they are empty."""
    declare(doc, {"credits": "", "gameStatus": ""})
    out = []
    for ident, key in (("credits", "credits"), ("game-status", "gameStatus")):
        row = flex_row(ident, [plate_vec(ident + "-plate", 14), bound_label(doc, ident + "-t", S(key), 9, WHITE, line=11, **ellipsis(160))],
                       **{"height": ui.length(u(14)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(8)),
                          "margin-top": ui.length(u(2)), "display": ui.keyword("none")})
        doc.bind(ident + ".display", ident, "display", sel(op("!=", S(key), ""), "flex", "none"))
        out.append(row)
    return out


def net_level(doc: HudDocument) -> dict:
    """Network quality's lit bars (13.13) for your connection: four to 60 ms,
    three to 120 ms, two to 200 ms and one above, the worse of that and the
    loss, each 2% of which removes a bar, down to one."""
    declare(doc, {"oq4_ping": 0, "oq4_loss": 0})
    ping = S("oq4_ping")
    by_ping = op("select", op("<=", ping, 60), 4, op("select", op("<=", ping, 120), 3, op("select", op("<=", ping, 200), 2, 1)))
    return op("max", 1, op("min", by_ping, op("-", 4, op("floor", op("/", S("oq4_loss"), 2)))))


def connection_notice(doc: HudDocument) -> dict:
    """A lagging connection (14.9, 13.13): the network symbol, its bars by
    the ping and loss, and the ping in ms, on a header-band plate under the
    kill feed."""
    declare(doc, {"IsLagged": False})
    ping = S("oq4_ping")
    lit = net_level(doc)
    bars = []
    for k in range(4):
        h = 2 + k * 1.6
        bar = ui.group(f"lag-b{k}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(2.2)),
                                     "height": ui.length(u(h)), "margin-right": ui.length(u(1.2)),
                                     "background-color": ui.colour([1, 1, 1, 0.2])})
        doc.bind(f"lag-b{k}.bg", bar["id"], "background-color",
                 colour_of([(AND(op("<", k, lit), op(">=", lit, 3)), rgb(OK, 0.9)), (AND(op("<", k, lit), op("==", lit, 2)), rgb("#E3AD36", 0.9)),
                            (op("<", k, lit), rgb(ERR, 0.9))], [1, 1, 1, 0.2]))
        bars.append(bar)
    row = flex_row("lag", [plate_vec("lag-plate", 15, 0.3, BAND),
                           flex_row("lag-bars", bars, **{"align-items": ui.keyword("flex-end"), "height": ui.length(u(8)),
                                                         "margin-right": ui.length(u(4))}),
                           bound_label(doc, "lag-ping", op("numberText", ping), 9.6, WHITE, face="chain", line=12),
                           label("lag-ms", "#str_231228", 7.2, [1, 1, 1, 0.7], line=12, **{"margin-left": ui.length(u(2))})],
                   **{"height": ui.length(u(15)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(6))})
    holder = ui.group("lag-holder", {**ui.absolute(left=0, top=u(78), width=u(637)), "display": ui.keyword("none"),
                                     "justify-content": ui.keyword("flex-end")}, [row])
    doc.bind("lag-holder.display", "lag-holder", "display", sel(S("IsLagged"), "flex", "none"))
    doc.bind("lag-holder.top", "lag-holder", "top", sel(under_match_bar(), u(78 + UNDER_BAR_DROP), u(78)))
    return holder


# ================================================================ document

def mp_document() -> dict:
    """The Remastered multiplayer HUD (mphud.gui): standing or team panels
    and the timer top-leading, the center column top center, the kill feed
    and the managed-match card top-trailing, aim text under the crosshair,
    the vote at the leading edge, chat and voice at the bottom-leading
    corner, the follow card or the respawn prompt over the status bar, and
    the free camera's name tags over the players in view."""
    doc = HudDocument("openq4.hud.remastered.mp")
    layout_state(doc)
    doc.number("gametype", 1)
    feed = kill_feed(doc)
    stand = standing(doc)
    teams = team_panels(doc)
    clock = timer(doc)
    column = center_column(doc)
    aim = aim_text(doc)
    ballot = vote(doc)
    talkers = voice(doc)
    lines = chat_lines(doc)
    ctx = match_context(doc)
    follow = follow_card(doc)
    respawn = respawn_prompt(doc)
    strip, bar, banners = tourney(doc)
    stats = statistics(doc)
    lag = connection_notice(doc)
    arena = arena_presentation(doc)
    tags = name_tags(doc)
    from remastered_hud_competitive import competitive, match_bar, team_status
    matchbar = match_bar(doc)
    leading, trailing = team_status(doc)
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Competitive (ui_hudLayout 3) trades the standing or team panels, the
    # timer and the Tourney strip for the match bar and the team status.
    remastered_top = full("tl-rem", [stand, teams, clock])
    doc.show("tl-rem", NOT(competitive()))
    strip_slot = full("tc-strip", [strip, bar])
    doc.show("tc-strip", NOT(competitive()))
    # Minimal (ui_hudLayout 2) keeps the held statistics, the vote, chat and
    # voice, the follow card, the respawn prompt and the Arena presentation.
    # An Arena presentation shot (arena_hudHidden) keeps only its own layer
    # and the main notice in the center column.
    def chrome(ident: str, children: list) -> dict:
        node = ui.group(ident, {**ui.FULL, "display": ui.keyword("block")}, children)
        doc.show(ident, NOT(S("arena_hudHidden")))
        return node
    root = gameplay_root(doc, "mp-root", [
        chrome("tags-shot", [beyond_minimal(doc, "tags-full", [tags])]),
        anchor(doc, "tl", [chrome("tl-shot", [beyond_minimal(doc, "tl-full", [remastered_top])])]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [chrome("strip-shot", [strip_slot]), column,
                                                            chrome("banners-shot", [banners])]),
                             chrome("matchbar-shot", [matchbar])]),
        anchor(doc, "tr", [chrome("tr-shot", [beyond_minimal(doc, "tr-full", [feed, ctx, lag])])]),
        anchor(doc, "cr", [chrome("cr-shot", [trailing, stats])]),
        anchor(doc, "cc", [chrome("cc-shot", [beyond_minimal(doc, "cc-full", [aim])])]),
        anchor(doc, "cl", [chrome("cl-shot", [leading, ballot])]),
        anchor(doc, "bl", [chrome("bl-shot", [chat_box(doc, talkers + lines + [chat_input(doc)])])]),
        anchor(doc, "bc", [chrome("bc-shot", [follow, respawn])], scale="hud.centerScale"),
        arena,
    ])
    # The peer measures whether the chat entry outgrows its view (the probe,
    # the text with its 3 u, is wider than the view) for the mark before it.
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.9", "stock": "guis/mphud.gui",
                                                      "readbacks": [["oq4.chatClipped", "chatin-probe-text", "chatin-view"],
                                                                    ["oq4.chatPrompts", "chatin-prompts", "@height"],
                                                                    ["oq4.chatFirst", "chatlog", "@firstInside", "chat", "8"]]}}})
