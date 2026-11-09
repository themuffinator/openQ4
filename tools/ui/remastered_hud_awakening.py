"""The Awakening's Remastered vehicle displays (ui-visual-design.md 14.12).

The space cannon (guis/vehicles/airassault/turretplayerhud.gui), the m03
dropship guns: the on-foot HUD's radio, boss bar and notices without its
gauges, strip and powerups, and the ship-status panel top-trailing directly
below the transmission display. The same panel joins the expansion's on-foot
HUD, which receives the script's markers, damage and warnings but cannot
show them. The MCC cannon reports the MCC's hull, the speeder bike keeps
the vehicle status bar under the m07 race display, and the GEV's status
cluster is drawn from scratch, since its art never shipped. Constructions
are the design atlas's (awakening sheet), measured from the expansion's
GUIs; the stock coordinates are the 640x480 canvas.
"""
from __future__ import annotations

import math

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (AND, BOUND_TEXT, NOT, OR, S, HudDocument, anchor, beyond_minimal, box, face_metrics, gameplay_root,
                                 join_events,
                                 layout_state, num, op, play, pts_path, rgb, rounded_rect, sel, shear_group, stroke, svg_path,
                                 text_box, tr, transform_value, u, vec, when)

# The dropship from above, nose up (hud_ship_01), in the panel's 128 u box.
SHIP = [(62.7, 17.0), (58.6, 20.5), (56.9, 28.0), (51.0, 33.5), (47.5, 38.5), (47.5, 40.0), (34.0, 49.5), (34.6, 61.5),
        (35.8, 61.5), (37.0, 63.5), (48.1, 65.5), (51.0, 69.5), (51.0, 76.5), (52.2, 78.0), (55.1, 79.0), (57.5, 94.0),
        (52.2, 102.0), (50.4, 103.5), (45.2, 105.0), (44.6, 109.0), (61.0, 109.0), (62.2, 110.5), (66.3, 110.5),
        (67.4, 109.0), (74.5, 108.5), (83.8, 109.0), (83.2, 105.0), (78.0, 103.5), (70.9, 94.5), (73.3, 79.0), (76.2, 78.0),
        (77.4, 76.5), (77.4, 69.0), (79.1, 66.5), (85.6, 64.0), (90.9, 63.5), (94.4, 61.0), (94.4, 49.5), (80.9, 40.0),
        (80.9, 38.5), (77.4, 33.5), (71.5, 28.0), (70.9, 23.5), (68.6, 19.0), (65.7, 17.0)]
# The damaged sections, lit by ship_health_01..05 (80, 60, 40, 20 and 10% of the
# dropship's health): left wing, right wing, nose, tail, mid body.
DAMAGE = [
    [(48.7, 41.0), (47.5, 41.0), (42.2, 45.0), (38.7, 46.5), (34.6, 50.0), (34.6, 60.5), (36.4, 61.0), (38.1, 63.0),
     (43.4, 63.5), (45.8, 65.0), (48.7, 65.0)],
    [(79.7, 42.0), (79.7, 65.0), (82.7, 65.0), (85.0, 63.5), (90.3, 63.0), (92.0, 61.0), (93.8, 60.5), (93.8, 50.0),
     (83.2, 42.5)],
    [(48.1, 39.0), (48.1, 40.0), (80.3, 40.0), (76.8, 34.0), (70.9, 28.5), (70.9, 26.0), (68.0, 19.5), (65.1, 17.5),
     (63.3, 17.5), (60.4, 19.5), (58.1, 24.0), (57.5, 28.5), (51.6, 34.0), (49.9, 37.5)],
    [(57.4, 91.5), (70.9, 91.5), (70.9, 94.5), (78.0, 103.5), (83.2, 105.0), (83.2, 110.5), (45.2, 110.5), (45.2, 105.0),
     (50.4, 103.5), (52.2, 102.0), (57.5, 94.0)],
    [(50.4, 67.0), (51.6, 68.5), (51.6, 76.0), (55.7, 79.0), (72.7, 79.0), (76.8, 76.0), (76.8, 68.5), (78.0, 67.0)],
]
# ship_target_01..05: the objective markers' centres (the stock 16 u rects).
MARKERS = [(56, 83), (64, 79), (55, 66), (54, 51), (65, 37)]
# warning_<dir>: the darts' centres (the stock 24 u rects) and their headings.
WARNINGS = [("N", 65, 9, 0), ("NE", 107, 25, 45), ("E", 114, 63, 90), ("SE", 105, 103, 135), ("S", 64, 122, 180),
            ("SW", 23, 104, 225), ("W", 13, 62, 270), ("NW", 21, 23, 315)]
PANEL_X, PANEL_Y = 512, 32
MARKER_TURN_MS = round(1000 / 0.35)        # 0.35 turn a second
DART_MS = 3000                             # a dart fades over 3 s, pulsing at 2 Hz


def circle_points(cx: float, cy: float, r: float, steps: int = 64) -> list:
    return [(cx + r * math.cos(2 * math.pi * k / steps), cy + r * math.sin(2 * math.pi * k / steps)) for k in range(steps)]


def arc_points(cx: float, cy: float, r: float, a0: float, a1: float, step: float = 4.0) -> list:
    """An arc in the atlas's convention: degrees clockwise from east (y down)."""
    count = max(2, int(math.ceil(abs(a1 - a0) / step)) + 1)
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * k / (count - 1))),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * k / (count - 1)))) for k in range(count)]


def scaled(points: list, dx: float = 0, dy: float = 0) -> list:
    return [(u(x + dx), u(y + dy)) for x, y in points]


def ring_path(ident: str, cx: float, cy: float, inner: float, outer: float, fill) -> dict:
    """An annulus as one even-odd path."""
    def contour(r, prefix):
        pts = circle_points(cx, cy, r)
        cmds = [{"id": f"{prefix}0", "op": "move", "points": [[u(pts[0][0]), u(pts[0][1])]]}]
        cmds += [{"id": f"{prefix}{k}", "op": "line", "points": [[u(x), u(y)]]} for k, (x, y) in enumerate(pts[1:], 1)]
        cmds.append({"id": f"{prefix}z", "op": "close"})
        return cmds
    return {"id": ident, "commands": contour(outer, "o") + contour(inner, "i"), "fillRule": "evenodd", "fill": fill}


def radial_paint(cx: float, cy: float, r: float, stops: list) -> dict:
    """A radial paint in node-local dp: stops are (radius u, rgba)."""
    return {"type": "radial", "from": [u(cx), u(cy)], "to": [u(cx + r), u(cy)],
            "stops": [{"at": round(min(1.0, rad / r), 5), "color": ui.colour(colour)} for rad, colour in stops]}


def arc_stroke(ident: str, cx: float, cy: float, r: float, a0: float, a1: float, colour: list, width: float, blend=None) -> dict:
    return pts_path(ident, scaled(arc_points(cx, cy, r, a0, a1)), stroke=stroke(colour, u(width)), closed=False, blend=blend)


def rotate_about(points: list, cx: float, cy: float, degrees: float) -> list:
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in points]


def ship_panel(doc: HudDocument, *, always: bool, cone: bool) -> dict:
    """The ship-status panel (14.12): an olive disc 58.75 u in radius, #1F2A13
    at 0.85 with a #4D682F to #64883D rim, holding the dropship from above in
    #475736. Its five sections turn #A13C2B as the script reports damage (and
    clear on the _off events). Gold markers, broken rings 16 u across, spin at
    0.35 turn a second over the place that needs the player; eight red darts
    on the rim point in from attackers, pulsing at 2 Hz while they fade over
    3 s. On the gun, an additive olive cone 52 degrees wide shows where it
    looks relative to the ship's nose. On foot the panel appears once the
    script sends it something."""
    doc.boolean("oq4.ship")
    doc.number("playerYaw")
    doc.number("oq4_shipYaw")
    olive = rgb("#585E29")
    layers = [
        vec("ship-disc", [pts_path("disc", scaled(circle_points(64, 64, 58.75)), fill=ui.solid(rgb("#1F2A13", 0.85)))]),
        vec("ship-rim", [ring_path("rim", 64, 64, 56.45, 58.75,
                                   radial_paint(64, 64, 58.75, [(0, rgb("#4D682F")), (56.4, rgb("#4D682F")), (58.75, rgb("#64883D"))]))]),
        vec("ship-shade", [pts_path("shade", scaled(circle_points(64, 64, 56)),
                                    fill=radial_paint(64, 64, 56, [(0, [0, 0, 0, 0.5]), (28, [0, 0, 0, 0.5]), (40, [0, 0, 0, 0.25]),
                                                                   (56, [0, 0, 0, 0])]))]),
    ]
    if cone:
        reach, half = 33.7, math.radians(26)
        left = (60 - reach * math.sin(half), 60 - reach * math.cos(half))
        right = (60 + reach * math.sin(half), 60 - reach * math.cos(half))
        wedge = [(60, 60), left] + arc_points(60, 60, reach, -90 - 26, -90 + 26)[1:-1] + [right]
        fill = {"type": "linear", "from": [u(60), u(60)], "to": [u(60), u(60 - reach)],
                "stops": [{"at": 0, "color": ui.colour([*olive[:3], 0.7])}, {"at": 1, "color": ui.colour([*olive[:3], 0.14])}]}
        cone_paths = [pts_path("wedge", scaled(wedge), fill=fill, blend="additive"),
                      pts_path("edge-l", scaled([(60, 60), left]), stroke=stroke([*olive[:3], 0.95], u(1.3)), closed=False, blend="additive"),
                      pts_path("edge-r", scaled([(60, 60), right]), stroke=stroke([*olive[:3], 0.95], u(1.3)), closed=False, blend="additive"),
                      pts_path("hub", scaled(circle_points(60, 60, 2.35, 24)), stroke=stroke([*olive[:3], 0.55], u(1.9)), blend="additive")]
        turning = box("ship-cone", u(4), u(4), u(120), u(120), [vec("ship-cone-v", cone_paths)], transform=ui.transform())
        # The gun's heading relative to the ship's nose, counter-clockwise for a left turn.
        doc.bind("ship-cone.turn", "ship-cone", "transform",
                 [0, 0, 1, 1, op("-", 0, op("-", S("playerYaw"), S("oq4_shipYaw")))])
        layers.append(turning)
    layers.append(vec("ship-hull", [pts_path("hull", scaled(SHIP), fill=ui.solid(rgb("#475736")),
                                              stroke=stroke(rgb("#63883C"), u(1.1), join="round"))]))
    for index, section in enumerate(DAMAGE):
        key = f"oq4.ship.damage{index + 1}"
        doc.boolean(key)
        ident = f"ship-damage{index + 1}"
        layers.append(vec(ident, [pts_path("d", scaled(section), fill=ui.solid(rgb("#A13C2B")),
                                           stroke=stroke(rgb("#76351D"), u(0.9), join="round"))], {"display": ui.keyword("none")}))
        doc.show(ident, S(key))
        number = f"{index + 1:02d}"
        doc.on(f"ship_health_{number}_on", {"op": "setState", "values": {key: True, "oq4.ship": True}})
        doc.on(f"ship_health_{number}_off", {"op": "setState", "values": {key: False, "oq4.ship": True}})
    spin_tracks = []
    for index, (mx, my) in enumerate(MARKERS):
        key = f"oq4.ship.target{index + 1}"
        doc.boolean(key)
        ident = f"ship-target{index + 1}"
        gold = rgb("#FFD800")
        paths = [pts_path("core", scaled(circle_points(8, 8, 3.75, 24)), fill=ui.solid(rgb("#695900")), blend="additive")]
        for k, a in enumerate((145, 325)):
            paths.append(arc_stroke(f"tick{k}", 8, 8, 3.3, 360 - a - 10, 360 - a + 10, gold, 0.9, "additive"))
        for k, (a0, a1) in enumerate(((20, 95), (200, 275), (110, 125), (140, 155), (170, 185), (290, 305), (320, 335), (350, 365))):
            paths.append(arc_stroke(f"ring{k}", 8, 8, 5.875, 360 - a1, 360 - a0, gold, 1.25, "additive"))
        layers.append(box(ident, u(mx - 8), u(my - 8), u(16), u(16), [vec(ident + "-v", paths)],
                          transform=ui.transform(), **{"display": ui.keyword("none")}))
        doc.show(ident, S(key))
        spin_tracks.append(tr(ident, "transform", [(0, transform_value(0, 0, 1, 1, 0)), (MARKER_TURN_MS, transform_value(0, 0, 1, 1, 360))]))
        number = f"{index + 1:02d}"
        doc.on(f"ship_target_{number}_on", {"op": "setState", "values": {key: True, "oq4.ship": True}}, play("ship-spin"))
        doc.on(f"ship_target_{number}_off", {"op": "setState", "values": {key: False}})
    doc.timeline("ship-spin", MARKER_TURN_MS, spin_tracks, iterations=0)
    for name, cx, cy, heading in WARNINGS:
        ident = f"ship-warn-{name}"
        # The dart points along +y before turning to its heading; its faint
        # arcs turn with it.
        dart = [(-4.1, -5.25), (0, 7.1), (3.75, -4.9), (1.5, -1.9), (-1.5, -1.9), (-3.75, -4.1)]
        paths = [arc_stroke(f"arc{k}", 12, 12, 6.6, a0 + heading, a1 + heading, rgb("#760000", 0.5), 2.6, "additive")
                 for k, (a0, a1) in enumerate(((200, 340), (20, 160)))]
        paths.append(pts_path("dart", scaled(rotate_about([(12 + x, 12 + y) for x, y in dart], 12, 12, heading)),
                              fill=ui.solid(rgb("#D50000")), blend="additive"))
        layers.append(box(ident, u(cx - 12), u(cy - 12), u(24), u(24), [vec(ident + "-v", paths)], opacity=num(0)))
        # 2 Hz pulse inside a linear fade from full to nothing over 3 s.
        keys = []
        for step in range(0, DART_MS // 250 + 1):
            t = step * 250
            envelope = 1 - t / DART_MS
            keys.append((t, num(envelope * (1 if step % 2 == 0 else 0.4))))
        doc.timeline(ident + "-fade", DART_MS, [tr(ident, "opacity", keys)], essential=True)
        doc.on(f"warning_{name}", {"op": "setState", "values": {"oq4.ship": True}}, play(ident + "-fade"))
    panel = box("ship", u(PANEL_X), u(PANEL_Y), u(128), u(128), layers, **{"display": ui.keyword("block" if always else "none")})
    if not always:
        doc.show("ship", S("oq4.ship"))
        doc.on("aa_showShipStatusOn", {"op": "setState", "values": {"oq4.ship": True}})
        doc.on("aa_showShipStatusOff", {"op": "setState", "values": {"oq4.ship": False}})
    # A joining peer finds the markers spinning.
    doc.joins.append(play("ship-spin"))
    return panel


def center_box(ident: str, children: list) -> dict:
    """The unscaled 4:3 canvas at the view centre (world-projected brackets)."""
    return ui.group(ident, {"position": ui.keyword("absolute"), "left": ui.length(50, "%"), "top": ui.length(50, "%"),
                            "margin-left": ui.length(-480), "margin-top": ui.length(-360), "width": ui.length(960),
                            "height": ui.length(720), "display": ui.keyword("block")}, children)


def turret_document() -> dict:
    """The space cannon (turretplayerhud.gui, 14.12): the on-foot HUD's radio,
    boss bar and notices without its gauges, strip, pickups or powerups, the
    damage direction, brackets and missile warning, the ship-status panel
    directly below the transmission display, and the HUD prompt's Exit,
    which the turret HUD never showed. The HUD exists only while the gun is
    crewed, so a joining peer shows the prompt at once."""
    from remastered_hud_groups import boss_bar, hit_indicator, interactive_brackets, missile_chevron, notices, transmission
    from remastered_hud_status import full
    from remastered_hud_vehicle import hud_prompt
    doc = HudDocument("openq4.hud.remastered.turret")
    layout_state(doc)
    radio = transmission(doc, strogg=False, emblem="squad_falcon")
    hit = hit_indicator(doc)
    brackets = interactive_brackets(doc)
    boss = boss_bar(doc)
    exit_notice, saved, missile_text = notices(doc)
    missile = missile_chevron(doc)
    panel = ship_panel(doc, always=True, cone=True)
    prompt = hud_prompt(doc)
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}})
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    doc.joins.append({"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # The stock windows that hold an event's lasting effect.
    pairs = [("p_radio::visible", "radioChatterUp"), ("BossBar::visible", "showBossBar"),
             ("p_boss_shield_bar_left::visible", "showBossShieldBar"), ("brackets::visible", "showBrackets"),
             ("missile_indicator::visible", "missileThreatUp")]
    pairs += [(f"ship_core_{i + 2:02d}::visible", f"ship_health_{i + 1:02d}_on") for i in range(5)]
    pairs += [(f"ship_target_{i + 1:02d}::visible", f"ship_target_{i + 1:02d}_on") for i in range(5)]
    joins = join_events(doc, pairs)
    # Minimal (ui_hudLayout 2) keeps the ship panel, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        center_box("hud-world", [brackets]),
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio]), panel]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [boss, missile_text, exit_notice]), saved]),
        anchor(doc, "cc", [beyond_minimal(doc, "cc-full", [hit, missile])]),
        anchor(doc, "br", [prompt]),
    ])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.12",
                                                      "stock": "guis/vehicles/airassault/turretplayerhud.gui",
                                                      "joinEvents": joins}}})


# ------------------------------------------------------------ the MCC cannon

# The stock gauge plate (backbar) and track (valbar), 125x59 u, drawn 136x70 u.
MCC_BACK = "M1 10.4Q1 9.2 2.2 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H2.2Q1 49.3 1 48.1Z"
MCC_FILL = ("M5.4 14.8Q5.4 13.8 6.4 13.8H118.6Q119.6 13.8 119.6 14.8V34.6H105.4A16.6 16.6 0 0 0 93.6 44.7H6.4Q5.4 44.7 5.4 "
            "43.7Z")
HULL, HULL_WARN = "#B3D06E", "#FF3300"
USS_KEY, HANNIBAL_KEY = "#str_231242", "#str_231243"


def mcc_readout(doc: HudDocument, dy: float = -36.0) -> dict:
    """The U.S.S. Hannibal readout (cannonplayerhud.gui, group 469,64): the
    stock gauge plate scaled to 136x70 u under U.S.S. Hannibal in Marine 0.25,
    the hull in Marine 0.50 #F1F0C9 (the MCC's health divided by 10, 2000 when
    whole). Remastered stacks it 36 u higher, directly under the transmission
    display, fills the track in proportion (the stock track is static) and at
    or below 25% turns it #FF3300 at 0.60 with a 2 Hz pulse, as the vehicle
    gauges warn."""
    from remastered_hud_base import BOUND_TEXT, shear_group, svg_path, text_box
    from remastered_hud_vehicle import moving_window
    doc.number("mcchealth", 2000)
    width, height = 136.0, 70.0
    sx, sy = width / 125.0, height / 59.0
    m = (round(sx * ui.U, 5), 0, 0, round(sy * ui.U, 5), 0, 0)
    health = op("clamp", op("/", S("mcchealth"), 2000), 0, 1)
    warn = op("<=", health, 0.25)

    def part(ident, data, colour, alpha, **props):
        return vec(ident, [svg_path(ident, data, matrix=m, fill=ui.solid(rgb(colour, alpha)))], props or None)
    back = part("mcc-back", MCC_BACK, "#000000", 0.5)
    back_opaque = part("mcc-back-opaque", MCC_BACK, "#000000", 0.85, display=ui.keyword("none"))
    doc.show("mcc-back", op("!", S("oq4.opaque")))
    doc.show("mcc-back-opaque", S("oq4.opaque"))
    track = part("mcc-track", MCC_FILL, HULL, 0.2)
    doc.show("mcc-track", op("!", warn))
    track_warn = part("mcc-track-warn", MCC_FILL, HULL_WARN, 0.6, opacity=num(1), display=ui.keyword("none"))
    doc.show("mcc-track-warn", warn)
    doc.timeline("mcc-pulse", 500, [tr("mcc-track-warn", "opacity", [(0, num(1)), (250, num(0.5)), (500, num(1))])], iterations=0)
    doc.joins.append(play("mcc-pulse"))
    fill = part("mcc-fill", MCC_FILL, HULL, 0.35)
    doc.show("mcc-fill", op("!", warn))
    fill_warn = part("mcc-fill-warn", MCC_FILL, HULL_WARN, 0.6, display=ui.keyword("none"))
    doc.show("mcc-fill-warn", warn)
    # The fill grows from the leading end: the window's trailing edge follows the hull.
    window = moving_window(doc, "mcc-win", 0, 0, width, height, [fill, fill_warn],
                           op("-", op("*", op("+", 5.4, op("*", 114.2, health)), sx), width))
    gauge = box("mcc-gauge", u(494), u(80 + dy), u(width), u(height),
                [shear_group("mcc-plate", 0, 0, u(width), u(height), -0.22, [back, back_opaque, track, track_warn, window])])
    labels = [text_box("mcc-uss", USS_KEY, face="marine", size=u(8.375), colour=[1, 1, 1, 0.74], baseline=u(89 + dy), x=u(512),
                       width=u(60)),
              text_box("mcc-name", HANNIBAL_KEY, face="marine", size=u(8.375), colour=[1, 1, 1, 0.74], baseline=u(89 + dy),
                       x=u(560), width=u(80))]
    hull = text_box("mcc-hull", BOUND_TEXT, face="marine", size=u(16.75), colour=rgb("#F1F0C9", 0.733), baseline=u(128 + dy),
                    right=u(585), width=u(101))
    doc.bind("mcc-hull.text", "mcc-hull", "text", op("numberText", op("floor", S("mcchealth"))))
    doc.bind("mcc-hull.color", "mcc-hull", "color", [sel(warn, c, d) for c, d in zip(rgb(HULL_WARN, 0.733), rgb("#F1F0C9", 0.733))])
    return ui.group("mcc", {**ui.FULL, "display": ui.keyword("block")}, [gauge, *labels, hull])


def mcc_document() -> dict:
    """The MCC cannon (guis/vehicles/mcc/cannonplayerhud.gui, 14.12): the
    side gun cannot be damaged (it inherits noDamage, health 0 and god mode),
    so its HUD reports what the player defends, the MCC's hull. Remastered
    adds the transmission display the side gun's HUD lacks, stacks the readout
    under it and shows Exit in the HUD prompt."""
    from remastered_hud_groups import notices, transmission
    from remastered_hud_status import full
    from remastered_hud_vehicle import hud_prompt
    doc = HudDocument("openq4.hud.remastered.mcc")
    layout_state(doc)
    radio = transmission(doc, strogg=False, emblem="squad_falcon")
    exit_notice, saved, missile_text = notices(doc)
    readout = mcc_readout(doc)
    prompt = hud_prompt(doc)
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}})
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    # The HUD exists only while the gun is crewed.
    doc.joins.append({"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Minimal (ui_hudLayout 2) keeps the hull readout, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio]), readout]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [missile_text, exit_notice]), saved]),
        anchor(doc, "br", [prompt]),
    ])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.12",
                                                      "stock": "guis/vehicles/mcc/cannonplayerhud.gui"}}})


# ------------------------------------------------------ the chaingun turret

# Barrel heat (idVehicleTurretGun): #FF9E1A, the vehicle charge bar's orange,
# and #FF3300, the HUD's warning red, from vehicle_heatwarn and while it vents.
HEAT, HEAT_HOT = "#FF9E1A", "#FF3300"
OVERHEATED_KEY = "#str_231709"


def heat_gauge(doc: HudDocument) -> dict:
    """The barrel heat on the Marine gauge where the on-foot status bar shows
    ammunition (101, 420 u): its track at 0.20 and the trailing-anchored fill
    at 0.35 in #FF9E1A, both #FF3300 (0.35 and 0.60) and pulsing at 2 Hz
    once the heat reaches vehicle_heatwarn or the gun vents; the heat as a
    percentage of the gun's limit in Chain 0.5 where the clip shows, the
    gun's traced glyph (veh_mgun) where the weapon render sits and a heat
    symbol in the notch, where the other gauges carry their icons."""
    from remastered_hud_status import BACK, MARINE, backing_nodes, fill_window, gauge_surface, legacy_readout
    from remastered_hud_status import full as full_group
    w, h, k = 125, 59, -0.22
    heat = op("clamp", S("vehicle_heat"), 0, 1)
    hot = OR(op(">=", heat, S("vehicle_heatwarn")), op("!=", S("vehicle_overheat"), 0))
    plate = backing_nodes(doc, "heat", BACK)
    tracks, fills = [], []
    for name, colour, shown, track_alpha, fill_alpha in (("cool", HEAT, NOT(hot), 0.2, 0.35),
                                                         ("hot", HEAT_HOT, hot, 0.35, 0.6)):
        track = gauge_surface(f"heat-track-{name}", "ammo", False, colour, track_alpha)
        doc.show(track["id"], shown)
        tracks.append(track)
        fill = gauge_surface(f"heat-fill-{name}", "ammo", False, colour, fill_alpha)
        doc.show(fill["id"], shown)
        fills.append(fill)
    # Only the hot paint pulses; it is hidden while the gun is cool.
    for node in (tracks[1], fills[1]):
        node["properties"]["opacity"] = num(1)
    doc.timeline("heat-pulse", 500, [tr(node["id"], "opacity", [(0, num(1)), (250, num(0.5)), (500, num(1))])
                                     for node in (tracks[1], fills[1])], iterations=0)
    doc.joins.append(play("heat-pulse"))
    plate += tracks
    plate.append(fill_window(doc, "heat-fill", w, h, "vehicle_heat", fills, 5.4, 115, 120))
    pulse = shear_group("heat-plate", 0, 0, u(w), u(h), k, plate)
    ink = rgb(MARINE["ink"])
    number = legacy_readout(doc, "heat-num", op("*", heat, 100), face="chain", rect=(66, 9, 52, 26), scale=.5, colour=ink,
                            shear=k)
    numerals = full_group("heat-numerals", [number], color=ui.colour(ink))
    doc.bind("heat-numerals.color", "heat-numerals", "color", [sel(hot, c, d) for c, d in zip(rgb(HEAT_HOT), ink)])
    gun = full_group("heat-gun", hud_svg.icon_nodes("veh_mgun", "heat-gun-g", u(9), u(17), u(22), tint=MARINE["gun"]))
    # The notch carries the gauge's symbol, as on the health and armor gauges
    # (16 u at 98, 38): three rising heat waves, #FF3300 at 0.80 when hot.
    icons = []
    for name, tint, shown in (("cool", MARINE["icon"], NOT(hot)), ("hot", rgb(HEAT_HOT, 0.8), hot)):
        waves = [pts_path(f"wave{i}", [(u(x0 + 1.4 * math.sin(math.pi * 3 * s / 12)), u(53 - 14 * s / 12)) for s in range(13)],
                          closed=False, stroke=stroke(tint, u(1.9), join="round", cap="round"))
                 for i, x0 in enumerate((101.5, 106, 110.5))]
        icon = vec(f"heat-icon-{name}", waves)
        doc.show(icon["id"], shown)
        icons.append(icon)
    return box("heat-gauge", u(101), u(420), u(w), u(h), [pulse, gun, numerals, *icons])


def heat_reticle(doc: HudDocument) -> dict:
    """Under the crosshair, once the barrel is warm: a 40x3 u bar at black
    0.45 filling from its leading edge in #FF9E1A at 0.85, #FF3300 from the
    warning level, and OVERHEATED in Marine 10.5 u #FF3300 with the stock drop
    shadow, pulsing between 100% and 55% at 2 Hz, while the gun vents.
    Minimal keeps it: the gun cannot be used without it."""
    from remastered_hud_vehicle import rect_path
    heat = op("clamp", S("vehicle_heat"), 0, 1)
    overheated = op("!=", S("vehicle_overheat"), 0)
    hot = OR(op(">=", heat, S("vehicle_heatwarn")), overheated)
    track = vec("xheat-track", [rect_path("track", ui.solid([0, 0, 0, 0.45]))],
                ui.absolute(left=u(300), top=u(262), width=u(40), height=u(3)))
    bars = []
    for name, colour, shown in (("cool", rgb(HEAT, 0.85), NOT(hot)), ("hot", rgb(HEAT_HOT, 0.9), hot)):
        bar = vec(f"xheat-{name}", [rect_path("bar", ui.solid(colour))],
                  {**ui.absolute(left=u(300), top=u(262), width=u(40), height=u(3)), "transform": ui.transform()})
        # Scaled about its leading edge: the box centre sits 20 u in.
        doc.bind(f"xheat-{name}.scale", bar["id"], "transform", [op("*", u(-20), op("-", 1, heat)), 0, heat, 1, 0])
        doc.show(bar["id"], shown)
        bars.append(bar)
    from remastered_hud_base import shadowed
    over = ui.group("xheat-over", {**ui.FULL, "display": ui.keyword("none"), "opacity": num(1)},
                    shadowed("xheat-over-text", OVERHEATED_KEY, face="marine", size=u(10.5), colour=rgb(HEAT_HOT, 1),
                             baseline=u(281), center=u(320), width=u(220)))
    doc.show("xheat-over", overheated)
    doc.timeline("xheat-over-pulse", 500, [tr("xheat-over", "opacity", [(0, num(1)), (250, num(0.55)), (500, num(1))])],
                 iterations=0)
    doc.joins.append(play("xheat-over-pulse"))
    group = ui.group("xheat", {**ui.FULL, "display": ui.keyword("none")}, [track, *bars, over])
    doc.show("xheat", OR(op(">", heat, 0.02), overheated))
    return group


def chaingun_turret_document() -> dict:
    """The Awakening's Strogg chaingun turret (guis/vehicles/turret_hud.gui,
    14.12; m01's trench turret and m04's prison turrets). The gun cannot be
    damaged, but its gunner stands exposed behind it, so the status bar is
    the gunner's: the on-foot Marine health and armor gauges in their places,
    and the barrel heat on the gauge where ammunition shows on foot. The
    heat also shows under the crosshair. With them the transmission display,
    the damage direction, the notices and the HUD prompt's Exit. The HUD
    exists only while the gun is crewed."""
    from remastered_hud_base import status_scale
    from remastered_hud_groups import hit_indicator, notices, transmission
    from remastered_hud_status import marine_gauge
    from remastered_hud_vehicle import hud_prompt
    doc = HudDocument("openq4.hud.remastered.chaingun_turret")
    layout_state(doc)
    for key in ("player_health", "player_healthpct", "player_armor", "player_armorpct", "player_healthDelta",
                "player_armorDelta"):
        doc.number(key, 0)
    doc.number("player_healthpct", 1)
    doc.number("vehicle_heat", 0)
    doc.number("vehicle_overheat", 0)
    doc.number("vehicle_heatwarn", 0.75)
    doc.number("vehicle_zoom", 0)
    doc.boolean("mp")
    doc.boolean("oq4.low")
    doc.boolean("oq4.teamStrogg")
    gauges = [heat_gauge(doc), marine_gauge(doc, "health", 278, 420, mp_variants=False),
              marine_gauge(doc, "armor", 414, 420, mp_variants=False)]
    doc.show("armor-gauge", op(">", S("player_armor"), 0))
    reticle = heat_reticle(doc)
    radio = transmission(doc, strogg=False, emblem="squad_falcon")
    hit = hit_indicator(doc)
    exit_notice, saved, missile_text = notices(doc)
    prompt = hud_prompt(doc)
    # Low health below 26% of the maximum, and the gain pops, as on foot.
    doc.on("updateHealth",
           {"op": "setState", "values": {"oq4.low": op("<", S("player_healthpct"), 0.26)}},
           {"op": "if", "condition": op("<", S("player_healthDelta"), 0), "then": [play("health-gain")]})
    doc.on("updateArmor", {"op": "if", "condition": op("<", S("player_armorDelta"), 0), "then": [play("armor-gain")]})
    doc.joins.append({"op": "setState", "values": {"oq4.low": op("<", S("player_healthpct"), 0.26)}})
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}})
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    doc.joins.append({"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    joins = join_events(doc, [("p_radio::visible", "radioChatterUp"), ("lock_msg::visible", "showLockmessage")])
    # Minimal (ui_hudLayout 2) keeps the gauges, the crosshair's heat, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        anchor(doc, "bc", gauges, scale=status_scale(S("mp"))),
        anchor(doc, "cc", [reticle, beyond_minimal(doc, "cc-full", [hit])]),
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio])]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [missile_text, exit_notice]), saved]),
        anchor(doc, "br", [prompt]),
    ])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.12",
                                                      "stock": "guis/vehicles/turret_hud.gui", "joinEvents": joins}}})


# ------------------------------------------------------------ the speeder bike

# The bike lists its machinegun first (vehicle_weapon 0) and its cannon second,
# which recharges between shots; the expansion's HUD showed the hover tank's
# panels for them. The entry lines name the other weapon first.
SPEEDER_WEAPONS = [("veh_mgun", "veh_shell"), ("veh_shell", "veh_mgun")]
SPEEDER_LINES = [("#str_200940", "#str_200939"), ("#str_200939", "#str_200940")]
# speederplayerhud.gui's progress bar (its group sits 1 u high): the plate with
# its raised tab, and the pip leaning forward, repeated at each pip's x.
RACE_PLATE = [(68.1, 59.6), (554.6, 60.5), (571.4, 23.5), (125.0, 22.5), (115.9, 15.0), (85.4, 15.0), (83.4, 16.4)]
RACE_PIP = [(238.0, 26.4), (230.5, 55.0), (251.6, 55.5), (259.1, 26.9)]
RACE_PIPS = (215, 241, 267, 293, 319, 374, 400, 426, 452, 478)
MARINE = 33.5                       # Marine's size at textscale 1, u
GOLD, RACE_RED = "#E1CC67", "#FF3333"
BOOST_KEY, BOOSTING_KEY, POINT_KEY = "#str_231244", "#str_231245", "#str_231246"


def race_bar(doc: HudDocument, dy: float = 18.0) -> dict:
    """The m07 race display (14.12): a black plate at 0.454 whose raised tab
    carries the race timer, and ten forward-leaning pips in two groups of
    five, #4E5B1C dark and #798D2A lit, lit one per checkpoint (the script's
    showprogress0..100). The timer reads the script's countdown in gold Marine
    0.65 seconds and Marine 0.55 hundredths; Remastered keeps the hundredths
    two digits wide, turns the seconds #FF3333 over 100 ms in the last 10 s,
    shows the timer only while the race runs it (the stock's first frame
    showed a placeholder) and sets the bar `dy` u lower, clear of the
    transmission display at 4:3."""
    doc.number("oq4.race.progress")
    doc.number("currenttime")
    doc.number("currenttime2")
    for key in ("oq4.race.timer", "oq4.race.timer2", "oq4.race.red"):
        doc.boolean(key)

    def place(points, dx=0.0):
        return [(u(x + dx), u(y + dy)) for x, y in points]
    plate = vec("race-plate", [pts_path("plate", place(RACE_PLATE), fill=ui.solid([0, 0, 0, 0.454]))])
    opaque = vec("race-plate-opaque", [pts_path("plate", place(RACE_PLATE), fill=ui.solid([0, 0, 0, 0.85]))],
                 {"display": ui.keyword("none")})
    doc.show("race-plate", NOT(S("oq4.opaque")))
    doc.show("race-plate-opaque", S("oq4.opaque"))
    dark = vec("race-pips", [pts_path(f"pip{i}", place(RACE_PIP, x - 215), fill=ui.solid(rgb("#4E5B1C", 0.768)),
                                      stroke=stroke(rgb("#4E5B1C", 0.5), u(1.2))) for i, x in enumerate(RACE_PIPS)])
    lit = []
    for i, x in enumerate(RACE_PIPS):
        node = vec(f"race-pip{i}", [pts_path("lit", place(RACE_PIP, x - 215), fill=ui.solid(rgb("#798D2A", 0.76)),
                                             stroke=stroke(rgb("#B9CF62", 0.55), u(1.2)))], {"display": ui.keyword("none")})
        doc.show(node["id"], op(">=", S("oq4.race.progress"), i + 1))
        lit.append(node)
    spacing = {"letter-spacing": ui.length(u(1))}
    seconds = text_box("race-sec", BOUND_TEXT, face="marine", size=u(0.65 * MARINE), colour=rgb(GOLD), baseline=u(54 + dy),
                       right=u(158), width=u(90), extra={**spacing, "display": ui.keyword("none")})
    doc.bind("race-sec.text", "race-sec", "text", op("numberText", op("max", 0, op("floor", S("currenttime")))))
    doc.show("race-sec", S("oq4.race.timer"))
    point = text_box("race-point", POINT_KEY, face="marine", size=u(0.55 * MARINE), colour=rgb("#E2CC68"), baseline=u(54 + dy),
                     x=u(156), width=u(10), extra={"display": ui.keyword("none")})
    doc.show("race-point", S("oq4.race.timer2"))
    # The hundredths, always two digits: the tens, then the units.
    size = u(0.55 * MARINE)
    hundredths = op("clamp", op("floor", S("currenttime2")), 0, 99)

    def digits(ident, expr):
        node = ui.label(ident, BOUND_TEXT, {**ui.typeface("marine", size, size, rgb("#DFCC66")), **spacing,
                                            "white-space": ui.keyword("nowrap"), "position": ui.keyword("relative"),
                                            "display": ui.keyword("block")})
        doc.bind(ident + ".text", ident, "text", expr)
        return node
    hundredth_row = ui.group("race-hun", {**ui.absolute(left=u(164), top=u(54 + dy) - size * face_metrics("marine")["ascent"],
                                                        height=size), "display": ui.keyword("none"),
                                          "flex-direction": ui.keyword("row")},
                             [digits("race-hun-tens", op("numberText", op("floor", op("/", hundredths, 10)))),
                              digits("race-hun-units", op("numberText", op("%", hundredths, 10)))])
    doc.show("race-hun", S("oq4.race.timer2"), "flex")
    gold, red = ui.colour(rgb(GOLD)), ui.colour(rgb(RACE_RED))
    doc.timeline("race-red", 100, [tr("race-sec", "color", [(0, gold), (100, red)])], essential=True)
    doc.timeline("race-gold", 100, [tr("race-sec", "color", [(0, red), (100, gold)])], essential=True)
    last = op("<", S("currenttime"), 10)
    # The script runs the timer by events: the seconds and hundredths show
    # with their show events or as the countdown updates them.
    doc.on("showspeedertimer", {"op": "setState", "values": {"oq4.race.timer": True}})
    doc.on("hidespeedertimer", {"op": "setState", "values": {"oq4.race.timer": False}})
    doc.on("showspeedertimer2", {"op": "setState", "values": {"oq4.race.timer2": True}})
    doc.on("hidespeedertimer2", {"op": "setState", "values": {"oq4.race.timer2": False}})
    doc.on("updatetimer", {"op": "setState", "values": {"oq4.race.timer": True}},
           when(AND(last, NOT(S("oq4.race.red"))), [{"op": "setState", "values": {"oq4.race.red": True}}, play("race-red")]),
           when(AND(NOT(last), S("oq4.race.red")), [{"op": "setState", "values": {"oq4.race.red": False}}, play("race-gold")]))
    doc.on("updatetimer2", {"op": "setState", "values": {"oq4.race.timer2": True}})
    for n in range(0, 101, 10):
        doc.on(f"showprogress{n}", {"op": "setState", "values": {"oq4.race.progress": n // 10}})
    return box("race", 0, 0, u(640), u(480), [plate, opaque, dark, *lit, seconds, point, hundredth_row])


def hazard_icon(ident: str) -> dict:
    """The boost hazard (atlas hudPrompt): an #E62323 triangle edged in
    #1C0202 with its exclamation mark, 9 u wide on the prompt's line."""
    m = 5.3
    ink = stroke(rgb("#1C0202"), u(1.1), cap="round")
    paths = [pts_path("tri", scaled([(4.5, m - 4.6), (9, m + 3.8), (0, m + 3.8)]), fill=ui.solid(rgb("#E62323")),
                      stroke=stroke(rgb("#1C0202"), u(0.6), join="round")),
             pts_path("bar", scaled([(4.5, m - 1.5), (4.5, m + 1)]), stroke=ink, closed=False),
             pts_path("dot", scaled([(4.5, m + 2.3), (4.5, m + 2.6)]), stroke=ink, closed=False)]
    return ui.group(ident, {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(9)),
                            "height": ui.length(u(10.6)), "margin-right": ui.length(u(4))}, [vec(ident + "-g", paths)])


def boost_items(doc: HudDocument) -> list:
    """The boost in the HUD prompt (14.8, 14.12): the crouch control's cap
    and Boost while a boost is ready (the game's oq4_boost 0), the hazard and
    Boosting in #FF3333 while it pushes (1), nothing while it recharges (2).
    The expansion's "[ Press CROUCH to boost ]" took the exit line's window;
    Exit follows these at the prompt's trailing end."""
    from remastered_hud_mp import flex_row, label
    from remastered_hud_vehicle import prompt_chip
    doc.number("oq4_boost")
    doc.string("oq4_key_crouch")
    gap = {"margin-right": ui.length(u(11))}
    ready = flex_row("vpr-boost", [prompt_chip(doc, "vpr-boost", S("oq4_key_crouch")),
                                   label("vpr-boost-word", BOOST_KEY, 10.6, [1, 1, 1, 0.92], line=16)],
                     **{"display": ui.keyword("none"), **gap})
    doc.bind("vpr-boost.display", "vpr-boost", "display", sel(op("==", S("oq4_boost"), 0), "flex", "none"))
    pushing = flex_row("vpr-boosting", [hazard_icon("vpr-hazard"), label("vpr-boosting-word", BOOSTING_KEY, 10.6, rgb(RACE_RED), line=16)],
                       **{"display": ui.keyword("none"), **gap})
    doc.bind("vpr-boosting.display", "vpr-boosting", "display", sel(op("==", S("oq4_boost"), 1), "flex", "none"))
    return [ready, pushing]


def speed_streak(doc: HudDocument) -> dict:
    """While the boost pushes, the expansion's speed blur (its hs/SpeedBlur
    window, which its HUD left hidden): the view smears toward its centre
    along the bike's path, from nothing a quarter of the way in to the edges
    along speeder_blur's square frame, with the boost's envelope (the game's
    oq4_boost_envelope: 500 ms in, held, then 1.5 s out). The host draws it
    (backdrop-streak). Never under reduced motion, and not in Minimal."""
    doc.number("oq4_boost_envelope")
    node = ui.group("streak", {**ui.FULL, "display": ui.keyword("block"), "backdrop-streak": num(0)})
    off = OR(S("oq4.reduced"), op("==", S("hud.layout"), 2))
    doc.bind("streak.strength", "streak", "backdrop-streak", sel(off, 0, op("clamp", S("oq4_boost_envelope"), 0, 1)))
    return node


def speeder_document() -> dict:
    """The speeder bike (guis/vehicles/speederplayerhud.gui, 14.12): the
    vehicle status bar centred on the bottom edge with the bike's own glyphs
    and entry lines and the gauges' warnings the expansion removed, the race
    display at the top centre, the HUD prompt with the boost and Exit (which
    the expansion's HUD lost), and the speed streak while boosting."""
    from remastered_hud_groups import boss_bar, missile_chevron, notices, transmission
    from remastered_hud_status import full
    from remastered_hud_vehicle import electric_warning, hit_flash, hud_prompt, vehicle_gauge, weapon_lines, weapon_panel
    doc = HudDocument("openq4.hud.remastered.speeder")
    layout_state(doc)
    for key in ("vehicle_health", "vehicle_shield", "vehicle_weaponcharge", "vehicle_weaponammo"):
        doc.number(key, 1 if key in ("vehicle_health", "vehicle_shield") else 0)
    hull_warn = op("<=", S("vehicle_health"), 0.25)
    shield_warn = NOT(op(">", S("vehicle_shield"), 0))
    gauges = [vehicle_gauge(doc, "hull", 71, "#B3D06E", 0.35, "#str_200911", "health", "vehicle_health", hull_warn, True),
              vehicle_gauge(doc, "shield", 164, "#B2CF6D", 0.349, "#str_200912", "armor", "vehicle_shield", shield_warn, False)]
    panel = weapon_panel(doc, SPEEDER_WEAPONS, charged=(1,), counted=())
    lines, show_lines, hide_lines = weapon_lines(doc, SPEEDER_LINES)
    # The status bar spans 11-288 u in the stock; Remastered centres it on 320.
    bar = box("vbar", u(170.5), 0, u(640), u(480), panel + gauges + lines)
    race = race_bar(doc)
    radio = transmission(doc)
    boss = boss_bar(doc, offset=20.0)
    exit_notice, saved, missile_text = notices(doc)
    missile = missile_chevron(doc)
    prompt = hud_prompt(doc, boost_items(doc))
    hit = hit_flash(doc)
    elec_l, elec_r = electric_warning(doc)
    streak = speed_streak(doc)
    # The expansion sent its entry animation to no handler; Remastered plays
    # the stock vehicle's lines.
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}}, *show_lines)
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}}, *hide_lines)
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    # The HUD draws only while the bike is ridden, so a joining peer rides.
    doc.joins.append({"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Minimal (ui_hudLayout 2) keeps the status bar, the race display, the
    # prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        streak,
        beyond_minimal(doc, "hit-full", [hit]),
        anchor(doc, "cl", [beyond_minimal(doc, "cl-full", [elec_l])]),
        anchor(doc, "cr", [beyond_minimal(doc, "cr-full", [elec_r])]),
        anchor(doc, "bc", [bar], scale="hud.centerScale"),
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio])]),
        anchor(doc, "tc", [race, beyond_minimal(doc, "tc-full", [boss, missile_text, exit_notice]), saved]),
        anchor(doc, "cc", [beyond_minimal(doc, "cc-full", [missile])]),
        anchor(doc, "br", [prompt]),
    ])
    pairs = [("p_radio::visible", "radioChatterUp"), ("missile_indicator::visible", "missileThreatUp"),
             ("left_elecwarn::visible", "electricWarningOn"), ("Bossbar::visible", "showBossBar"),
             ("p_boss_shield_bar_left::visible", "showBossShieldBar")]
    pairs += [(f"progress_{n}_on::visible", f"showprogress{n}") for n in range(10, 101, 10)]
    joins = join_events(doc, pairs)
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.8, 14.12",
                                                      "stock": "guis/vehicles/speederplayerhud.gui", "joinEvents": joins}}})


# ------------------------------------------------------------ the GEV

# guis/vehicles/gev_hud.gui (vehicle_speeder; findings awakening_hud.md 4)
# names ten materials that neither game ships, so the cluster is the atlas's
# own drawing in the stock construction: the stock rects, with plates and bars
# sheared 0,-.32 about their own centres.
GEV_INK, GEV_CHARGE = "#B0C891", "#FF9E1A"
GEV_K = -0.32
GEV_SHIFT = 210.5            # Remastered centres the cluster (its plates span x 6.4-212.6 u) on 320
GEV_HULL = (77, 444)         # the hull glyph's centre (the stock base and turret, 48,415 58x58)
GEV_GLYPH = (171, 433)       # the weapon glyph's centre, turned 60 degrees
# The shield stages: radius, colour and the stock alpha (shield - low) / span.
GEV_STAGES = [(27.0, "#D1AF47", 0.8, 0.2), (23.5, "#C6C251", 0.4, 0.4), (20.0, "#A2B85C", 0.0, 0.4)]
# vehicle_weapon: 0 the cannon, 1 the machinegun, 2 the rocket pods (which
# the def comments out).
GEV_GLYPHS = ("veh_shell", "veh_mgun", "veh_rocket")


def gev_arcs(ident: str, r: float, spans: tuple, colour: list, width: float, props: dict | None = None) -> dict:
    """Arcs about the hull glyph's centre (30 u into its box), one per span
    of degrees clockwise from east, round-capped."""
    paths = [pts_path(f"a{index}", scaled(arc_points(30, 30, r, a0, a1, 3.0)), closed=False,
                      stroke=stroke(colour, u(width), join="round", cap="round")) for index, (a0, a1) in enumerate(spans)]
    return vec(ident, paths, props)


def gev_cluster(doc: HudDocument) -> dict:
    """The GEV's status cluster (14.12): two plates at black 0.60; a vertical
    #FF9E1A health bar in its #B0C891 track draining from the top (the stock
    divided the already normalized health by 1000 and read empty); the hull
    from above turning with the view relative to it under the fixed turret;
    three shield stages as arcs on both sides, fading out in turn, and inner
    arcs pulsing #FF591A to #80591A every 500 ms once the shields fail; the
    weapon's glyph turned 60 degrees with an infinity mark, or the rocket
    count, over the charge bar."""
    from remastered_hud_status import full
    for key, initial in (("vehicle_health", 1), ("vehicle_shield", 1), ("vehicle_weapon", 0), ("vehicle_weaponammo", 0),
                         ("vehicle_weaponcharge", 1), ("vehicle_rotate", 0)):
        doc.number(key, initial)
    ink = rgb(GEV_INK)
    nodes = []
    for ident, rect in (("gev-plate-hull", (16, 412, 97, 60)), ("gev-plate-weapon", (120, 412, 83, 60))):
        outline = scaled(rounded_rect(*rect, 2, GEV_K, 442))
        normal = vec(ident, [pts_path("back", outline, fill=ui.solid([0, 0, 0, 0.6]))])
        opaque = vec(ident + "-opaque", [pts_path("back", outline, fill=ui.solid([0, 0, 0, 0.85]))], {"display": ui.keyword("none")})
        doc.show(normal["id"], NOT(S("oq4.opaque")))
        doc.show(opaque["id"], S("oq4.opaque"))
        nodes += [normal, opaque]
    # The health bar: the track, the well and the fill, which keeps its foot
    # while it shortens from the top.
    nodes.append(vec("gev-health-track", [pts_path("track", scaled(rounded_rect(21, 416, 23, 52, 2, GEV_K, 442)),
                                                   fill=ui.solid(rgb(GEV_INK, 0.3)))]))
    nodes.append(vec("gev-health-well", [pts_path("well", scaled(rounded_rect(25, 419, 15, 46, 1.4, GEV_K, 442)),
                                                  fill=ui.solid([0, 0, 0, 0.65]))]))
    health = op("clamp", S("vehicle_health"), 0, 1)
    fill = box("gev-health-fill", u(25), u(419 - 412), u(15), u(46),
               [vec("gev-health-fill-v", [pts_path("fill", scaled(rounded_rect(0, 0, 15, 46, 1.4)), fill=ui.solid(rgb(GEV_CHARGE)))])],
               transform=ui.transform())
    doc.bind("gev-health-fill.drain", "gev-health-fill", "transform", [0, op("*", u(23), op("-", 1, health)), 1, health, 0])
    nodes.append(shear_group("gev-health", 0, u(412), u(640), u(60), GEV_K, [fill]))
    # The hull turns by the view's yaw relative to it (the stock rotate turns
    # counter-clockwise for a positive angle); the turret, its dome and
    # barrel, stays put.
    cx, cy = GEV_HULL
    hull = box("gev-hull", 0, 0, u(60), u(60), [vec("gev-hull-v", [
        svg_path("body", "M-10 -17Q0 -21 10 -17L13 14Q0 19 -13 14Z", dx=u(30), dy=u(30),
                 stroke=stroke(ink, u(2.2), join="round")),
        svg_path("skirts", "M-16 -11V10M16 -11V10", dx=u(30), dy=u(30), stroke=stroke(ink, u(3), cap="round"))])],
        transform=ui.transform())
    doc.bind("gev-hull.turn", "gev-hull", "transform", [0, 0, 1, 1, op("-", 0, S("vehicle_rotate"))])
    turret = vec("gev-turret", [pts_path("dome", scaled(circle_points(30, 30, 6.5, 32)), fill=ui.solid(rgb(GEV_INK, 0.85))),
                                pts_path("barrel", scaled(rounded_rect(28.4, 8, 3.2, 16, 1)), fill=ui.solid(ink))])
    shield = S("vehicle_shield")
    stages = []
    for index, (r, colour, low, span) in enumerate(GEV_STAGES):
        stage = gev_arcs(f"gev-shield{index + 1}", r, ((125, 235), (-55, 55)), rgb(colour), 2.6, {"opacity": num(1)})
        doc.bind(stage["id"] + ".fade", stage["id"], "opacity", op("clamp", op("/", op("-", shield, low), span), 0, 1))
        stages.append(stage)
    spans = ((140, 220), (-40, 40))
    out_dim = gev_arcs("gev-shield-out-dim", 18, spans, rgb("#80591A"), 3)
    out_lit = gev_arcs("gev-shield-out-lit", 18, spans, rgb("#FF591A"), 3, {"opacity": num(1)})
    out = full("gev-shield-out", [out_dim, out_lit], display=ui.keyword("none"))
    doc.show(out["id"], op("<=", shield, 0))
    # Half a second each way; reduced motion holds the lit colour.
    doc.timeline("gev-shield-out-pulse", 1000, [tr(out_lit["id"], "opacity", [(0, num(1)), (499, num(1)), (500, num(0)), (999, num(0)),
                                                                              (1000, num(1))])], iterations=0)
    doc.joins.append(play("gev-shield-out-pulse"))
    nodes.append(box("gev-glyph", u(cx - 30), u(cy - 30), u(60), u(60), [hull, turret, *stages, out]))
    # The weapon: its glyph turned 60 degrees, nose up and leading; an
    # infinity mark, or the rocket pods' count, over the charge bar.
    weapon = S("vehicle_weapon")
    gx, gy = GEV_GLYPH
    for index, glyph in enumerate(GEV_GLYPHS):
        turned = box(f"gev-weapon{index}", u(gx - 30), u(gy - 30), u(60), u(60),
                     hud_svg.icon_nodes(glyph, f"gev-weapon{index}-g", u(30), u(30 - 11), u(22), anchor="middle", tint=ink),
                     transform=transform_value(deg=60), display=ui.keyword("none"))
        doc.show(turned["id"], op("==", weapon, index))
        nodes.append(turned)
    infinity = shear_group("gev-infinity", u(128), u(435), u(29), u(24), GEV_K,
                           hud_svg.icon_nodes("icon_infinity", "gev-infinity-g", u(14.5), 0, u(24), anchor="middle",
                                              tint=rgb(GEV_INK, 0.8)))
    doc.show(infinity["id"], op("!=", weapon, 2))
    count = text_box("gev-count-text", "", face="marine", size=50.4, colour=rgb(GEV_INK, 0.8), baseline=u(457 - 420),
                     center=u(140.5), width=u(60))
    doc.bind("gev-count-text.text", "gev-count-text", "text", op("numberText", op("max", 0, op("floor", S("vehicle_weaponammo")))))
    counted = shear_group("gev-count", 0, u(420), u(640), u(41), GEV_K, [count])
    counted["properties"]["display"] = ui.keyword("none")
    doc.show(counted["id"], op("==", weapon, 2))
    nodes += [infinity, counted]
    nodes.append(vec("gev-charge-track", [pts_path("track", scaled(rounded_rect(124, 459, 63, 6, 3, GEV_K, 462)),
                                                   fill=ui.solid(rgb(GEV_INK, 0.3)))]))
    charge = op("clamp", S("vehicle_weaponcharge"), 0, 1)
    bar = box("gev-charge-fill", u(129), u(461 - 459), u(53), u(2),
              [vec("gev-charge-fill-v", [pts_path("fill", scaled(rounded_rect(0, 0, 53, 2, 1)), fill=ui.solid(rgb(GEV_CHARGE)))])],
              transform=ui.transform())
    # Scaled about its leading end: the box centre sits 26.5 u in.
    doc.bind("gev-charge-fill.scale", "gev-charge-fill", "transform", [op("*", u(-26.5), op("-", 1, charge)), 0, charge, 1, 0])
    nodes.append(shear_group("gev-charge", 0, u(459), u(640), u(6), GEV_K, [bar]))
    return box("gev", u(GEV_SHIFT), 0, u(640), u(480), nodes)


def gev_document() -> dict:
    """The GEV (guis/vehicles/gev_hud.gui, 14.12): its status cluster centred
    on the bottom edge like every status bar, the transmission display at the
    top-trailing corner, the notices, and the HUD prompt's Exit, which its HUD
    never showed. No map places the GEV."""
    from remastered_hud_groups import notices, transmission
    from remastered_hud_status import full
    from remastered_hud_vehicle import hud_prompt
    doc = HudDocument("openq4.hud.remastered.gev")
    layout_state(doc)
    radio = transmission(doc, strogg=False, emblem="squad_falcon")
    exit_notice, saved, missile_text = notices(doc)
    cluster = gev_cluster(doc)
    prompt = hud_prompt(doc)
    doc.on("showExitmessage", {"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hideExitmessage", {"op": "setState", "values": {"oq4.vehicle": False}})
    doc.on("showLockmessage", {"op": "setState", "values": {"oq4.locked": True}})
    doc.on("hideLockmessage", {"op": "setState", "values": {"oq4.locked": False}})
    # The HUD draws only while the GEV is driven.
    doc.joins.append({"op": "setState", "values": {"oq4.vehicle": True}})
    doc.on("hudJoin", *doc.joins)
    doc.on("onInit", *doc.joins)
    # Minimal (ui_hudLayout 2) keeps the cluster, the prompt and the save notice.
    root = gameplay_root(doc, "hud-root", [
        anchor(doc, "bc", [cluster], scale="hud.centerScale"),
        anchor(doc, "tr", [beyond_minimal(doc, "tr-full", [radio])]),
        anchor(doc, "tc", [beyond_minimal(doc, "tc-full", [missile_text, exit_notice]), saved]),
        anchor(doc, "br", [prompt]),
    ])
    joins = join_events(doc, [("p_radio::visible", "radioChatterUp")])
    return doc.build(root, {"canvas": {"height": 720, "minWidth": 960},
                            "extensions": {"openq4": {"reference": "ui-visual-design.md 14.12", "stock": "guis/vehicles/gev_hud.gui",
                                                      "joinEvents": joins}}})
