"""Choice tiles for the retained menus (docs/dev/ui-visual-design.md section 6
plates, cut at 45 degrees): a row of tiles that hold one choice between them,
and a switch for an option that is on or off.

A page's long list of plates mixed what it chooses (a difficulty, a campaign)
with what it does (START GAME, BACK), each a full plate on the 30 u pitch.
The choices stand as tiles in one row instead, the chosen one lit, and an
on/off option as a switch, so the plates under them are the page's actions
alone (user note, 11 November 2026).
"""
from __future__ import annotations

from build_retained_screens import (MARKER, OLIVE, ORANGE, RAIL_DARK, U, Document, absolute, colour, group, keyword, label, linear,
                                    marker_path, number, path, rgb, solid, stroke, transform, typeface, vector)

TILE_CUT = 9.0     # u: a tile's lower-leading cut
TILE_TEXT = 17.0   # dp: a tile's Marine label


def chevrons(count: int, centre: float, top: float, tint: list, reach: float = 11.0, rise: float = 6.5, pitch: float = 6.5) -> list:
    """`count` rank chevrons stacked from `top`, each `reach` dp either side
    of `centre` and `rise` dp tall."""
    return [path(f"chevron{index}", [(centre - reach, top + index * pitch + rise), (centre, top + index * pitch),
                                      (centre + reach, top + index * pitch + rise)], closed=False, stroke=stroke(solid(tint), 2.4))
            for index in range(count)]


def tile_shape(w: float, h: float, cut: float, inset: float = 0.0) -> list:
    nudge = inset * 0.414
    return [(inset, inset), (w - inset, inset), (w - inset, h - inset), (cut + nudge, h - inset), (inset, h - cut - nudge)]


def choice_tile(doc: Document, ident: str, key: str, left: float, top: float, width: float, height: float, *, chosen: dict,
                action: str | None = None, event: str | None = None, art=None, lit_art=None, size: float = TILE_TEXT) -> dict:
    """One choice of a row (u): a dark tile with the plates' #CCCC51 rail on
    its leading edge, cut and foot, its Marine label centred along its foot
    and `art` (a function of a tint, returning paths in the tile's box) over
    it. While `chosen`, an expression, holds, the tile is lit: an orange wash
    and outline, the corner marker in its upper trailing corner and its art
    in orange (`lit_art` where the lit tile draws another: a switch's knob
    at its other end). Hover and focus light the plate and turn the label
    orange; focus adds the inset line."""
    w, h, cut = U * width, U * height, U * TILE_CUT
    whole = absolute(left=0, top=0, width=w, height=h)
    shape = tile_shape(w, h, cut)
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.5)}, [
        path("fill", shape, fill=linear((0, 0), (0, h), [(0, [0, 0, 0, 0.9]), (1, [0, 0, 0, 0.6])])),
        path("rail", [(0.75, 0.75), (0.75, h - cut - 0.3), (cut + 0.3, h - 0.75), (w - 0.75, h - 0.75)], closed=False,
             stroke=stroke(linear((0, 0), (w, 0), [(0, rgb(RAIL_DARK)), (0.6, rgb(RAIL_DARK)), (1, rgb(RAIL_DARK, 0.25))]), 1.5))])
    drawn = art or (lambda tint: [])
    rest_art = vector(f"{ident}-art", {**whole, "opacity": number(0.75), "pointer-events": keyword("none")}, drawn([1, 1, 1, 1]))
    lit = vector(f"{ident}-lit", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
        path("wash", shape, fill=linear((0, 0), (0, h), [(0, rgb(ORANGE, 0.04)), (1, rgb(ORANGE, 0.32))])),
        path("edge", tile_shape(w, h, cut, 0.75), stroke=stroke(solid(rgb(ORANGE)), 1.5)),
        marker_path("mark", w - 7 - 9, 7, 9, rgb(ORANGE)), *(lit_art or drawn)(rgb(ORANGE))])
    doc.bind(f"{ident}-lit.display", f"{ident}-lit", "display", {"op": "select", "args": [chosen, "block", "none"]})
    doc.bind(f"{ident}-art.display", f"{ident}-art", "display", {"op": "select", "args": [chosen, "none", "block"]})
    rest_art["properties"]["display"] = keyword("block")
    focus = vector(f"{ident}-focus", {**whole, "opacity": number(0)}, [
        path("inset", tile_shape(w, h, cut, 4.0), stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    line = round(size * 1.25, 3)
    text_node = label(f"{ident}-label", key, {**absolute(left=6, top=h - line - 9, width=w - 12, height=line),
                                              **typeface("marine", size, line, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"),
                                              "text-align": keyword("center"), "overflow": keyword("hidden"), "transform": transform()})

    def state(plate_alpha: float, inset: float, tint: list, moved: dict) -> list:
        return [(f"{ident}-plate", "opacity", number(plate_alpha)), (f"{ident}-focus", "opacity", number(inset)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved)]
    ids = doc.states(ident, {
        "default": state(0.5, 0, [1, 1, 1, 0.8], transform()),
        "hover": state(0.9, 0, rgb(ORANGE), transform()),
        "focus": state(0.9, 1, rgb(ORANGE), transform()),
        "pressed": state(1, 1, rgb(ORANGE), transform(tx=1, ty=1)),
        "disabled": state(0.25, 0, [1, 1, 1, 0.3], transform()),
    })
    control = {"role": "button", "label": key, "states": ids}
    control["action" if action else "event"] = action or event
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=h), [plate, rest_art, lit, focus, text_node], control=control)


def switch_tile(doc: Document, ident: str, key: str, left: float, top: float, width: float, height: float, *, on: dict,
                action: str | None = None, event: str | None = None, on_key: str, off_key: str, size: float = TILE_TEXT) -> dict:
    """An option that is on or off (u): the tile's plate with a switch at its
    leading end (a 30 dp track whose knob stands at the trailing end and
    turns orange while `on`), its label, and ON or OFF at its trailing end."""
    w, h, cut = U * width, U * height, U * TILE_CUT
    whole = absolute(left=0, top=0, width=w, height=h)
    shape = tile_shape(w, h, cut)
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.5)}, [
        path("fill", shape, fill=linear((0, 0), (w, 0), [(0, [0, 0, 0, 0.9]), (0.7, [0, 0, 0, 0.7]), (1, [0, 0, 0, 0.3])])),
        path("rail", [(0.75, 0.75), (0.75, h - cut - 0.3), (cut + 0.3, h - 0.75), (w - 0.75, h - 0.75)], closed=False,
             stroke=stroke(linear((0, 0), (w, 0), [(0, rgb(RAIL_DARK)), (0.6, rgb(RAIL_DARK)), (1, rgb(RAIL_DARK, 0.25))]), 1.5))])
    track_w, track_h, knob = 30.0, 14.0, 10.0
    x, y = U * 12, round((h - track_h) / 2, 3)
    box = [(x, y), (x + track_w, y), (x + track_w, y + track_h), (x, y + track_h)]

    def knob_at(at: float) -> list:
        return [(at, y + 2), (at + knob, y + 2), (at + knob, y + track_h - 2), (at, y + track_h - 2)]
    off = vector(f"{ident}-off", {**whole, "display": keyword("block"), "pointer-events": keyword("none")}, [
        path("track", box, fill=solid([0, 0, 0, 0.6]), stroke=stroke(solid(rgb(OLIVE, 0.7)), 1.2)),
        path("knob", knob_at(x + 2), fill=solid(rgb(MARKER)))])
    lit = vector(f"{ident}-on", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
        path("track", box, fill=solid(rgb(ORANGE, 0.25)), stroke=stroke(solid(rgb(ORANGE)), 1.2)),
        path("knob", knob_at(x + track_w - 2 - knob), fill=solid(rgb(ORANGE)))])
    doc.bind(f"{ident}-on.display", f"{ident}-on", "display", {"op": "select", "args": [on, "block", "none"]})
    doc.bind(f"{ident}-off.display", f"{ident}-off", "display", {"op": "select", "args": [on, "none", "block"]})
    focus = vector(f"{ident}-focus", {**whole, "opacity": number(0)}, [
        path("inset", tile_shape(w, h, cut, 4.0), stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    value_w = U * 44
    text_left = x + track_w + 12
    text_node = label(f"{ident}-label", key, {**absolute(left=text_left, top=0, width=w - text_left - value_w - 8, height=h),
                                              **typeface("marine", size, h, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"),
                                              "overflow": keyword("hidden"), "transform": transform()})
    value = label(f"{ident}-value", off_key, {**absolute(left=w - value_w - 6, top=0, width=value_w, height=h),
                                              **typeface("marine", size, h, rgb(ORANGE)), "white-space": keyword("nowrap"),
                                              "text-align": keyword("right"), "pointer-events": keyword("none")})
    doc.bind(f"{ident}-value.text", f"{ident}-value", "text", {"op": "select", "args": [on, on_key, off_key]})

    def state(plate_alpha: float, inset: float, tint: list, moved: dict) -> list:
        return [(f"{ident}-plate", "opacity", number(plate_alpha)), (f"{ident}-focus", "opacity", number(inset)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved)]
    ids = doc.states(ident, {
        "default": state(0.5, 0, [1, 1, 1, 0.8], transform()),
        "hover": state(0.9, 0, rgb(ORANGE), transform()),
        "focus": state(0.9, 1, rgb(ORANGE), transform()),
        "pressed": state(1, 1, rgb(ORANGE), transform(tx=1, ty=1)),
        "disabled": state(0.25, 0, [1, 1, 1, 0.3], transform()),
    })
    control = {"role": "button", "label": key, "states": ids}
    control["action" if action else "event"] = action or event
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=h), [plate, off, lit, focus, text_node, value], control=control)
