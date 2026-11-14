"""The Remastered status bar: ammunition, health and armor (specification 14.3).

Marine construction from the atlas's marineGauge and Appendix B.5; the Strogg
construction from findings section 3 (hud_strogg.gui). Values arrive in the
stock HUD's own state keys.
"""
from __future__ import annotations

import math

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (
    AND, NOT, OR, S, U, HudDocument, box, num, op, play, pts_path, rgb, sel, shear_group, stroke, svg_path, text_box, tr,
    transform_value, u, vec)

# Measured Marine gauge geometry in gauge-local u inside the 125x59 rect.
BACK = "M1 10.4Q1 9.2 2.2 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H2.2Q1 49.3 1 48.1Z"
FILL = "M5.4 14.8Q5.4 13.8 6.4 13.8H118.6Q119.6 13.8 119.6 14.8V34.6H105.4A16.6 16.6 0 0 0 93.6 44.7H6.4Q5.4 44.7 5.4 43.7Z"
DIVIDERS = (15.6, 27.3, 39.1, 50.8, 62.5, 74.2, 85.9, 97.7, 108.9)
NOTCH_CENTER = (109.0, 50.8)   # the notch arc's centre; radius 16.6

# The Strogg gauge, 129x67 u (hud_strogg.gui s_backbar/s_valbar).
ST_BACK = "M1 22.5L26.8 7.3H69L72 11H128V53.4H1Z"
ST_FILL = "M46.7 15.7H124V40.3H69.3L56.3 48.7H5.1V39.8L15.8 33L24.2 32.5L33.4 26.8L33.9 24.1Z"

MARINE = {"fill": "#B3D06E", "armor": "#B2CF6D", "low": "#FF4C00", "ink": "#DDEBC3", "icon": rgb("#A8A360", 0.6),
          "gun": rgb("#C7BD78", 0.4), "ekg": "#161B0E", "ekg_low": "#4C0D00"}
# The Classic multiplayer ruleset's armour tiers (player_armorTier 1-3: jacket,
# combat, body) colour the multiplayer armour icon the way Quake 2 coloured its
# armour; body armour and any armour under Quake 4's rules (tier 0) keep the
# stock red.  Combat is a lemon yellow, apart from the health icon's gold.
CLASSIC_ARMOR_TINTS = ((1, "jacket", "#45C83A"), (2, "combat", "#EFE23A"))
STROGG = {"fill": "#FF9000", "armor": "#FF9000", "low": "#FF4C00", "ink": "#FCFFC8", "icon": rgb("#FFCC00", 0.6),
          "gun": rgb("#C6BC77", 0.4), "ekg": "#1D1403", "ekg_low": "#4C0D00", "static": "#161616"}
# The multiplayer ammunition gauge's weapon tints (hud.gui:200-234), grey
# before the first change; Appendix B.4 records where they differ from hud.mtr.
MP_AMMO = ["#00D9FF", "#FFFF00", "#FF8C00", "#0073FF", "#338F12", "#99CCCC", "#FF4000", "#00FF00", "#FFFF99", "#C433FF",
           "#FFBF40"]
MP_TEAM = {"marine": "#B2CF6D", "strogg": "#FF8000"}
MP_ROSTER = ["item_gauntlet", "item_machinegun", "item_shotgun", "item_hyperblaster", "item_grenade", "item_nailgun",
             "item_rocket", "item_railgun", "item_lightning", "item_darkmatter", "item_fire"]
# Weapon glyphs by their inv_icon base name (the peer forwards an icon
# material as its base name, without "_colored").
WEAPON_GLYPHS = ["item_blaster", "item_machinegun", "item_shotgun", "item_hyperblaster", "item_grenade", "item_nailgun",
                 "item_rocket", "item_railgun", "item_lightning", "item_darkmatter", "item_fire", "item_gauntlet",
                 "item_corecannon", "item_freeze"]


# ------------------------------------------------------------------ geometry

def arc_points(center, radius, a0, a1, steps=20):
    return [(center[0] + radius * math.cos(a0 + (a1 - a0) * i / steps),
             center[1] + radius * math.sin(a0 + (a1 - a0) * i / steps)) for i in range(steps + 1)]


def fill_outline(d: float = 0.0) -> list:
    """The Marine fill plate inset by d u: square corners once d passes the
    1 u radius, the ledge raised by d and the concave notch grown to 16.6+d."""
    left, top, right, bottom, ledge = 5.4 + d, 13.8 + d, 119.6 - d, 44.7 - d, 34.6 - d
    cx, cy = NOTCH_CENTER
    r = 16.6 + d
    x_ledge = cx - math.sqrt(max(0.0, r * r - (cy - ledge) ** 2))
    x_bottom = cx - math.sqrt(max(0.0, r * r - (cy - bottom) ** 2))
    a0 = math.atan2(ledge - cy, x_ledge - cx)
    a1 = math.atan2(bottom - cy, x_bottom - cx)
    return [(left, top), (right, top), (right, ledge)] + arc_points((cx, cy), r, a0, a1) + [(left, bottom)]


def polygon_offset(points: list, d: float) -> list:
    """A simple clockwise-on-screen polygon moved d inward (mitred joins);
    only for small d against the outline's features."""
    n = len(points)
    lines = []
    for i in range(n):
        (x1, y1), (x2, y2) = points[i], points[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        length = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / length, dx / length          # inward for clockwise-on-screen order
        lines.append(((x1 + nx * d, y1 + ny * d), (dx, dy)))
    result = []
    for i in range(n):
        (p, r), (q, s) = lines[i - 1], lines[i]
        den = r[0] * s[1] - r[1] * s[0]
        if abs(den) < 1e-9:
            result.append(q)
            continue
        t = ((q[0] - p[0]) * s[1] - (q[1] - p[1]) * s[0]) / den
        result.append((p[0] + t * r[0], p[1] + t * r[1]))
    return result


def clip_polygon(subject: list, clipper: list) -> list:
    """Sutherland-Hodgman: any simple subject inside a convex clipper given
    clockwise on screen."""
    def inside(p, a, b):
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) >= 0

    def cut(p1, p2, a, b):
        x1, y1 = p1; x2, y2 = p2; x3, y3 = a; x4, y4 = b
        den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        if abs(den) < 1e-12:
            return p2
        t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den
        return (x1 + t * (x2 - x1), y1 + t * (y2 - y1))
    output = list(subject)
    for i in range(len(clipper)):
        a, b = clipper[i], clipper[(i + 1) % len(clipper)]
        source, output = output, []
        if not source:
            break
        prev = source[-1]
        for cur in source:
            if inside(cur, a, b):
                if not inside(prev, a, b):
                    output.append(cut(prev, cur, a, b))
                output.append(cur)
            elif inside(prev, a, b):
                output.append(cut(prev, cur, a, b))
            prev = cur
    return output


def scaled(points: list, s: float = U) -> list:
    return [(x * s, y * s) for x, y in points]


def ring_path(ident: str, outer_svg: str, inner_points: list, colour: list, blend=None) -> dict:
    """An even-odd ring between an exact outline and an inset polygon."""
    path = svg_path(ident, outer_svg, fill=ui.solid(colour), rule="evenodd", blend=blend)
    start = len(path["commands"])
    inner = scaled(inner_points)
    path["commands"].append({"id": f"c{start}", "op": "move", "points": [[round(inner[0][0], 4), round(inner[0][1], 4)]]})
    for i, (x, y) in enumerate(inner[1:], start=1):
        path["commands"].append({"id": f"c{start + i}", "op": "line", "points": [[round(x, 4), round(y, 4)]]})
    path["commands"].append({"id": f"c{start + len(inner)}", "op": "close"})
    return path


def svg_polygon(data: str) -> list:
    """Vertices of a straight-edged SVG outline (the Strogg plates)."""
    return [seg[1] for seg in hud_svg.parse_path(data) if seg[0] in ("M", "L")]


def gauge_paint(kind: str, strogg: bool, colour, alpha: float) -> list:
    """Stock texture luminance multiplied by the GUI tint (Appendix B.5).

    Disjoint contour bands keep the material alpha constant through the soft
    bevel. Their shared edges must not antialias independently (that darkens
    every seam); the node's exact outline mask owns exterior coverage.
    Armor's cell cuts reduce texture alpha, rather than painting black lines.
    """
    w, h = (129, 67) if strogg else (125, 59)
    tint = rgb(colour)[:3] if isinstance(colour, str) else colour
    armor = kind == "armor"
    base = (175 if armor else 171 if strogg else 148) / 255
    depth = 17 * h / 128

    def contour(d):
        if not strogg:
            return fill_outline(d)
        points = polygon_offset(svg_polygon(ST_FILL), d)
        # The narrow leading tip disappears before the bevel reaches its
        # interior. Intersect the two surviving edges after its upright edge
        # collapses, rather than folding the inset back over itself.
        if points[6][1] > points[5][1]:
            a, b = points[6:8]
            y = points[5][1]
            x = a[0] + (y-a[1]) * (b[0]-a[0]) / (b[1]-a[1])
            points[5:7] = [(x, y)]
        return points

    def polygon(points):
        return "M" + "L".join(f"{x:.5f} {y:.5f}" for x, y in points) + "Z" if len(points) >= 3 else ""

    # Preserve the measured 24-texel pitch and 3-texel translucent cuts.
    cells, cuts, start = [], [], 0
    if armor:
        for i in range(9):
            left, right = (31 + 24*i)*w/256, (34 + 24*i)*w/256
            cells.append((start, left)); cuts.append((left, right)); start = right
    cells.append((start, w))
    paths = []
    steps = 12
    for i in range(steps + 1):
        outer = contour(depth*i/steps) if i else [(0, 0), (w, 0), (w, h), (0, h)]
        inner = contour(depth*(i+1)/steps) if i < steps else []
        rise = (1-(i+.5)/steps)**2 if i < steps else 0
        for label, spans, floor, opacity in (("face", cells, base, 1), ("cut", cuts, 194/255, 155/255)):
            data = []
            for left, right in spans:
                slab = [(left, 0), (right, 0), (right, h), (left, h)]
                data += [polygon(clip_polygon(outer, slab)), polygon(clip_polygon(inner, slab)) if inner else ""]
            if any(data):
                luminance = floor + (1-floor)*rise
                path = svg_path(f"{label}{i}", " ".join(data), rule="evenodd",
                                fill=ui.solid([*(c*luminance for c in tint), alpha*opacity]))
                path["antialias"] = False
                paths.append(path)
    return paths


def gauge_surface(ident, kind, strogg, colour, alpha):
    return vec(ident, gauge_paint(kind, strogg, colour, alpha), {"display": ui.keyword("none")},
               mask={"paths": [svg_path("outline", ST_FILL if strogg else FILL, fill=ui.solid([1, 1, 1, 1]))]})


# ------------------------------------------------------------------ readouts

def digit_readout(doc: HudDocument, ident: str, value, *, face: str, size: float, right: float, baseline: float,
                  colour: list, digits: int = 3, pitch: float | None = None) -> dict:
    """Tabular figures (section 14.3): fixed cells from a fixed trailing
    edge, so the leading edge never moves as digits change. Leading zeros
    stay blank. `value` is an expression, clamped to 0..999."""
    pitch = pitch or round(0.62 * size, 3)
    value = op("clamp", op("floor", value), 0, 10 ** digits - 1)
    nodes = []
    for k in range(digits):
        cell_right = right - k * pitch
        node = text_box(f"{ident}-d{k}", "", face=face, size=size, colour=colour, baseline=baseline,
                        center=cell_right - pitch / 2, width=pitch * 1.8)
        del node["properties"]["color"]          # the readout's colour, for the gain pop
        node["properties"]["display"] = ui.keyword("block")
        nodes.append(node)
        doc.bind(f"{ident}-d{k}.text", node["id"], "text", op("numberText", op("%", op("floor", op("/", value, 10 ** k)), 10)))
        if k:
            doc.bind(f"{ident}-d{k}.show", node["id"], "display", sel(op(">=", value, 10 ** k), "block", "none"))
    return ui.group(ident, {**ui.FULL, "display": ui.keyword("block")}, nodes)


def legacy_readout(doc, ident, value, *, face, rect, scale, colour, shear=0):
    """The original text window, including its own shear origin and 2 u inset.
    The engine lays out the complete number using the legacy TTF metrics.
    """
    x, y, w, h = rect
    label = ui.label(ident+"-text", "#str_230007", {**ui.FULL, "text-align": ui.keyword("right")})
    label["legacyText"] = "simple" if ident.startswith("ammo-") else "window"
    doc.bind(ident+".text", label["id"], "text", op("numberText", op("max", 0, op("floor", value))))
    props = ui.typeface(face, 72*scale, 72*scale, colour)
    if shear:
        node = shear_group(ident,u(x),u(y),u(w),u(h),shear,[label],**props)
        for child in (node["children"][0], node["children"][0]["children"][0]):
            child["properties"].update(width=ui.length(100,"%"),height=ui.length(100,"%"))
        return node
    return box(ident,u(x),u(y),u(w),u(h),[label],**props)



def status_gain(doc, node, flash, rect, colour, scale, enlarged=None):
    """Animate the original window and textscale, not a whole-gauge bitmap."""
    x, y, w, h = rect
    gx, gy, gw, gh = enlarged or (x-w*.2, y-h*.2, w*1.2, h*1.2)
    tracks = [tr(node, key, [(0, ui.length(start)), (250, ui.length(end))])
              for key, start, end in (("left", u(gx), u(x)), ("top", u(gy), u(y)),
                                      ("width", u(gw), u(w)), ("height", u(gh), u(h)),
                                      ("font-size", 72*scale*1.2, 72*scale))]
    tracks += [tr(node, "color", [(0, ui.colour([1,1,1,1])), (250, ui.colour(colour))]),
               tr(flash, "opacity", [(0, num(1)), (250, num(0))])]
    return doc.timeline(node+"-gain" if node.startswith("ammo") else node.split("-")[0]+"-gain", 250, tracks)


def weapon_backdrop(doc, rect, tint):
    doc.string("oq4.weaponImage")
    x, y, w, h = rect
    node = ui.picture("ammo-weapon-bitmap", "", {**ui.absolute(left=u(x), top=u(y), width=u(w), height=u(h)),
                       "image-color": ui.colour(tint), "display": ui.keyword("block")}, fit="fill")
    doc.bind("ammo-weapon-image", node["id"], "image", S("oq4.weaponImage"))
    doc.show(node["id"], NOT(S("mp")))
    return full("ammo-renders", [node])


def gain_timeline(doc: HudDocument, ident: str, numerals: str, flash: str, pivot: tuple, size: tuple, ink: list) -> str:
    """The gain pop: the numerals rise to 120% in white and settle to 100%
    and the readout colour over 250 ms while the icon flashes white. Damage
    never animates a gauge. Reduced motion drops it (nonessential)."""
    px, py = pivot
    cx, cy = size[0] / 2, size[1] / 2

    def tf(s):
        return transform_value((px - cx) * (1 - s), (py - cy) * (1 - s), s, s)
    return doc.timeline(ident, 250, [
        tr(numerals, "transform", [(0, tf(1.2)), (250, tf(1.0))]),
        tr(numerals, "color", [(0, ui.colour([1, 1, 1, 1])), (250, ui.colour(ink))]),
        tr(flash, "opacity", [(0, num(1)), (250, num(0))]),
    ])


def glyph_choice(doc: HudDocument, ident: str, key: str, choices: list, x: float, y: float, h: float, *, tint,
                 opacity: float = 1.0, anchor: str = "start", extra=None) -> list:
    """One holder per possible glyph, shown while `key` names it."""
    nodes = []
    for name in choices:
        parts = hud_svg.icon_nodes(name, f"{ident}-{name}", x, y, h, tint=tint, opacity=opacity, anchor=anchor)
        holder = ui.group(f"{ident}-{name}-on", {**ui.FULL, "display": ui.keyword("none")}, parts)
        condition = op("==", S(key), name)
        doc.show(holder["id"], AND(condition, extra) if extra is not None else condition)
        nodes.append(holder)
    return nodes


def full(ident: str, children=(), mask: dict | None = None, **props) -> dict:
    """A group filling its parent; `mask` is a node mask (paths in dp)."""
    p = {**ui.FULL, "display": ui.keyword("block")}
    p.update(props)
    node = ui.group(ident, p, children)
    if mask is not None:
        node["mask"] = mask
    return node


# ------------------------------------------------------------------ gauges

def colour_variants(kind: str, strogg: bool, mp_variants: bool) -> list:
    """(name, colour, condition, track alpha, fill alpha) for each paint the
    gauge can take; paint is static, so each is its own node."""
    mp, low, team = S("mp"), S("oq4.low"), S("oq4.teamStrogg")
    # Keep the source GUI's floats, including the slightly darker health
    # colour installed by updateHealth and armor's 0.349 fill alpha.
    base = [1, .564, 0] if strogg else ([.701, .815, .431] if kind == "ammo" else [.698, .811, .427])
    out = []
    if kind == "health":
        out += [("sp", base, AND(NOT(mp), NOT(low)), 0.2, 0.35), ("low", [1, .3, 0], AND(NOT(mp), low), 0.35, 0.5)]
    else:
        out.append(("sp", base, NOT(mp), 0.2, .349 if kind == "armor" and not strogg else .35))
    if mp_variants:
        if kind == "ammo":
            out.append(("mpgrey", "#808080", AND(mp, OR(op("<", S("player_weapon"), 0), op(">", S("player_weapon"), 10))), 0.2, 0.5))
            out += [(f"mp{i}", c, AND(mp, op("==", S("player_weapon"), i)), 0.2, 0.5) for i, c in enumerate(MP_AMMO)]
        else:
            out += [("mpm", MP_TEAM["marine"], AND(mp, NOT(team)), 0.2, 0.5), ("mps", MP_TEAM["strogg"], AND(mp, team), 0.2, 0.5)]
    return out


def fill_window(doc: HudDocument, prefix: str, width: float, height: float, pct_key: str, nodes: list,
                lead_at_full: float, span: float, trailing: float) -> dict:
    """The trailing-anchored fill: a window whose leading edge sits at
    max(lead, trailing - span * pct) while its content stays put."""
    pct = op("clamp", S(pct_key), 0, 1)
    lead = op("max", lead_at_full, op("-", trailing, op("*", span, pct)))
    # RmlUi clips only content that overflows toward the trailing or bottom
    # side (scroll size beyond the client box), so the content box reaches
    # past the window's trailing edge; the fill paths themselves are absolute.
    # Each vector inside keeps the gauge's own box.
    nodes = [dict(node, properties={**node["properties"], **ui.absolute(left=0, top=0, width=u(width), height=u(height))})
             for node in nodes]
    content = ui.group(prefix + "-content", {**ui.absolute(left=0, top=0, width=u(width) + 600, height=u(height)),
                                             "display": ui.keyword("block")}, nodes)
    window = ui.group(prefix + "-window", {**ui.absolute(left=0, top=0, width=u(width), height=u(height)),
                                           "display": ui.keyword("block"), "overflow": ui.keyword("hidden")}, [content])
    doc.bind(prefix + "-window.left", window["id"], "left", op("*", lead, U))
    doc.bind(prefix + "-window.width", window["id"], "width", op("*", op("-", width, lead), U))
    doc.bind(prefix + "-content.left", content["id"], "left", op("*", op("-", 0, lead), U))
    return window


def ekg_layer(doc: HudDocument, prefix: str, mask_svg: str, bitmaps: list, low_colour: str, colour: str,
              *, width: float, height: float) -> dict:
    """Installed EKG bitmaps, added beneath the fill through its vector mask.
    Each (image, widths/second) scrolls repeating UVs over a stationary quad.
    The sheet covers the mask throughout the loop. Keep the whole
    texture period: the stock beats are not exactly identical. The EKG is an
    instrument, so reduced motion keeps its essential timelines running.
    User-approved bitmap exception: ui-visual-design.md section 14.3.
    """
    low = S("oq4.low")
    layers = []
    for variant, tint, shown in (("normal", colour, NOT(low)), ("low", low_colour, low)):
        for wave_index, (image, speed) in enumerate(bitmaps):
            ident = f"{prefix}-ekg-{variant}-{wave_index}"
            mover = ui.picture(ident, image,
                               {**ui.absolute(left=0, top=0, width=u(width), height=u(height)),
                                "image-blend": ui.keyword("additive"), "image-color": ui.colour(rgb(tint)),
                                "image-offset-x": num(0)}, fit="fill")
            duration = 1000 / speed
            run = doc.timeline(ident + "-run", duration,
                               [tr(ident, "image-offset-x", [(0, num(0)), (duration, num(-1))])],
                               iterations=0, essential=True)
            doc.joins.append(play(run))
            layers.append(mover)
        holder = full(f"{prefix}-ekg-{variant}", layers[-len(bitmaps):])
        doc.show(holder["id"], shown)
        layers = layers[:-len(bitmaps)] + [holder]
    return full(prefix + "-ekg", layers, mask={"paths": [svg_path("mask", mask_svg, fill=ui.solid([1, 1, 1, 1]))]})


def low_rim(doc: HudDocument, prefix: str, back_svg: str) -> dict:
    """Low health: an additive red glow along the inside of the backing,
    full at the edge and gone about 17 u in, its red pulsing between 100%
    and 50% at 2 Hz (backbar_add). Nested strokes clipped to the silhouette
    build the falloff."""
    paths = []
    steps = 8
    for i in range(steps):
        prev, reach = 17.0 * i / steps, 17.0 * (i + 1) / steps
        alpha = (1 - prev / 17.0) ** 2 - (1 - reach / 17.0) ** 2
        paths.append(svg_path(f"rim{i}", back_svg, stroke=stroke([1, 0, 0, round(alpha, 4)], u(reach * 2), join="round"),
                              blend="additive"))
    rim = vec(prefix + "-rim", paths, {"opacity": num(1)}, mask={"paths": [svg_path("mask", back_svg, fill=ui.solid([1, 1, 1, 1]))]})
    holder = full(prefix + "-rim-on", [rim])
    doc.show(holder["id"], S("oq4.low"))
    run = doc.timeline(prefix + "-rim-pulse", 500, [tr(rim["id"], "opacity", [(0, num(1)), (250, num(0.5)), (500, num(1))])],
                       iterations=0)
    doc.joins.append(play(run))
    return holder


def backing_nodes(doc: HudDocument, kind: str, outline: str) -> list:
    """The black backing at 0.50, or 0.85 with the opaque-backing option.
    Paint alpha, not node opacity, so no composition layer is needed."""
    normal = vec(kind + "-back", [svg_path("back", outline, fill=ui.solid([0, 0, 0, 0.5]))])
    opaque = vec(kind + "-back-opaque", [svg_path("back", outline, fill=ui.solid([0, 0, 0, 0.85]))], {"display": ui.keyword("none")})
    doc.show(normal["id"], NOT(S("oq4.opaque")))
    doc.show(opaque["id"], S("oq4.opaque"))
    return [normal, opaque]


def marine_gauge(doc: HudDocument, kind: str, gx: float, gy: float, *, mp_variants: bool) -> dict:
    """One Marine gauge at (gx, gy) u (atlas marineGauge; findings 1.2-1.4)."""
    w, h, k = 125, 59, -0.22
    mp = S("mp")
    plate = []
    plate += backing_nodes(doc, kind, BACK)

    tracks, fills = [], []
    for name, colour, shown, track_alpha, fill_alpha in colour_variants(kind, False, mp_variants):
        t = gauge_surface(f"{kind}-track-{name}", kind, False, colour, track_alpha)
        doc.show(t["id"], shown)
        tracks.append(t)
        f = gauge_surface(f"{kind}-fill-{name}", kind, False, colour, fill_alpha)
        doc.show(f["id"], shown)
        fills.append(f)
    plate += tracks
    if kind == "health":
        plate.append(ekg_layer(doc, "health", FILL, [("gfx/guis/hud/ekg.tga", .2)],
                               MARINE["ekg_low"], MARINE["ekg"], width=w, height=h))
    pct = {"ammo": "player_ammopct", "health": "player_healthpct", "armor": "player_armorpct"}[kind]
    plate.append(fill_window(doc, kind + "-fill", w, h, pct, fills, 5.4, 115, 120))
    if kind == "health":
        plate.append(low_rim(doc, "health", BACK))

    ink = rgb(MARINE["ink"])
    if kind == "ammo":
        clipped = AND(op(">=", S("player_clip_size"), 2), op(">=", S("player_ammo"), 0), NOT(mp))
        infinite = op("<", S("player_totalammo"), 0)
        clip = legacy_readout(doc, "ammo-clip", S("player_ammo"), face="chain", rect=(31,9,49,26), scale=.5, colour=ink, shear=k)
        reserve = legacy_readout(doc, "ammo-reserve", S("player_totalammo"), face="chain", rect=(78,12,40,18), scale=.32, colour=ink, shear=k)
        total = legacy_readout(doc, "ammo-total", S("player_totalammo"), face="chain", rect=(69,9,49,26), scale=.5, colour=ink, shear=k)
        doc.show(clip["id"], AND(clipped, NOT(infinite)))
        doc.show(reserve["id"], AND(clipped, NOT(infinite)))
        doc.show(total["id"], AND(NOT(clipped), NOT(infinite)))
        readouts = [clip, reserve, total]
    else:
        key = "player_health" if kind == "health" else "player_armor"
        readouts = [legacy_readout(doc, f"{kind}-num", S(key), face="chain", rect=(66,9,52,26), scale=.5, colour=ink, shear=k)]
    numerals = full(kind + "-numerals", readouts, transform=ui.transform(), color=ui.colour(ink))

    children = [shear_group(kind + "-plate", 0, 0, u(w), u(h), k, plate)]
    if kind == "ammo":
        children.append(weapon_backdrop(doc, (-2,15,106,26), [0.780,0.741,0.470,0.4]))
    children.append(numerals)

    # The notch icon, upright, with a white copy for the gain flash.
    if kind == "ammo":
        sp_icons = full("ammo-icon-sp", glyph_choice(doc, "ammo-icon", "ammoIcon", WEAPON_GLYPHS, u(100), u(38), u(16),
                                                     tint=MARINE["icon"]))
        doc.show(sp_icons["id"], NOT(mp))
        mp_icons = []
        for index, glyph in enumerate(MP_ROSTER):
            holder = full(f"ammo-icon-mp{index}", hud_svg.icon_nodes(glyph, f"ammo-icon-mpg{index}", u(100), u(38), u(16),
                                                                     tint=rgb(MP_AMMO[index])), display=ui.keyword("none"))
            doc.show(holder["id"], AND(mp, op("==", S("player_weapon"), index)))
            mp_icons.append(holder)
        flash = full("ammo-flash", glyph_choice(doc, "ammo-flash", "ammoIcon", WEAPON_GLYPHS, u(100), u(38), u(16),
                                                tint=[1, 1, 1, 1]), opacity=num(0))
        children += [sp_icons, full("ammo-icon-mp", mp_icons), flash]
    else:
        glyph = "health" if kind == "health" else "armor"
        icon = full(kind + "-icon", hud_svg.icon_nodes(glyph, kind + "-icon-g", u(98), u(38), u(16), tint=MARINE["icon"]))
        doc.show(icon["id"], NOT(mp))
        # Multiplayer: health yellow and armor red at 0.80 (hud.gui:1001, 1087).
        mp_tint = rgb("#F7C004", 0.8) if kind == "health" else rgb("#FA2B05", 0.8)
        icon_mp = full(kind + "-icon-mp", hud_svg.icon_nodes(glyph, kind + "-icon-mpg", u(98), u(38), u(16), tint=mp_tint))
        tiers = []
        if kind == "armor" and mp_variants:
            doc.number("player_armorTier", 0)
            tier = S("player_armorTier")
            doc.show(icon_mp["id"], AND(mp, *[op("!=", tier, value) for value, _name, _colour in CLASSIC_ARMOR_TINTS]))
            for value, name, colour in CLASSIC_ARMOR_TINTS:
                holder = full(f"armor-icon-mp-{name}", hud_svg.icon_nodes(glyph, f"armor-icon-mpg-{name}", u(98), u(38), u(16),
                                                                         tint=rgb(colour, 0.8)), display=ui.keyword("none"))
                doc.show(holder["id"], AND(mp, op("==", tier, value)))
                tiers.append(holder)
        else:
            doc.show(icon_mp["id"], mp)
        flash = full(kind + "-flash", hud_svg.icon_nodes(glyph, kind + "-flash-g", u(98), u(38), u(16), tint=[1, 1, 1, 1]),
                     opacity=num(0))
        children += [icon, icon_mp, *tiers, flash]
    if kind == "ammo":
        for node, rect, scale in ((clip,(31,9,49,26),.5),(reserve,(78,12,40,18),.32),(total,(69,9,49,26),.5)):
            status_gain(doc,node["id"],flash["id"],rect,ink,scale)
    else:
        status_gain(doc,readouts[0]["id"],flash["id"],(66,9,52,26),ink,.5,(64,7,58,31))
    return box(kind + "-gauge", u(gx), u(gy), u(w), u(h), children)


def strogg_gauge(doc: HudDocument, kind: str, gx: float, gy: float) -> dict:
    """One Strogg gauge at (gx, gy) u: 129x67 u, sheared +0.22 about its own
    centre, upright R_Strogg numerals, the icon at the upper-leading shoulder,
    a two-wave EKG and masked static (findings section 3)."""
    w, h, k = 129, 67, 0.22
    plate = []
    plate += backing_nodes(doc, kind, ST_BACK)
    tracks, fills = [], []
    for name, colour, shown, track_alpha, fill_alpha in colour_variants(kind, True, False):
        t = gauge_surface(f"{kind}-track-{name}", kind, True, colour, track_alpha)
        doc.show(t["id"], shown)
        tracks.append(t)
        f = gauge_surface(f"{kind}-fill-{name}", kind, True, colour, fill_alpha)
        doc.show(f["id"], shown)
        fills.append(f)
    plate += tracks
    if kind == "health":
        plate.append(ekg_layer(doc, "health", ST_FILL,
                               [("gfx/guis/hud/s_ekg.tga", .2), ("gfx/guis/hud/s_ekg2.tga", .38)],
                               STROGG["ekg_low"], STROGG["ekg"], width=w, height=h))
    pct = {"ammo": "player_ammopct", "health": "player_healthpct", "armor": "player_armorpct"}[kind]
    plate.append(fill_window(doc, kind + "-fill", w, h, pct, fills, 5.1, 119, 124))
    # This complex stock noise is a bitmap exception (design section 6),
    # loaded from the installation like the existing Strogg pause-page grain.
    # Repeat it across the entire swept area: the previous sparse dot sheet
    # moved out of the mask and left the gauge bare for most of each loop.
    # The two-second loop ends exactly four rows down and one column left,
    # so its restart has the same texels under every point of the mask.
    grain = [ui.picture(f"{kind}-static-r{row + 4}-c{column}", "gfx/guis/hud/s_static",
                        {**ui.absolute(left=u(column*w), top=u(row*h), width=u(w), height=u(h)),
                         "image-blend": ui.keyword("additive"),
                         "image-color": ui.colour(rgb(STROGG["static"]))}, fit="fill")
             for row in range(-4, 1) for column in range(2)]
    grain_node = full(kind + "-static", grain, transform=ui.transform())
    run = doc.timeline(kind + "-static-run", 2000, [tr(grain_node["id"], "transform",
                                                       [(0, transform_value(0, 0)), (2000, transform_value(u(-w), u(4 * h)))])],
                       iterations=0)
    doc.joins.append(play(run))
    plate.append(full(kind + "-static-mask", [grain_node],
                      mask={"paths": [svg_path("mask", ST_FILL, fill=ui.solid([1, 1, 1, 1]))]}))
    if kind == "health":
        plate.append(low_rim(doc, "health", ST_BACK))

    ink = rgb(STROGG["ink"])
    mp = S("mp")
    if kind == "ammo":
        clipped = AND(op(">=", S("player_clip_size"), 2), op(">=", S("player_ammo"), 0), NOT(mp))
        infinite = op("<", S("player_totalammo"), 0)
        clip = legacy_readout(doc, "ammo-clip", S("player_ammo"), face="r_strogg", rect=(59,11,60,26), scale=.5, colour=ink)
        reserve = legacy_readout(doc, "ammo-reserve", S("player_totalammo"), face="r_strogg", rect=(81,38,40,18), scale=.26, colour=rgb(STROGG["ink"],.8))
        total = legacy_readout(doc, "ammo-total", S("player_totalammo"), face="r_strogg", rect=(59,11,60,26), scale=.5, colour=ink)
        doc.show(clip["id"], AND(clipped, NOT(infinite)))
        doc.show(reserve["id"], AND(clipped, NOT(infinite)))
        doc.show(total["id"], AND(NOT(clipped), NOT(infinite)))
        readouts = [clip, reserve, total]
    else:
        key = "player_health" if kind == "health" else "player_armor"
        readouts = [legacy_readout(doc, f"{kind}-num", S(key), face="r_strogg", rect=(59 if kind == "health" else 57,10,60,26), scale=.5, colour=ink)]
    numerals = full(kind + "-numerals", readouts, transform=ui.transform(), color=ui.colour(ink))
    children = [shear_group(kind + "-plate", 0, 0, u(w), u(h), k, plate)]
    if kind == "ammo":
        children.append(weapon_backdrop(doc, (25,16,104,23), [0.776,0.737,0.466,0.4]))
    children.append(numerals)
    if kind == "ammo":
        icons = full("ammo-icon-sp", glyph_choice(doc, "ammo-icon", "ammoIcon", WEAPON_GLYPHS, u(12), u(14), u(16),
                                                  tint=STROGG["icon"]))
        flash = full("ammo-flash", glyph_choice(doc, "ammo-flash", "ammoIcon", WEAPON_GLYPHS, u(12), u(14), u(16),
                                                tint=[1, 1, 1, 1]), opacity=num(0))
        children += [icons, flash]
    else:
        glyph = "health" if kind == "health" else "armor"
        children.append(full(kind + "-icon", hud_svg.icon_nodes(glyph, kind + "-icon-g", u(12), u(14), u(16), tint=STROGG["icon"])))
        flash = full(kind + "-flash", hud_svg.icon_nodes(glyph, kind + "-flash-g", u(12), u(14), u(16), tint=[1, 1, 1, 1]),
                     opacity=num(0))
        children.append(flash)
    if kind == "ammo":
        for node, rect, scale, colour in ((clip,(59,11,60,26),.5,ink), (reserve,(81,38,40,18),.26,rgb(STROGG["ink"],.8)),
                                          (total,(59,11,60,26),.5,ink)):
            status_gain(doc,node["id"],flash["id"],rect,colour,scale)
    else:
        status_gain(doc,readouts[0]["id"],flash["id"],(59 if kind == "health" else 57,10,60,26),ink,.5,
                    (49,8,74,32) if kind == "health" else (54,8,69,32))
    return box(kind + "-gauge", u(gx), u(gy), u(w), u(h), children)


def air_gauge(doc: HudDocument, strogg: bool, *, classic: bool = False) -> dict:
    """An air instrument in the status bar's rounded, sheared HUD family."""
    doc.number("player_air", 100)
    amount = op("clamp", S("player_air"), 0, 100)
    palette = STROGG if strogg else MARINE
    low_air = op("<=", amount, 25)
    colour = [sel(low_air, low, normal) for low, normal in zip(rgb(palette["low"]), rgb(palette["fill"]))]
    outline = "M2 0H123Q125 0 125 2V14Q125 16 123 16H2Q0 16 0 14V2Q0 0 2 0Z"
    track = box("air-track", u(54), u(6), u(45), u(4),
                **{"background-color": ui.colour(rgb(palette["fill"], .2))})
    fill = box("air-fill", u(54), u(6), u(45), u(4),
               **{"background-color": ui.colour(rgb(palette["fill"]))})
    width = op("*", op("/", amount, 100), u(45))
    doc.bind("air-fill.width", fill["id"], "width", width)
    doc.bind("air-fill.left", fill["id"], "left", op("-", u(99), width))
    doc.bind("air-fill.colour", fill["id"], "background-color", colour)
    label = text_box("air-label", "#str_231326", face="lowpixel", size=u(7.5),
                     colour=rgb(palette["ink"]), baseline=u(12), x=u(5), width=u(46))
    value = text_box("air-value", "", face="r_strogg" if strogg else "chain", size=u(10.5),
                     colour=rgb(palette["ink"]), baseline=u(12.5), right=u(120), width=u(20))
    doc.bind("air-value.text", value["id"], "text", op("numberText", op("floor", amount)))
    doc.bind("air-value.colour", value["id"], "color",
             [sel(low_air, low, normal) for low, normal in zip(rgb(palette["low"]), rgb(palette["ink"]))])
    gauge = shear_group("air-gauge", u(190 if classic else 277.5 if strogg else 278), u(397), u(125), u(16), .22,
                        [*backing_nodes(doc, "air", outline), track, fill, label, value])
    doc.show("air-gauge", AND(op("<", amount, 100), op(">", S("player_health"), 0),
                              NOT(S("oq4.hidden")), NOT(S("oq4.ride"))))
    return gauge


def status_bar(doc: HudDocument, *, strogg: bool = False, mp_variants: bool = False) -> list:
    """The three gauges as one group centred on the bottom edge with the stock
    gaps: Marine 52 u and 11 u (438 u, ammo at 101 u), Strogg 51 u and 11 u
    (449 u). Positions never shift while armor comes and goes (14.4)."""
    for key in ("player_health", "player_healthpct", "player_armor", "player_armorpct", "player_ammo", "player_totalammo",
                "player_clip_size", "player_ammopct", "player_weapon", "player_healthDelta", "player_armorDelta"):
        doc.number(key, -1 if key in ("player_weapon",) else 0)
    doc.number("player_healthpct", 1)
    doc.string("ammoIcon")
    doc.string("weapicon")
    doc.boolean("mp")
    doc.boolean("oq4.low")
    doc.boolean("oq4.teamStrogg")
    doc.boolean("oq4.hidden")
    if strogg:
        gauges = [strogg_gauge(doc, "ammo", 95.5, 419), strogg_gauge(doc, "health", 275.5, 419), strogg_gauge(doc, "armor", 415.5, 419)]
    else:
        gauges = [marine_gauge(doc, "ammo", 101, 420, mp_variants=mp_variants),
                  marine_gauge(doc, "health", 278, 420, mp_variants=mp_variants),
                  marine_gauge(doc, "armor", 414, 420, mp_variants=mp_variants)]
    # A ride's silhouette gauge (the flatbed, the tram) replaces all three.
    doc.boolean("oq4.ride")
    riding = S("oq4.ride")
    doc.show("armor-gauge", AND(op(">", S("player_armor"), 0), NOT(S("oq4.hidden")), NOT(riding)))
    doc.show("ammo-gauge", AND(NOT(S("oq4.hidden")), NOT(riding)))
    doc.show("health-gauge", AND(NOT(S("oq4.hidden")), NOT(riding)))
    # Low health: below 26% of the maximum (25% on the Strogg HUD), set from
    # the same updateHealth event the stock scripts read.
    threshold = 0.25 if strogg else 0.26
    doc.on("updateHealth",
           {"op": "setState", "values": {"oq4.low": op("<", S("player_healthpct"), threshold)}},
           {"op": "if", "condition": op("<", S("player_healthDelta"), 0), "then": [play("health-gain")]})
    doc.on("updateArmor", {"op": "if", "condition": op("<", S("player_armorDelta"), 0), "then": [play("armor-gain")]})
    # The engine compares complete readouts after the game's state batch.
    # Inventory pulses can describe another weapon and never drive this pop.
    doc.on("ammoPulse")
    doc.on("ammoClipGain", play("ammo-clip-gain"))
    doc.on("ammoReserveGain", play("ammo-reserve-gain"))
    doc.on("ammoTotalGain", play("ammo-total-gain"))
    doc.on("setTeam_marine", {"op": "setState", "values": {"oq4.teamStrogg": False}})
    doc.on("setTeam_strogg", {"op": "setState", "values": {"oq4.teamStrogg": True}})
    doc.joins.append({"op": "setState", "values": {"oq4.low": op("<", S("player_healthpct"), threshold)}})
    return [*gauges, air_gauge(doc, strogg)]
