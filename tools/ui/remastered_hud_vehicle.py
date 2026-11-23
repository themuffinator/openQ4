"""The Remastered vehicle HUD (guis/vehicles/hud.gui; ui-visual-design.md 14.12).

The walker and the hover tank share the stock vehicle HUD. Remastered centres
its status bar on the bottom edge: two numeral-free gauges on the Marine gauge
art (ARMOR, the hull, with cells, and SHIELDS) and the chamfered weapon panel
with the current weapon's glyph, the other weapon's, the rocket count and the
charge bar. It keeps the transmission display top-trailing and the HUD prompt
(Exit, or a lock and Vehicle locked) bottom-trailing, and keeps the stock
weapon lines, hit flash, electrical warning, boss bar (20 u toward the
trailing side), missile warning and timed notices.

Geometry and timing are the stock GUI's (guis/vehicles/hud.gui) and the
atlas's vehicleBar; the bracket, electrical warning and hit flash are vector
reconstructions measured from their stock art. Every string is a language
key. The game publishes the jump key (oq4_key_jump) beside the stock fields.
"""
from __future__ import annotations

import build_retained_scopes as scopes
import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (AND, NOT, OR, S, U, HudDocument, anchor, beyond_minimal, box, gameplay_root, join_events,
                                 layout_state, num,
                                 op, play,
                                 cancel, pts_path,
                                 rgb, rounded_rect, sel, shadowed, shear_group, sheared_points, stroke, svg_path, text_box,
                                 tr, transform_value, u, vec)
from remastered_hud_groups import boss_bar, flash_cap, missile_chevron, notices, transmission
from remastered_hud_mp import bound_label, flex_row, glyph_chip, label, plate_vec
from remastered_hud_status import BACK, ST_BACK, backing_nodes, clip_polygon, full, scaled

# The vehicle gauge: the Marine gauge art from x 36.2, without its numeral zone.
VBACK = "M36.2 10.4Q36.2 9.2 37.4 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H37.4Q36.2 49.3 36.2 48.1Z"
VFILL = "M40.6 14.8Q40.6 13.8 41.6 13.8H118.6Q119.6 13.8 119.6 14.8V34.5H107.3A16.6 16.6 0 0 0 93.2 44.7H41.6Q40.6 44.7 40.6 43.7Z"
CELLS = (51.0, 62.7, 74.5, 86.2, 97.9, 109.1)
FILL_LEAD, FILL_SPAN, FILL_TRAIL = 40.6, 79.0, 119.6
INK = "#A8A360"          # labels and icons, at 0.60
WARN = "#FF3300"
LINE = "#FF8000"         # the weapon lines and their brackets
PANEL = "M11.4 403.6H65.1L91 429H103.4V476.4H11.4Z"
# vehicle_weapon (rvVehiclePosition::UpdateHUD): 0 the hover tank's cannon,
# 1 its machinegun, 2 the walker's machineguns, 3 its rocket pods. Each shows
# its glyph (atlas VGLYPH: x, y, height, opacity) and the other weapon's.
GLYPHS = {"veh_rocket": (14, 437, 26, 0.8), "veh_mgun": (14, 436, 33, 0.8), "veh_shell": (16, 432, 30, 0.6)}
WEAPONS = [("veh_shell", "veh_mgun"), ("veh_mgun", "veh_shell"), ("veh_mgun", "veh_rocket"), ("veh_rocket", "veh_mgun")]
# The entry lines (showWeapMsg): the other weapon's line first, then the current one's.
LINES = [("#str_200939", "#str_200940"), ("#str_200940", "#str_200939"), ("#str_200941", "#str_200942"),
         ("#str_200942", "#str_200941")]
EXIT_KEY, LOCKED_KEY = "#str_231229", "#str_231230"


# ------------------------------------------------------------------ geometry

def flatten(data: str, steps: int = 8) -> list:
    """An SVG outline as a polygon (curves divided evenly)."""
    points, current = [], (0.0, 0.0)
    for seg in hud_svg.parse_path(data):
        kind = seg[0]
        if kind in ("M", "L"):
            current = seg[1]
            points.append(current)
        elif kind == "Q":
            (cx, cy), (x, y) = seg[1], seg[2]
            x0, y0 = current
            for i in range(1, steps + 1):
                t = i / steps
                points.append(((1 - t) ** 2 * x0 + 2 * (1 - t) * t * cx + t * t * x,
                               (1 - t) ** 2 * y0 + 2 * (1 - t) * t * cy + t * t * y))
            current = (x, y)
        elif kind == "C":
            (ax, ay), (bx, by), (x, y) = seg[1], seg[2], seg[3]
            x0, y0 = current
            for i in range(1, steps + 1):
                t = i / steps
                s = 1 - t
                points.append((s ** 3 * x0 + 3 * s * s * t * ax + 3 * s * t * t * bx + t ** 3 * x,
                               s ** 3 * y0 + 3 * s * s * t * ay + 3 * s * t * t * by + t ** 3 * y))
            current = (x, y)
    return points


def rect_path(ident: str, fill) -> dict:
    """A rectangle filling its node."""
    def p(fx, fy):
        return [{"fraction": fx}, {"fraction": fy}]
    return {"id": ident, "fill": fill, "commands": [
        {"id": "a", "op": "move", "points": [p(0, 0)]}, {"id": "b", "op": "line", "points": [p(1, 0)]},
        {"id": "c", "op": "line", "points": [p(1, 1)]}, {"id": "d", "op": "line", "points": [p(0, 1)]},
        {"id": "e", "op": "close"}]}


def moving_window(doc: HudDocument, ident: str, left: float, top: float, width: float, height: float, children: list,
                  lead) -> dict:
    """A clip window whose leading edge sits at `lead` u (an expression) while
    its content stays put: the window and its content move by opposite
    transforms, so a draining gauge never lays the document out again."""
    shift = op("*", op("-", lead, left), U)
    # RmlUi clips only content that overflows toward the trailing or bottom side
    # (as fill_window in remastered_hud_status notes), so the content box reaches
    # past the window's trailing edge; the paths inside are absolute.
    content = box(ident + "-content", 0, 0, u(width) + 600, u(height), children, transform=ui.transform())
    window = box(ident, u(left), u(top), u(width), u(height), [content], overflow=ui.keyword("hidden"),
                 transform=ui.transform())
    doc.bind(ident + ".shift", ident, "transform", [shift, 0, 1, 1, 0])
    doc.bind(ident + "-content.shift", ident + "-content", "transform", [op("-", 0, shift), 0, 1, 1, 0])
    return window


# ------------------------------------------------------------------ the gauges

def vehicle_gauge(doc: HudDocument, kind: str, gx: float, colour: str, fill_alpha: float, label_key: str, glyph: str,
                  pct_key: str, warn, cells: bool) -> dict:
    """One gauge at (gx, 420) u (atlas vehicleBar): the black backing, the
    track at 0.20 and the trailing-anchored fill, the hull's cell dividers,
    the label and the notch icon, all #FF3300 at 0.60 while it warns, the
    track pulsing at 2 Hz."""
    w, h = 125, 59
    ident = f"v{kind}"
    plate = backing_nodes(doc, ident, VBACK)
    track = vec(ident + "-track", [svg_path("track", VFILL, fill=ui.solid(rgb(colour, 0.2)))])
    doc.show(track["id"], NOT(warn))
    track_warn = vec(ident + "-track-warn", [svg_path("track", VFILL, fill=ui.solid(rgb(WARN, 0.6)))],
                     {"opacity": num(1), "display": ui.keyword("none")})
    doc.show(track_warn["id"], warn)
    doc.timeline(ident + "-pulse", 500, [tr(track_warn["id"], "opacity", [(0, num(1)), (250, num(0.5)), (500, num(1))])],
                 iterations=0)
    doc.joins.append(play(ident + "-pulse"))
    fill = vec(ident + "-fill", [svg_path("fill", VFILL, fill=ui.solid(rgb(colour, fill_alpha)))])
    doc.show(fill["id"], NOT(warn))
    fill_warn = vec(ident + "-fill-warn", [svg_path("fill", VFILL, fill=ui.solid(rgb(WARN, 0.6)))], {"display": ui.keyword("none")})
    doc.show(fill_warn["id"], warn)
    pct = op("clamp", S(pct_key), 0, 1)
    window = moving_window(doc, ident + "-win", 0, 0, w, h, [fill, fill_warn],
                           op("-", FILL_TRAIL, op("*", FILL_SPAN, pct)))
    layers = plate + [track, track_warn, window]
    if cells:
        outline = flatten(VFILL)
        pieces = []
        for i, x in enumerate(CELLS):
            piece = clip_polygon(outline, [(x - 0.75, 13.8), (x + 0.75, 13.8), (x + 0.75, 44.8), (x - 0.75, 44.8)])
            if len(piece) >= 3:
                pieces.append(pts_path(f"cell{i}", scaled(piece), fill=ui.solid([0, 0, 0, 0.28])))
        layers.append(vec(ident + "-cells", pieces))
    sheared = shear_group(ident + "-plate", 0, 0, u(w), u(h), -0.22, layers)
    text = text_box(ident + "-label", label_key, face="lowpixel", size=10.08, colour=rgb(INK, 0.6), baseline=u(42), x=u(39),
                    width=u(60))
    text["properties"]["display"] = ui.keyword("block")
    doc.show(text["id"], NOT(warn))
    text_warn = text_box(ident + "-label-warn", label_key, face="lowpixel", size=10.08, colour=rgb(WARN, 0.6), baseline=u(42),
                         x=u(39), width=u(60))
    text_warn["properties"]["display"] = ui.keyword("none")
    doc.show(text_warn["id"], warn)
    icon = full(ident + "-icon", hud_svg.icon_nodes(glyph, ident + "-icon-g", u(98), u(38), u(16), tint=rgb(INK, 0.6)))
    doc.show(icon["id"], NOT(warn))
    icon_warn = full(ident + "-icon-warn", hud_svg.icon_nodes(glyph, ident + "-icon-wg", u(98), u(38), u(16),
                                                             tint=rgb(WARN, 0.6)), display=ui.keyword("none"))
    doc.show(icon_warn["id"], warn)
    return box(ident + "-gauge", u(gx), u(420), u(w), u(h), [sheared, text, text_warn, icon, icon_warn])


def weapon_panel(doc: HudDocument, weapons: list = WEAPONS, charged: tuple = (0, 3), counted: tuple = (3,)) -> list:
    """The chamfered panel (stock ammo_backbar, sheared about y 440) with the
    current weapon's traced glyph and the other's at 0.40, both upright, the
    rocket pods' count in Chain and the #FF9E1A charge bar for the cannon and
    the rocket pods. `weapons` maps vehicle_weapon to (current, other) glyphs;
    `charged` and `counted` are the indexes with a charge bar and a count."""
    weapon = S("vehicle_weapon")
    nodes = [vec("vpanel-back", [pts_path("back", scaled(sheared_points(flatten(PANEL), -0.22, 440)), fill=ui.solid([0, 0, 0, 0.5]))]),
             vec("vpanel-back-opaque", [pts_path("back", scaled(sheared_points(flatten(PANEL), -0.22, 440)),
                                                 fill=ui.solid([0, 0, 0, 0.85]))], {"display": ui.keyword("none")})]
    doc.show("vpanel-back", NOT(S("oq4.opaque")))
    doc.show("vpanel-back-opaque", S("oq4.opaque"))
    for index, (current, other) in enumerate(weapons):
        x, y, h, alpha = GLYPHS[current]
        parts = hud_svg.icon_nodes(current, f"vweap{index}-cur", u(x), u(y), u(h), tint=rgb(INK, alpha))
        parts += hud_svg.icon_nodes(other, f"vweap{index}-oth", u(23), u(409), u(17), tint=rgb(INK, 0.4))
        holder = full(f"vweap{index}", parts, display=ui.keyword("none"))
        doc.show(holder["id"], op("==", weapon, index))
        nodes.append(holder)
    # The count: Chain 0.7, trailing edge at x 97, sheared about its own centre.
    count = text_box("vcount-text", "", face="chain", size=50.4, colour=rgb("#D9E7BF"), baseline=u(33), right=u(97), width=u(60))
    doc.bind("vcount-text.text", "vcount-text", "text", op("numberText", op("max", 0, op("floor", S("vehicle_weaponammo")))))
    count_box = shear_group("vcount", 0, u(429), u(110), u(41), -0.22, [count])
    count_box["properties"]["display"] = ui.keyword("none")
    doc.show("vcount", OR(*[op("==", weapon, i) for i in counted]) if counted else False)
    nodes.append(count_box)
    # The charge bar: its track and the fill growing from the leading end.
    track = vec("vcharge-track", [pts_path("track", scaled(rounded_rect(9, 465, 85, 8, 4, -0.22, 440)),
                                           fill=ui.solid(rgb("#B3D06E", 0.3)))])
    bar = vec("vcharge-bar", [rect_path("bar", ui.solid(rgb("#FF9E1A")))])
    charge = op("clamp", S("vehicle_weaponcharge"), 0, 1)
    # Scaled about its leading edge: the box centre sits 37 u in.
    bar_box = box("vcharge-fill", u(15), u(467 - 400), u(74), u(4), [bar], transform=ui.transform())
    doc.bind("vcharge-fill.scale", "vcharge-fill", "transform", [op("*", u(-37), op("-", 1, charge)), 0, charge, 1, 0])
    fill_shear = shear_group("vcharge-shear", 0, u(400), u(640), u(80), -0.22, [bar_box])
    charging = full("vcharge", [track, fill_shear], display=ui.keyword("none"))
    doc.show("vcharge", OR(*[op("==", weapon, i) for i in charged]))
    nodes.append(charging)
    return nodes


# ------------------------------------------------------------------ the weapon lines

def bracket_paths(mirrored: bool) -> list:
    """bracket1 (8x32 texels) on its 12x32 u rect: a 1.5 u upright one texel
    in and 1 u arms reaching 9 u, mirrored for the trailing bracket."""
    sx = 12 / 8
    def x(t):
        return (12 - t * sx) if mirrored else t * sx
    upright = [(x(1), 1), (x(2), 1), (x(2), 31), (x(1), 31)]
    top = [(x(1), 1), (x(7), 1), (x(7), 2), (x(1), 2)]
    bottom = [(x(1), 30), (x(7), 30), (x(7), 31), (x(1), 31)]
    return [pts_path(f"p{i}", scaled(part), fill=ui.solid(rgb(LINE))) for i, part in enumerate((upright, top, bottom))]


def bracket_value(base: tuple, rect: tuple) -> dict:
    """The transform taking a 12x32 u bracket at `base` onto `rect` (x, y, w, h):
    stretched like its texture, about the box centre."""
    bx, by = base
    x, y, w, h = rect
    return transform_value(u(x + w / 2 - (bx + 6)), u(y + h / 2 - (by + 16)), w / 12, h / 32)


def weapon_lines(doc: HudDocument, lines: list = LINES) -> list:
    """On entry (showWeapMsg, which showExitmessage sends): after 2 s the
    other weapon's line wipes open in Marine 0.22 #FF8000 while orange
    brackets slide out around its glyph; it rises 12 u and turns white, the
    current weapon's line wipes open below as the brackets frame the weapon
    panel and fade; each line fades over 500 ms, the last ending at 5.5 s
    (stock anim_weap). The text keeps the weapon of the moment it began."""
    doc.number("vehicle_weapon", -1)
    doc.number("oq4.weapMsg", 0)
    doc.boolean("oq4.weapMsgOn")
    nodes = []
    for line, (x, y) in ((1, (90, 411)), (2, (90, 411))):
        ident = f"vmsg{line}"
        text = shadowed(ident + "-text", "", face="marine", size=15.84, colour=[1, 1, 1, 1], baseline=u(14), x=0, width=u(348))
        del text[1]["properties"]["color"]       # the line's colour, which the timeline turns white
        key = lines[-1][line - 1]
        for index in range(len(lines) - 2, -1, -1):
            key = op("select", op("==", S("oq4.weapMsg"), index), lines[index][line - 1], key)
        for node in text:
            doc.bind(node["id"] + ".text", node["id"], "text", key)
        content = box(ident + "-content", 0, 0, u(348), u(19), text, transform=transform_value(u(348)),
                      color=ui.colour(rgb(LINE)))
        window = box(ident + "-win", u(x), u(y), u(348), u(19), [content], overflow=ui.keyword("hidden"),
                     transform=transform_value(u(-348)))
        nodes.append(full(ident, [window], transform=ui.transform(), opacity=num(1)))
    closed, opened = transform_value(u(-348)), transform_value(0)
    pushed = transform_value(u(348))
    orange, white = ui.colour(rgb(LINE)), ui.colour([1, 1, 1, 1])
    doc.timeline("vmsg-run", 5500, [
        tr("vmsg1-win", "transform", [(0, closed), (2000, closed), (2250, opened)]),
        tr("vmsg1-content", "transform", [(0, pushed), (2000, pushed), (2250, opened)]),
        tr("vmsg1", "transform", [(0, transform_value()), (2500, transform_value()), (2750, transform_value(u(-9), u(-12)))]),
        tr("vmsg1-content", "color", [(0, orange), (2500, orange), (2750, white)]),
        tr("vmsg1", "opacity", [(0, num(1)), (4500, num(1)), (5000, num(0))]),
        tr("vmsg2-win", "transform", [(0, closed), (2600, closed), (2850, opened)]),
        tr("vmsg2-content", "transform", [(0, pushed), (2600, pushed), (2850, opened)]),
        tr("vmsg2-content", "color", [(0, orange), (3000, orange), (3250, white)]),
        tr("vmsg2", "opacity", [(0, num(1)), (5000, num(1)), (5500, num(0))]),
    ], essential=True, complete="weapMsgDone")
    # The brackets: decorative, so reduced motion leaves them out.
    left = box("vbr-l", u(78), u(403), u(12), u(32), [vec("vbr-l-art", bracket_paths(False))], transform=ui.transform())
    right = box("vbr-r", u(80), u(403), u(12), u(32), [vec("vbr-r-art", bracket_paths(True))], transform=ui.transform())
    brackets = full("vbr", [left, right], opacity=num(0))
    doc.timeline("vbr-run", 3250, [
        tr("vbr-l", "transform", [(0, bracket_value((78, 403), (78, 403, 12, 32))), (2000, bracket_value((78, 403), (78, 403, 12, 32))),
                                  (2250, bracket_value((78, 403), (18, 403, 12, 32))), (2600, bracket_value((78, 403), (18, 403, 12, 32))),
                                  (2850, bracket_value((78, 403), (10, 426, 17, 52)))]),
        tr("vbr-r", "transform", [(0, bracket_value((80, 403), (80, 403, 12, 32))), (2000, bracket_value((80, 403), (80, 403, 12, 32))),
                                  (2250, bracket_value((80, 403), (68, 403, 12, 32))), (2600, bracket_value((80, 403), (68, 403, 12, 32))),
                                  (2850, bracket_value((80, 403), (84, 426, 17, 52)))]),
        tr("vbr", "opacity", [(0, num(0)), (2000, num(0)), (2250, num(1)), (3000, num(1)), (3250, num(0))]),
    ])
    group = full("vmsg", nodes + [brackets], display=ui.keyword("none"))
    doc.show("vmsg", S("oq4.weapMsgOn"))
    show = [{"op": "setState", "values": {"oq4.weapMsg": op("clamp", S("vehicle_weapon"), 0, len(lines) - 1), "oq4.weapMsgOn": True}},
            play("vmsg-run"), play("vbr-run")]
    hide = [cancel("vmsg-run"), cancel("vbr-run"), {"op": "setState", "values": {"oq4.weapMsgOn": False}}]
    doc.on("showWeapMsg", *show)
    doc.on("hideWeapMsg", *hide)
    doc.on("weapMsgDone", {"op": "setState", "values": {"oq4.weapMsgOn": False}})
    return [group], show, hide


# ------------------------------------------------------------------ hits and warnings

# add_box2 along a mid-line, every 1/32 of the canvas from the edge (of 247).
HIT_FALLOFF = [247, 189, 135, 90, 53, 23, 5, 0]


def hit_flash(doc: HudDocument) -> dict:
    """vehicleHit: the stock additive box glow (add_box2) in #550000, from the
    edges of the view to 22% of the canvas in, the strips adding at the
    corners as the texture does, gone over 500 ms."""
    stops = [{"at": round(i / (len(HIT_FALLOFF) - 1), 4), "color": ui.colour([0.333, 0, 0, round(v / HIT_FALLOFF[0], 4)])}
             for i, v in enumerate(HIT_FALLOFF)]

    def strip(ident, props, a, b):
        paint = {"type": "linear", "from": a, "to": b, "stops": stops}
        path = rect_path("glow", paint)
        path["blend"] = "additive"
        node = vec(ident, [path], {**props, "opacity": num(1)})
        # A strip held by its trailing or bottom edge drops the full box's
        # leading and top offsets, which would otherwise win.
        for side, opposite in (("right", "left"), ("bottom", "top")):
            if side in props:
                node["properties"].pop(opposite, None)
        return node
    # Gradient ends are in the strip's own dp (only their direction matters).
    deep_x, deep_y = u(0.22 * 640), u(0.22 * 480)
    strips = [
        strip("vhit-l", {**ui.absolute(left=0, top=0, width=deep_x), "height": ui.length(100, "%")}, [0, 0], [deep_x, 0]),
        strip("vhit-r", {**ui.absolute(right=0, top=0, width=deep_x), "height": ui.length(100, "%")}, [deep_x, 0], [0, 0]),
        strip("vhit-t", {**ui.absolute(left=0, top=0, height=deep_y), "width": ui.length(100, "%")}, [0, 0], [0, deep_y]),
        strip("vhit-b", {**ui.absolute(left=0, bottom=0, height=deep_y), "width": ui.length(100, "%")}, [0, deep_y], [0, 0]),
    ]
    group = full("vhit", strips, opacity=num(0))
    # Reduced flashing (13.10) caps it at 0.40.
    flash_cap(doc, [node["id"] for node in strips], 1)
    doc.timeline("vhit-flash", 500, [tr("vhit", "opacity", [(0, num(1)), (500, num(0))])], essential=True)
    doc.on("vehicleHit", play("vhit-flash"))
    return group


# veh_elecwarn (128x128 texels on its 128 u square at the leading edge, y 167),
# traced: a bracket bowing toward the edge with arms reaching inward, the bolt
# inside it, and a glow rising toward the bracket.
ELEC_BRACKET = [(60, 18), (66, 19.5), (76, 21), (85, 23.5), (83.8, 33), (82.6, 42), (81.6, 52), (81.2, 62), (81.6, 72),
                (82.2, 82), (83.2, 92), (84.6, 100), (85.2, 104), (76, 106), (66, 107.5), (60, 108)]
ELEC_BOLT = [(62.5, 43), (60.5, 46), (57, 49), (54, 51.5), (51.5, 53), (59.5, 54), (62.5, 55.5), (57, 60), (52, 63.5),
             (57.5, 66), (55, 68.5), (51, 71.5), (47, 74.5), (43.5, 78), (38, 82.5), (40, 79.5), (42.5, 76.5), (45, 73.5),
             (47.5, 71), (49, 69), (43, 67.5), (40.5, 66), (44, 63.5), (47, 61), (50, 58.5), (52, 56.5), (47, 54.5),
             (48.5, 52.5), (52, 50), (55.5, 47.5), (58.5, 45)]
ELEC_GLOW = [(30, 20), (60, 18), (85, 23.5), (81.2, 62), (85.2, 104), (60, 108), (30, 106)]


def electric_warning(doc: HudDocument) -> list:
    """electricWarningOn/Off: red bracket, bolt and glow at both side edges,
    additive, dimming from 0.70 to 0.30 every second (stock anim_elec) while
    on."""
    red = [1, 0, 0, 1]
    sides = []
    for side, mirrored in (("l", False), ("r", True)):
        def place(points):
            return scaled([((640 - x) if mirrored else x, y + 167) for x, y in points])
        rise = {"type": "linear", "from": [u(640 - 30 if mirrored else 30), u(230)], "to": [u(640 - 82 if mirrored else 82), u(230)],
                "stops": [{"at": 0, "color": ui.colour([1, 0, 0, 0])}, {"at": 1, "color": ui.colour([1, 0, 0, 0.22])}]}
        glow_path = pts_path("glow", place(ELEC_GLOW), fill=rise, blend="additive")
        bracket = pts_path("bracket", place(ELEC_BRACKET), closed=False, stroke=stroke(red, u(2.6), join="round", cap="round"),
                           blend="additive")
        bolt = pts_path("bolt", place(ELEC_BOLT), fill=ui.solid(red), blend="additive")
        sides.append(vec(f"velec-{side}", [glow_path, bracket, bolt]))
    holder_l = full("velec-l-on", [sides[0]], display=ui.keyword("none"), opacity=num(0.7))
    holder_r = full("velec-r-on", [sides[1]], display=ui.keyword("none"), opacity=num(0.7))
    doc.boolean("oq4.elec")
    for holder in (holder_l, holder_r):
        doc.show(holder["id"], S("oq4.elec"))
    # The stock sawtooth snaps back on its last millisecond, where reduced
    # motion ends it: a steady 0.70 (14.10).
    doc.timeline("velec-pulse", 1000, [tr("velec-l-on", "opacity", [(0, num(0.7)), (999, num(0.3)), (1000, num(0.7))]),
                                       tr("velec-r-on", "opacity", [(0, num(0.7)), (999, num(0.3)), (1000, num(0.7))])], iterations=0)
    doc.on("electricWarningOn", {"op": "setState", "values": {"oq4.elec": True}}, play("velec-pulse"))
    doc.on("electricWarningOff", {"op": "setState", "values": {"oq4.elec": False}}, cancel("velec-pulse"))
    return [holder_l, holder_r]


# ------------------------------------------------------------------ the HUD prompt

def lock_icon(ident: str, colour: list) -> dict:
    """The menus' lock (atlas hudPrompt), 9 u wide on the prompt's line."""
    m = (U * 1.2, 0, 0, U * 1.2, u(0.6), u(5.3))
    paths = [svg_path("shackle", "M1.3 -1.9v-1.6a2.2 2.2 0 0 1 4.4 0v1.6", matrix=m,
                      stroke=stroke(colour, u(0.9 * 1.2), join="round", cap="butt")),
             svg_path("body", "M0.6 -2.1H6.4Q7 -2.1 7 -1.5V2.5Q7 3.1 6.4 3.1H0.6Q0 3.1 0 2.5V-1.5Q0 -2.1 0.6 -2.1Z", matrix=m,
                      fill=ui.solid(colour))]
    return ui.group(ident, {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(9)),
                            "height": ui.length(u(10.6)), "margin-right": ui.length(u(4))}, [vec(ident + "-g", paths)])


def prompt_chip(doc: HudDocument, ident: str, key_expr) -> dict:
    """The active control (13.4): a keycap with its key name in dark ProFont
    6.4 u on a white chip at 0.85, 10.6 u tall with a cut lower-leading
    corner, or the controller button's glyph (glyph_chip)."""
    return glyph_chip(doc, ident, key_expr, h=10.6, size=6.4, cut=2.6, pad=(3, 3), gap=4, min_width=10.6)


def hud_prompt(doc: HudDocument, lead: list = ()) -> dict:
    """The HUD prompt (14.8): bottom-trailing, one sheared plate at black 0.50
    never wider than its content. In a vehicle it shows the jump key's cap and
    Exit; while a script locks the vehicle, a lock and Vehicle locked in
    #FF8000 instead. `lead` items (the speeder's boost) come first, 11 u
    apart."""
    doc.string("oq4_key_jump")
    doc.number("vehicle_locked")
    doc.boolean("oq4.vehicle")
    doc.boolean("oq4.locked")
    locked = OR(op("!=", S("vehicle_locked"), 0), S("oq4.locked"))
    exit_item = flex_row("vpr-exit", [prompt_chip(doc, "vpr", S("oq4_key_jump")), label("vpr-exit-word", EXIT_KEY, 10.6, [1, 1, 1, 0.92], line=16)])
    doc.bind("vpr-exit.display", "vpr-exit", "display", sel(locked, "none", "flex"))
    lock_item = flex_row("vpr-lock", [lock_icon("vpr-lock-icon", rgb(LINE)), label("vpr-lock-word", LOCKED_KEY, 10.6, rgb(LINE), line=16)],
                         display=ui.keyword("none"))
    doc.bind("vpr-lock.display", "vpr-lock", "display", sel(locked, "flex", "none"))
    row = flex_row("vpr-row", [plate_vec("vpr-plate", 16, 0.5), *lead, exit_item, lock_item],
                   **{"height": ui.length(u(16)), "padding-left": ui.length(u(5.5)), "padding-right": ui.length(u(5.5))})
    group = ui.group("vpr", {**ui.absolute(right=u(640 - 629), bottom=u(480 - 476)), "display": ui.keyword("none")}, [row])
    doc.bind("vpr.display", "vpr", "display", sel(S("oq4.vehicle"), "flex", "none"))
    return group


# ------------------------------------------------------------------ the document

def vehicle_document() -> dict:
    doc = HudDocument("openq4.hud.remastered.vehicle")
    layout_state(doc)
    for key in ("vehicle_health", "vehicle_shield", "vehicle_weaponcharge", "vehicle_weaponammo"):
        doc.number(key, 1 if key in ("vehicle_health", "vehicle_shield") else 0)
    hull_warn = op("<=", S("vehicle_health"), 0.25)
    shield_warn = NOT(op(">", S("vehicle_shield"), 0))
    gauges = [vehicle_gauge(doc, "hull", 71, "#B3D06E", 0.35, "#str_200911", "health", "vehicle_health", hull_warn, True),
              vehicle_gauge(doc, "shield", 164, "#B2CF6D", 0.349, "#str_200912", "armor", "vehicle_shield", shield_warn, False)]
    panel = weapon_panel(doc)
    lines, show_lines, hide_lines = weapon_lines(doc)
    # The status bar spans 11-288 u in the stock; Remastered centres it on 320.
    bar = box("vbar", u(170.5), 0, u(640), u(480), panel + gauges + lines)
    radio = transmission(doc)
    boss = boss_bar(doc, offset=20.0)
    exit_notice, saved, missile_text = notices(doc)
    missile = missile_chevron(doc)
    prompt = hud_prompt(doc)
    hit = hit_flash(doc)
    elec_l, elec_r = electric_warning(doc)
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}}, *show_lines)
    # A rebuilt peer rejoins a crewed vehicle without replaying the entry lines.
    doc.on("oq4_joinVehicle", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}}, *hide_lines)
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Minimal (ui_hudLayout 2) keeps the status bar, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        beyond_minimal(doc, "hit-full", [hit]),
        anchor(doc, "cl", [beyond_minimal(doc, "cl-full", [elec_l])]),
        anchor(doc, "cr", [beyond_minimal(doc, "cr-full", [elec_r])]),
        anchor(doc, "bc", [bar], scale="hud.centerScale"),
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio])]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [boss, missile_text, exit_notice]), saved]),
        anchor(doc, "cc", [beyond_minimal(doc, "cc-full", [missile])]),
        anchor(doc, "br", [prompt]),
    ])
    joins = join_events(doc, [("exit_msg::visible", "oq4_joinVehicle"), ("lock_msg::visible", "showLockmessage"),
                              ("p_radio::visible", "radioChatterUp"), ("missile_indicator::visible", "missileThreatUp"),
                              ("left_elecwarn::visible", "electricWarningOn"), ("BossBar::visible", "showBossBar"),
                              ("p_boss_shield_bar_left::visible", "showBossShieldBar")])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.8, 14.12",
                                                      "stock": "guis/vehicles/hud.gui", "joinEvents": joins}}})


# ------------------------------------------------------------ the air-defence cannon

def load_bar(ident: str, mirror: bool) -> list:
    """A crescent load bar (the atlas's air-defence bow) inside the lens's
    trailing edge, mirrored for the leading one: an #E6780B edge over #FF6C0B
    at 0.40. The stock binds the bars to nothing, so they stay full."""
    def p(x, y):
        return [round(U * (640 - x if mirror else x), 4), round(U * y, 4)]
    cmds = [{"id": "a", "op": "move", "points": [p(546, 145)]},
            {"id": "b", "op": "quadratic", "points": [p(584.5, 240.5), p(541, 334)]},
            {"id": "c", "op": "line", "points": [p(537, 330)]},
            {"id": "d", "op": "quadratic", "points": [p(566.5, 241), p(542, 152)]},
            {"id": "e", "op": "close"}]
    return [{"id": ident + "-fill", "commands": cmds, "fill": ui.solid(ui.rgb("#FF6C0B", 0.4))},
            {"id": ident + "-edge", "commands": cmds, "fill": {"type": "none"},
             "stroke": {"paint": ui.solid(ui.rgb("#E6780B")), "widthDp": round(U * 1.6, 4), "minimumPixels": 0, "join": "round"}}]


def airdef_lens() -> list:
    """The cannon's optic: the retained nailgun scope's amber lens held still
    (Gaussian focus beyond r 255, the glow at its resting tint, the notched
    ring in black 0.34 without the yaw, the hooks, bands and hairline, haze
    beyond), the two load bars and the crosshair_nailgun reticle, 50 u in
    #E6780B. The optic fits the view's height or width like every scope, and
    the haze fills the exposed bands."""
    stops = [(144, 0), (168, .07), (192, .255), (216, .57), (240, .97), (252, 1), (264, .69), (276, 0)]
    # The glow's 0.44 lives in its stops: a nested opacity layer would keep the
    # optic's layer below from caching.
    glow = scopes.art("ad-glow", scopes.radial("ad-glow", [(r, round(a * .44, 4)) for r, a in stops], "#533200", "additive"))
    ring2 = scopes.art("ad-ring2", [scopes.measured("nail_ring2", [0, 0, 0, .34])])
    haze = scopes.art("ad-haze", scopes.radial("ad-haze", [(291.5, .08), (310, .24), (332, .4), (356, .5)], "#828282"))
    marks = scopes.art("ad-marks", [scopes.measured("nail_hook", [0, 0, 0, .5]),
                                    scopes.ring("band1", 263, 15, ui.rgb("#020202", .5)),
                                    scopes.ring("edge", 272, 2.4, [0, 0, 0, 1]),
                                    scopes.ring("band2", 282.5, 18, ui.rgb("#020202", .71)),
                                    scopes.ring("hairline", 218.1, 2.2, ui.rgb("#3C2500"), "additive")])
    bars = scopes.art("ad-load", load_bar("lead", True) + load_bar("trail", False))
    reticle = hud_svg.icon_nodes("crosshair_nailgun", "ad-reticle", U * 295, U * 215, U * 50, tint=ui.rgb("#E6780B"),
                                 family="crosshairs")
    # The optic never moves or changes, but its rings, gradients and haze are
    # about 220k antialiased vertices at 720p, resubmitted every frame. Under
    # one isolating group (opacity just under 1) the runtime caches it as a
    # layer and composites one quad a frame instead.
    # The group spans the view: a layer keeps only what lies inside its group.
    static = ui.group("ad-static", {**ui.FULL, "display": ui.keyword("block"), "opacity": ui.number(0.999)},
                      [scopes.surround("ad-outer-haze", scopes.circle(356), ui.rgb("#828282", .5)),
                       scopes.center("ad-optic", [glow, ring2, haze, marks, bars, *reticle])])
    return [scopes.lens_focus(255), static]


def airdef_document() -> dict:
    """The air-defence cannon (guis/vehicles/airdef_cannon.gui, 14.12). The
    stock draws a static amber scope overlay with two load bars and its own
    reticle and centres its exit line. Remastered keeps the optic, adds the
    transmission display the cannon's HUD lacks at the top-trailing corner,
    sets the HUD prompt (Exit, or a lock and Vehicle locked) bottom-trailing in
    place of the exit line and keeps the save notice."""
    doc = HudDocument("openq4.hud.remastered.airdef")
    layout_state(doc)
    lens = airdef_lens()
    radio = transmission(doc, strogg=False, emblem="radio_rhino")
    exit_notice, saved, missile_text = notices(doc)
    prompt = hud_prompt(doc)
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}})
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Minimal (ui_hudLayout 2) keeps the optic, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        *lens,
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio])]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [exit_notice, missile_text]), saved]),
        anchor(doc, "br", [prompt]),
    ])
    joins = join_events(doc, [("exit_msg::visible", "showExitmessage"), ("lock_msg::visible", "showLockmessage")])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.12",
                                                      "stock": "guis/vehicles/airdef_cannon.gui", "joinEvents": joins}}})

# ------------------------------------------------------------------ ride gauges

# The flatbed's and the tram's silhouettes, traced from flatbedbar/flatbededge
# and trambar/tramedge (256x128 texels each; edges are an outer and an inner
# outline of one ring).
FLATBED = [(106, 109.5), (89.5, 110), (87.5, 108), (72.5, 108), (70.5, 110), (56.5, 110), (48.5, 102), (44.5, 102), (34.5, 109), (32.5, 109), (29, 105.5), (36, 99.5), (36, 82.5), (27.5, 82), (24.5, 79), (10.5, 79), (5, 73.5), (5, 36.5), (9.5, 32), (17, 32.5), (17.5, 63), (151.5, 63), (152, 35.5), (149, 32.5), (149, 16.5), (152.5, 13), (178.5, 13), (191.5, 26), (210.5, 26), (234.5, 51), (248, 62.5), (248, 68.5), (251, 71.5), (251, 86.5), (238, 87.5), (238, 97.5), (246, 104.5), (242.5, 108), (240.5, 108), (230.5, 101), (192.5, 101), (184.5, 107), (180.5, 108), (178, 104.5), (184, 98.5), (183.5, 93), (179.5, 97), (154.5, 97), (151.5, 94), (126.5, 94), (126, 98.5), (133, 105.5), (130.5, 109), (126.5, 108), (118.5, 102), (113.5, 102)]
FLATBED_EDGE = ([(107, 110.5), (89.5, 111), (87.5, 109), (72.5, 109), (70.5, 111), (55.5, 111), (47.5, 103), (44.5, 103), (36.5, 109), (31.5, 110), (28, 105.5), (35, 98.5), (35, 83.5), (27.5, 83), (24.5, 80), (9.5, 80), (4, 74.5), (4, 35.5), (8.5, 31), (18, 31.5), (18.5, 62), (150.5, 62), (151, 35.5), (148, 33.5), (148, 16.5), (152.5, 12), (178.5, 12), (191.5, 25), (211.5, 25), (232.5, 48), (249, 61.5), (249, 67.5), (252, 71.5), (252, 87.5), (239, 88.5), (239, 96.5), (247, 104.5), (243.5, 109), (240.5, 109), (229.5, 102), (193.5, 102), (183.5, 109), (180.5, 109), (177, 105.5), (177, 103.5), (183, 98.5), (182.5, 95), (179.5, 98), (154.5, 98), (151.5, 95), (127.5, 95), (127, 98.5), (134, 104.5), (134, 106.5), (130.5, 110), (124.5, 108), (117.5, 103), (114.5, 103)],
                [(105.5, 109), (113.5, 101), (118.5, 101), (129.5, 108), (132, 105.5), (125, 99.5), (125.5, 93), (152.5, 93), (155.5, 96), (178.5, 96), (182.5, 92), (185, 92.5), (185, 99.5), (179, 104.5), (181.5, 107), (192.5, 100), (230.5, 100), (240.5, 107), (242.5, 107), (244, 104.5), (237, 97.5), (237, 86.5), (250, 85.5), (250, 72.5), (247, 68.5), (247, 62.5), (230.5, 49), (210.5, 27), (190.5, 27), (177.5, 14), (153.5, 14), (150, 17.5), (150, 32.5), (153, 34.5), (152.5, 64), (16.5, 64), (16, 33.5), (9.5, 33), (6, 36.5), (6, 73.5), (10.5, 78), (24.5, 78), (28.5, 81), (37, 81.5), (37, 99.5), (31, 105.5), (32.5, 108), (34.5, 108), (44.5, 101), (48.5, 101), (56.5, 109), (69.5, 109), (72.5, 107), (87.5, 107), (90.5, 109)])
TRAM = [(44, 114.5), (42.5, 115), (41, 113.5), (13, 71.5), (16.5, 68), (18.5, 68), (24.5, 74), (25, 70.5), (44, 51.5), (42, 45.5), (36.5, 43), (33, 37.5), (34, 30.5), (24, 24.5), (48.5, 12), (123.5, 17), (136.5, 23), (154, 23.5), (154, 27.5), (151.5, 29), (142.5, 29), (103.5, 34), (88.5, 34), (84.5, 39), (76.5, 39), (65, 56.5), (65, 58.5), (68.5, 62), (175.5, 62), (184.5, 58), (241.5, 58), (243, 61.5), (243, 79.5), (240, 87.5), (238.5, 90), (235.5, 90), (234, 94.5), (226.5, 101), (200.5, 101), (179.5, 98), (130.5, 98), (117.5, 104), (51.5, 104), (44.5, 101), (37.5, 94), (37, 95.5), (46, 111.5), (46, 113.5)]
TRAM_EDGE = ([(44, 115.5), (42.5, 116), (40, 113.5), (12, 71.5), (13.5, 69), (18.5, 67), (23.5, 72), (43, 51.5), (41, 45.5), (36.5, 44), (32, 37.5), (33, 30.5), (23.5, 26), (23, 24.5), (48.5, 11), (123.5, 16), (136.5, 22), (155, 22.5), (155, 28.5), (151.5, 30), (142.5, 30), (103.5, 35), (88.5, 35), (85.5, 40), (76.5, 40), (66, 57.5), (68.5, 61), (175.5, 61), (184.5, 57), (241.5, 57), (244, 61.5), (244, 79.5), (241, 87.5), (226.5, 102), (200.5, 102), (179.5, 99), (130.5, 99), (117.5, 105), (51.5, 105), (41.5, 100), (41, 101.5), (47, 111.5), (47, 113.5)],
             [(43.5, 114), (45, 111.5), (35, 93.5), (35.5, 92), (44.5, 100), (51.5, 103), (117.5, 103), (130.5, 97), (179.5, 97), (200.5, 100), (225.5, 100), (233, 94.5), (235.5, 89), (239, 87.5), (242, 79.5), (242, 61.5), (240.5, 59), (184.5, 59), (175.5, 63), (67.5, 63), (64, 59.5), (64, 56.5), (76.5, 38), (84.5, 38), (86, 34.5), (88.5, 33), (104.5, 33), (142.5, 28), (151.5, 28), (153, 24.5), (136.5, 24), (123.5, 18), (48.5, 13), (26, 24.5), (35, 30.5), (34, 37.5), (38.5, 43), (40.5, 43), (43, 45.5), (45, 51.5), (26, 70.5), (26, 73.5), (24.5, 75), (18.5, 69), (14, 71.5), (42, 113.5)])


def ride_gauge(doc: HudDocument, *, strogg: bool) -> dict:
    """The flatbed (Marine, enterFlatbed) or the tram (Strogg, enterTram)
    replaces the three gauges with one silhouette gauge (14.12): the gauge's
    backing, the vehicle's silhouette filling from the trailing end with its
    health, and its outline at 0.40. Remastered centres it like every status
    bar. The flatbed shears with the Marine gauges; the tram's silhouette is
    the texture mirrored, upright on the Strogg backing."""
    if strogg:
        w, h, gx, gy, k, back = 129, 67, 320 - 129 / 2, 419, 0.22, ST_BACK
        colour, fill_alpha, key = "#FF9000", 0.2, "tram_healthpct"
        rect, shape, edge = (9, 10, 115, 45), TRAM, TRAM_EDGE
        lead = lambda pct: op("-", 119, op("*", 105, pct))
        start, stop = "enterTram", "leaveTram"
    else:
        w, h, gx, gy, k, back = 125, 59, 320 - 125 / 2, 420, -0.22, BACK
        colour, fill_alpha, key = "#B3D06E", 0.35, "vehicle_health"
        rect, shape, edge = (4, 10, 118, 40), FLATBED, FLATBED_EDGE
        lead = lambda pct: op("-", 120, op("*", 115, pct))
        start, stop = "enterFlatbed", "leaveFlatbed"
    doc.number(key, 1)
    rx, ry, rw, rh = rect

    def place(loop):
        if strogg:   # matscalex -1
            return [(u(rx + rw - x * rw / 256), u(ry + y * rh / 128)) for x, y in loop]
        return [(u(rx + x * rw / 256), u(ry + y * rh / 128)) for x, y in loop]
    fill = vec("ride-fill", [pts_path("fill", place(shape), fill=ui.solid(rgb(colour, fill_alpha)))])
    window = moving_window(doc, "ride-win", 0, 0, w, h, [fill], lead(op("clamp", S(key), 0, 1)))
    outline = {"id": "edge", "fillRule": "evenodd", "fill": ui.solid(rgb(colour, 0.4)),
               "commands": [cmd for index, loop in enumerate(edge) for cmd in loop_commands(f"l{index}", place(loop))]}
    plate = backing_nodes(doc, "ride", back)
    if strogg:
        layers = [shear_group("ride-plate", 0, 0, u(w), u(h), k, plate), window, vec("ride-edge", [outline])]
    else:
        layers = [shear_group("ride-plate", 0, 0, u(w), u(h), k, plate + [window, vec("ride-edge", [outline])])]
    group = box("ride", u(gx), u(gy), u(w), u(h), layers, display=ui.keyword("none"))
    doc.show("ride", S("oq4.ride"))
    doc.on(start, {"op": "setState", "values": {"oq4.ride": True}})
    doc.on(stop, {"op": "setState", "values": {"oq4.ride": False}})
    return group


def loop_commands(prefix: str, points: list) -> list:
    commands = [{"id": f"{prefix}m", "op": "move", "points": [[round(points[0][0], 4), round(points[0][1], 4)]]}]
    commands += [{"id": f"{prefix}{i}", "op": "line", "points": [[round(x, 4), round(y, 4)]]} for i, (x, y) in enumerate(points[1:])]
    commands.append({"id": f"{prefix}z", "op": "close"})
    return commands
