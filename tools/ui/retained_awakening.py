"""The Awakening's in-game menus (docs/dev/ui-visual-design.md sections 2, 4,
6, 8 and 13.7, versions 1.40 to 1.45; the atlas sheet
docs/dev/ui/atlas/awakening-menu.html is the reference drawing).

Built by tools/ui/build_retained_screens.py with the other retained screens.
While The Awakening is the running campaign and the player is not Strogg, the
session presents pause_awakening.rui in place of pause.rui, and the pause
hands off to loadgame_awakening.rui, savegame_awakening.rui and
restartlevel_awakening.rui. Each is the Marine document it replaces, built by
the same code, then carried into the family here:

- the Awakening tokens of section 4 replace the Marine colors everywhere,
  timelines included, while marine.orange stays the one interaction color;
- the Valkaryne frames of section 6: horned framing bands whose small steps
  are 30-degree flanks, with fullers and the rime line; blade ends on the
  navigation, section and action plates and the plinth; a horned corner on
  the level block and the confirmations; a needle rule; a lens around the
  focused marker;
- seeded frost on the bands only, fixed to the view while the bands move,
  carved around each level's band text; snow behind the instruments; the low
  haze;
- the Awakening motion of section 8: the bands close in as the menu opens
  and the frost grows; bands, levels and the carried title glide on one
  curve; the bottom band leads to a page and the top band follows 40 ms
  later; the home level scrolls back in from the trailing side; the rime
  line brightens with a band's speed; a page scrolls in from the leading
  side as it arrives.
"""
from __future__ import annotations

import colorsys
import copy
import math

from build_retained_screens import (PAK0, ACCEL, BOTTOM_TO_PAGE, CANVAS_W, EASE_OUT, FULL, PAGE_BAR_LEFT, TOP_TO_PAGE, U, absolute,
                                    colour, edge, group, keyword, label, length, linear, merged_text, number, solid, stroke,
                                    text_width, track, transform, typeface, vector, vx, walk_nodes)

# Section 4 tokens.
RAIL = "#78DEFF"          # awakening.rail and awakening.value
GLACIER = "#289EB7"       # light plates, markers, header bands, rules
RAIL_CARD = "#3AAAC4"
RIM = "#2098BB"
RIME = "#EBFAFF"
GLOW = "#1E6B85"
GLOW_MODAL = "#154C5E"
DEEP = "#040E1A"
PLINTH = "#1B4656"
PICTURE = "#17414F"
TEXT = "#EEF8FF"
TERTIARY = "#7E99A5"
FROST = "#D1EDFF"
SNOW = "#EBF6FF"
HAZE = "#AAD7F0"

# The Marine tokens each replaces (section 4), by RGB.
TOKENS = {
    "#CCCC51": RAIL, "#FFBE23": RAIL, "#909A49": GLACIER, "#8B964B": GLACIER, "#A0A040": RAIL_CARD, "#626934": RIM,
    "#616F26": GLOW, "#454E1B": GLOW_MODAL, "#46501B": GLOW_MODAL, "#3E4A21": PLINTH, "#414624": PICTURE,
    "#B0CD6B": RAIL, "#FF8000": RAIL, "#8C947A": TERTIARY, "#FFFFFF": TEXT, "#B8C29E": "#A9C9D6",
}
KEEP = {"#B5C784", "#E38900", "#FF9C00", "#000000"}   # a done objective's check, the interaction oranges, black

# Motion (section 8, Awakening motion).
GLIDE = [0.65, 0.0, 0.35, 1.0]      # frame.glide
CLOSE = [0.16, 1.0, 0.3, 1.0]       # frame.close and level.arrive
SWELL = [0.45, 0.0, 0.55, 1.0]      # the rime line's rise and fall with a band's speed
RIME_REST, RIME_LIT = 0.5, 1.0      # node opacity over a 0.90 line: 0.45 at rest, 0.90 at speed
CLOSE_IN = 30.0                     # u: how far beyond their places the bands start as the menu opens
ARRIVE = 32.0                       # u: the last stretch an arriving level scrolls in
FROST_MS, CLOSE_MS, ARRIVE_MS = 900, 400, 250
FROST_BINS = 6                      # frost.grow: the crystal front in six steps of reach

# The sweep (section 6): a 30-degree arc leaving one edge at 45 degrees and reaching the other at 15.
A1, A2 = math.radians(45), math.radians(15)
KX = (math.sin(A1) - math.sin(A2)) / (math.cos(A2) - math.cos(A1))
KR = 1 / (math.cos(A2) - math.cos(A1))
T30 = math.sqrt(3)
SEED = 1414


def hexrgb(value: str) -> tuple:
    return tuple(int(value[i:i + 2], 16) / 255 for i in (1, 3, 5))


def tint(hex_colour: str, alpha: float = 1.0) -> list:
    return [round(c, 4) for c in hexrgb(hex_colour)] + [alpha]


# ----------------------------------------------------------------- the palette

def remap(value: list) -> list:
    """A Marine color's Awakening token, by RGB; other olive and khaki tints
    turn to the same hue of ice, keeping their lightness and saturation."""
    r, g, b, a = value
    key = "#%02X%02X%02X" % tuple(round(c * 255) for c in (r, g, b))
    if key in KEEP:
        return value
    for marine, awakening in TOKENS.items():
        if all(abs(c - m) < 0.006 for c, m in zip((r, g, b), hexrgb(marine))):
            return tint(awakening, a)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if 40 / 360 < h < 110 / 360 and s > 0.12:
        nr, ng, nb = colorsys.hsv_to_rgb(195 / 360, s, v)
        return [round(nr, 4), round(ng, 4), round(nb, 4), a]
    return value


def recolour(node) -> None:
    """Every color value under `node` (documents, timelines, bindings), in place."""
    if isinstance(node, dict):
        if node.get("type") == "color" and isinstance(node.get("value"), list) and len(node["value"]) == 4 \
                and all(isinstance(c, (int, float)) for c in node["value"]):
            node["value"] = remap(node["value"])
            return
        for value in node.values():
            recolour(value)
    elif isinstance(node, list):
        for value in node:
            recolour(value)


def deepen(paint: dict) -> None:
    """Black in a plate's or card's paint becomes awakening.deep."""
    for item in walk_paint(paint):
        value = item["value"]
        if value[:3] == [0, 0, 0] or all(abs(c) < 1e-6 for c in value[:3]):
            item["value"] = tint(DEEP, value[3])


def walk_paint(paint):
    if isinstance(paint, dict):
        if paint.get("type") == "color":
            yield paint
        else:
            for value in paint.values():
                yield from walk_paint(value)
    elif isinstance(paint, list):
        for value in paint:
            yield from walk_paint(value)


# ------------------------------------------------------------------- geometry

def arc_cubic(x1, y1, r, sweep_flag, x2, y2):
    """An SVG arc (small, of radius r) as one cubic: its two control points."""
    hx, hy = (x1 - x2) / 2, (y1 - y2) / 2
    d2 = hx * hx + hy * hy
    # SVG's endpoint form with the large-arc flag 0: the center's sign
    # follows the sweep flag.
    coef = math.sqrt(max(0.0, (r * r - d2) / d2)) * (1 if sweep_flag == 1 else -1)
    cx, cy = coef * hy + (x1 + x2) / 2, -coef * hx + (y1 + y2) / 2
    t1, t2 = math.atan2(y1 - cy, x1 - cx), math.atan2(y2 - cy, x2 - cx)
    dt = t2 - t1
    if sweep_flag == 1 and dt < 0:
        dt += 2 * math.pi
    if sweep_flag == 0 and dt > 0:
        dt -= 2 * math.pi
    k = 4 / 3 * math.tan(dt / 4)
    return (x1 - k * r * math.sin(t1), y1 + k * r * math.cos(t1)), (x2 + k * r * math.sin(t2), y2 - k * r * math.cos(t2))


def sweep(x, y, f, g, c, steep):
    """The atlas's sweep from (x, y): its end and an ('A', r, flag, x, y) op."""
    ex, ey = x + f * KX * c, y + g * c
    return ex, ey, ("A", KR * c, 0 if ((f * g > 0) == steep) else 1, ex, ey)


class Shape:
    """A path in one coordinate frame: x is dp from an anchor (a fraction of
    the box, 0 by default), y is dp from the top. Ops: M, L, A (radius, sweep
    flag, x, y), C (x1, y1, x2, y2, x, y), Z."""

    def __init__(self, fraction: float = 0.0):
        self.fraction = fraction
        self.ops: list = []

    def x(self, value):
        return round(value, 2) if self.fraction == 0 else {"fraction": self.fraction, "dp": round(value, 2)}

    def commands(self) -> list:
        out, at = [], (0.0, 0.0)
        for index, op in enumerate(self.ops):
            name = f"c{index}"
            if op[0] == "M":
                out.append({"id": name, "op": "move", "points": [[self.x(op[1]), round(op[2], 2)]]}); at = op[1:3]
            elif op[0] == "L":
                out.append({"id": name, "op": "line", "points": [[self.x(op[1]), round(op[2], 2)]]}); at = op[1:3]
            elif op[0] == "A":
                (ax, ay), (bx, by) = arc_cubic(at[0], at[1], op[1], op[2], op[3], op[4])
                out.append({"id": name, "op": "cubic", "points": [[self.x(ax), round(ay, 2)], [self.x(bx), round(by, 2)],
                                                                   [self.x(op[3]), round(op[4], 2)]]}); at = op[3:5]
            elif op[0] == "C":
                out.append({"id": name, "op": "cubic", "points": [[self.x(op[1]), round(op[2], 2)], [self.x(op[3]), round(op[4], 2)],
                                                                   [self.x(op[5]), round(op[6], 2)]]}); at = op[5:7]
            elif op[0] == "Z":
                out.append({"id": name, "op": "close"})
        return out


def shape_path(ident: str, shape: Shape, **paint) -> dict:
    result = {"id": ident, "commands": shape.commands()}
    for key in ("fill", "stroke", "blend"):
        if paint.get(key) is not None:
            result[key] = paint[key]
    return result


def circle(shape: Shape, cx: float, cy: float, r: float) -> None:
    k = 0.5523 * r
    shape.ops += [("M", cx + r, cy), ("C", cx + r, cy + k, cx + k, cy + r, cx, cy + r), ("C", cx - k, cy + r, cx - r, cy + k, cx - r, cy),
                  ("C", cx - r, cy - k, cx - k, cy - r, cx, cy - r), ("C", cx + k, cy - r, cx + r, cy - k, cx + r, cy), ("Z",)]


# --------------------------------------------------------------- the bands

FAR_U = 1400 / U + 200   # u past the canvas the bands' flat ends run


def band_edges() -> tuple:
    """The Awakening bands' inner edges at home, in u on the 4:3 canvas
    (section 6 and the atlas's sweptEdges): ops for the top and bottom edge,
    and the fullers (point x, y, run direction f, g, length)."""
    lead, trail = -CANVAS_W / U / 2 - FAR_U + 320, CANVAS_W / U / 2 + FAR_U + 320
    top = [("M", lead, 89.7), ("L", -297.9, 89.7), ("L", -297.9 + 14.1 * T30, 103.8)]
    for n in (30.7, 127.6, 223.5):
        top += [("L", n + 10.2 - 9.1 * T30, 103.8), ("L", n + 10.2, 94.7), ("L", n + 45.9, 94.7), ("L", n + 45.9 + 9.1 * T30, 103.8)]
    tx = 315.4 + KX * 35.3
    top += [("L", tx, 103.8), sweep(tx, 103.8, -1, -1, 35.3, False)[2], ("L", trail, 68.5)]
    fullers = [("top", tx, 103.8, -1, -1, KX * 35.3)]
    bottom = [("M", lead, 421.5), ("L", -222.5 - 9.1 * T30, 421.5), ("L", -222.5, 430.6), ("L", 2.1, 430.6),
              sweep(2.1, 430.6, -1, -1, 30.2, True)[2]]
    fullers.append(("bottom", 2.1 - KX * 30.2, 400.4, 1, 1, KX * 30.2))
    tx = 344.9 + KX * 43.3
    bottom += [("L", tx, 400.4), sweep(tx, 400.4, -1, 1, 43.3, False)[2], ("L", 582.7, 443.7), ("L", 582.7 + 9.1 * T30, 434.6),
               ("L", trail, 434.6)]
    fullers.append(("bottom", tx, 400.4, -1, 1, KX * 43.3))
    return top, bottom, fullers


def view_shape(ops: list) -> Shape:
    """Canvas-u ops as a Shape in a node spanning the whole view (centred on the 4:3 canvas)."""
    shape = Shape(0.5)
    for op in ops:
        if op[0] in ("M", "L"):
            shape.ops.append((op[0], -CANVAS_W / 2 + U * op[1], U * op[2]))
        elif op[0] == "A":
            shape.ops.append(("A", U * op[1], op[2], -CANVAS_W / 2 + U * op[3], U * op[4]))
        else:
            shape.ops.append(op)
    return shape


def edge_strokes(ident: str, ops: list, colour_hex: str, peak: float, reach_u: float, steps: int = 10) -> list:
    """marine.rim's construction (section 6): nested strokes of equal alpha
    along an edge whose accumulated alpha falls near-linearly from `peak` at
    the edge to nothing `reach_u` away."""
    alpha = 1 - (1 - peak) ** (1 / steps)
    return [shape_path(f"{ident}-{index}", view_shape(ops), stroke=stroke(solid(tint(colour_hex, round(alpha, 4))),
                                                                          2 * reach_u * U * index / steps, minimum=0, join="round"))
            for index in range(steps, 0, -1)]


def band_fill(ops: list, top: bool) -> list:
    lead_x, trail_x = ops[0][1], ops[-1][1]
    far = -400 / U if top else 480 + 400 / U
    return ops + [("L", trail_x, far), ("L", lead_x, far), ("Z",)]


# ------------------------------------------------------------------ the frost

class Mulberry:
    """mulberry32, as the atlas seeds it, so a rebuild grows the same frost."""

    def __init__(self, seed: int):
        self.a = seed & 0xFFFFFFFF

    def __call__(self) -> float:
        self.a = (self.a + 0x6D2B79F5) & 0xFFFFFFFF
        a = self.a
        t = ((a ^ (a >> 15)) * (1 | a)) & 0xFFFFFFFF
        t = ((t + (((t ^ (t >> 7)) * (61 | t)) & 0xFFFFFFFF)) & 0xFFFFFFFF) ^ t
        return ((t ^ (t >> 14)) & 0xFFFFFFFF) / 4294967296


def grow(rand, seeds: list) -> list:
    """Section 6's crystals from (x, y, heading in degrees, length, width) seeds:
    3 u steps wandering 6.9 degrees, thinning 3% a step to 0.5 u, a 28% chance
    a step of a branch at 60 +- 8.6 degrees, 25-50% as long and 70% as wide,
    to four levels. Returns branches as (points, widths, base, alpha)."""
    def normal(s):
        u, v = max(1e-9, rand()), rand()
        return math.sqrt(-2 * math.log(u)) * math.cos(2 * math.pi * v) * s

    def uni(a, b):
        return a + (b - a) * rand()
    out = []

    def branch(x, y, ang, length_u, width, depth, base):
        if depth > 4 or length_u < 2:
            return
        pts, wid, kids, travelled = [(x, y)], [width], [], 0.0
        for _ in range(int(length_u // 3) + 1):
            ang += normal(math.radians(6.9))
            x += math.cos(ang) * 3; y += math.sin(ang) * 3; travelled += 3
            width = max(0.5, width * 0.97)
            pts.append((x, y)); wid.append(width)
            if rand() < 0.28:
                side = -1 if rand() < 0.5 else 1
                kids.append((x, y, ang + side * (math.radians(60) + normal(math.radians(8.6))), length_u * uni(0.25, 0.5), width * 0.7,
                             depth + 1, base + travelled))
        wid[-1] = 0.0
        out.append((pts, wid, base, uni(0.59, 0.98)))
        for kid in kids:
            branch(*kid)
    for x, y, heading, length_u, width in seeds:
        branch(x, y, math.radians(heading + normal(28.6 if abs(heading) == 90 else 8)), length_u, width, 0, 0.0)
    return out


def frost_seeds(rand, top: bool) -> tuple:
    """Seeds on the view's top (or bottom) edge: mid-span ones every 48 u
    (varied 30%) across the widest view, anchored to the canvas; and, within
    200 u of each of the edge's two corners, denser ones anchored to that
    corner, with three more on the corner itself (section 6). Returns the
    canvas-anchored seeds and the leading and trailing corners' seeds, each
    in u from its anchor."""
    def uni(a, b):
        return a + (b - a) * rand()
    y, inward = (0.0, 90) if top else (480.0, -90)
    middle, x = [], -560.0
    while x < 1200:
        middle.append((x, y, inward, uni(20, 75) * 0.55, uni(0.75, 1.6)))
        x += 48 * uni(0.7, 1.3)
    corners = []
    for side in (1, -1):                        # leading corner runs toward +x, trailing toward -x
        seeds, d = [], uni(2, 8)
        while d < 200:
            w = (1 - d / 200) ** 2
            seeds.append((side * d, y, inward, uni(20, 75) * (0.55 + 0.45 * w), uni(0.75, 1.6)))
            d += (48 - 36 * w) * uni(0.7, 1.3)
        base = 0 if side == 1 else 180
        for angle in (20, 45, 70):
            heading = (angle if top else -angle) if side == 1 else (180 - angle if top else 180 + angle)
            seeds.append((0.0, y, heading, uni(45, 75), uni(1.1, 1.6)))
        corners.append(seeds)
    return middle, corners[0], corners[1]


def ribbon_pieces(branch, cuts: list) -> list:
    """A branch's tapered ribbon cut into pieces by distance from its root:
    [(bin, polygon points)]."""
    pts, wid, base, _ = branch
    dist = [base + 3 * i for i in range(len(pts))]
    pieces = []
    for index in range(len(cuts) - 1):
        a, b = max(cuts[index], dist[0]), min(cuts[index + 1], dist[-1])
        if b - a < 0.2:
            continue
        def at(s):
            i = min(len(pts) - 2, max(0, int((s - dist[0]) // 3)))
            f = (s - dist[i]) / 3
            return (pts[i][0] + (pts[i + 1][0] - pts[i][0]) * f, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * f,
                    wid[i] + (wid[i + 1] - wid[i]) * f, pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
        # every other 3 u step: at a crystal's width the outline cannot tell
        samples = [a] + [d for i, d in enumerate(dist) if a < d < b and i % 2 == 0] + [b]
        left, right = [], []
        for s in samples:
            x, y, w, dx, dy = at(s)
            n = math.hypot(dx, dy) or 1
            nx, ny = -dy / n * w / 2, dx / n * w / 2
            left.append((x + nx, y + ny)); right.append((x - nx, y - ny))
        pieces.append((index, left + right[::-1]))
    return pieces


def frost_layers(prefix: str, top: bool) -> tuple:
    """The frost of one band as FROST_BINS vectors of view-anchored ribbons,
    revealed in order of reach as frost.grow plays. Returns the vectors and
    the bins' reach fractions."""
    rand = Mulberry(SEED if top else SEED + 2)
    middle, lead, trail = frost_seeds(rand, top)
    groups = [("middle", grow(rand, middle)), ("lead", grow(rand, lead)), ("trail", grow(rand, trail))]
    longest = max(b[2] + 3 * (len(b[0]) - 1) for _, branches in groups for b in branches)
    cuts = [longest * i / FROST_BINS for i in range(FROST_BINS)] + [longest + 1]
    layers = []
    for index in range(FROST_BINS):
        parts = []
        for anchor, branches in groups:
            box, offset = BOXES[anchor]
            paths = []
            for branch in branches:
                for piece, polygon in ribbon_pieces(branch, cuts):
                    if piece != index:
                        continue
                    shape = Shape(0.0)
                    shape.ops = [("M" if i == 0 else "L", offset + U * x, U * y) for i, (x, y) in enumerate(polygon)] + [("Z",)]
                    paths.append(shape_path(f"r{len(paths)}", shape, fill=solid(tint(FROST, round(0.8 * branch[3], 2)))))
            if paths:
                parts.append(vector(f"{prefix}-{index}-{anchor}", {**box, "pointer-events": keyword("none")}, paths))
        layers.append(group(f"{prefix}-{index}", {**FULL, "opacity": number(1), "pointer-events": keyword("none")}, parts))
    return layers


# Boxes that let generated art use plain dp: the canvas widened 600 u to
# either side (art in canvas u), and 400 dp at each of the view's edges (art
# in dp from the leading edge, or back from the trailing one).
WIDE = 600.0
BOXES = {
    "middle": ({**absolute(top=0, height=720, width=CANVAS_W + 2 * U * WIDE), "left": length(50, "%"),
                "margin-left": length(-CANVAS_W / 2 - U * WIDE)}, U * WIDE),
    "lead": (absolute(left=0, top=0, width=400, height=720), 0.0),
    "trail": (absolute(right=0, top=0, width=400, height=720), 400.0),
}


def carve(ident: str, x_fraction: float, left_dp: float, top_dp: float, width_dp: float, height_dp: float, opacity: float) -> dict:
    """A carved slot (section 6): the item's box with 6 u clear, feathered
    over a further 6 u, in black over the frost."""
    paths = []
    for step in range(6, -1, -1):
        grow_by = U * (6 + step)
        shape = Shape(x_fraction)
        x0, y0, x1, y1 = left_dp - grow_by, top_dp - grow_by, left_dp + width_dp + grow_by, top_dp + height_dp + grow_by
        shape.ops = [("M", x0, y0), ("L", x1, y0), ("L", x1, y1), ("L", x0, y1), ("Z",)]
        paths.append(shape_path(f"s{step}", shape, fill=solid([0, 0, 0, 1.0 if step == 0 else 0.25])))
    return vector(ident, {**FULL, "opacity": number(opacity), "pointer-events": keyword("none")}, paths)


PAUSE_CARVES = {"top": [("carve-title", 0.5, -CANVAS_W / 2 + U * 37, U * 19, U * 214, U * 18.3, 0)],
                "bottom": [("carve-home", 1.0, -U * 172, U * 447, U * 160, 22, 1),
                           ("carve-page", 0.5, -CANVAS_W / 2 + PAGE_BAR_LEFT, U * 441, CANVAS_W - PAGE_BAR_LEFT - U * 8, 45, 0)]}
# A page's band text: the crumbs and the title in the top band's thin span,
# and the page's prompt bar in the bottom band.
PAGE_CARVES = {"top": [("carve-title", 0.5, -CANVAS_W / 2 + U * 37, U * 12, U * 330, U * 25.3, 1)],
               "bottom": [("carve-page", 0.5, -CANVAS_W / 2 + PAGE_BAR_LEFT, U * 441, CANVAS_W - PAGE_BAR_LEFT - U * 8, 45, 1)]}


def awakening_band(prefix: str, top: bool, edge_ops: list, fullers: list, base: dict | None = None,
                   carves: dict | None = None) -> dict:
    """One Awakening band (section 6): its rim light, the opaque band, the
    fullers, the frost held to the view under the band's silhouette and
    fading out 6 u inside its edge, and the rime line. The group keeps the
    Marine band's id, so the stock motion moves it; `<id>-frost` carries the
    opposite motion so the frost stays where the view holds it."""
    ident = f"{prefix}-{'top' if top else 'bottom'}"
    fill_ops = band_fill(edge_ops, top)
    art = vector(f"{ident}-art", dict(FULL), [
        *edge_strokes("rim", edge_ops, RIM, 0.37, 19.0),
        shape_path("band", view_shape(fill_ops), fill=solid([0, 0, 0, 1])),
        *[fuller(index, f) for index, f in enumerate(fullers)],
    ])
    frost = group(f"{ident}-frost", {**FULL, "transform": transform(), "pointer-events": keyword("none")}, [
        vector(f"{ident}-wash", dict(FULL), [shape_path("wash", wash_shape(top), fill=linear(
            (0, 0 if top else U * 466), (0, U * 14 if top else U * 480),
            [(0, tint(FROST, 0.1 if top else 0)), (1, tint(FROST, 0 if top else 0.1))]))]),
        *frost_layers(f"{ident}-crystals", top),
        *[carve(f"{ident}-{name}", *spec) for name, *spec in (carves or PAUSE_CARVES)["top" if top else "bottom"]],
    ])
    base = base or transform()
    frost["properties"]["transform"] = opposite(base)
    feather = vector(f"{ident}-feather", dict(FULL), edge_strokes("fade", edge_ops, "#000000", 0.98, 6.0, steps=8))
    held = group(f"{ident}-held", {**FULL, "pointer-events": keyword("none")}, [frost, feather],
                 mask={"paths": [shape_path("band", view_shape(fill_ops), fill=solid([1, 1, 1, 1]))]})
    rime = vector(f"{ident}-rime", {**FULL, "opacity": number(RIME_REST)}, [
        shape_path("rime", view_shape(edge_ops), stroke=stroke(solid(tint(RIME, 0.9)), U * 0.5, minimum=1))])
    band = group(ident, {**FULL, "transform": base, "pointer-events": keyword("none")}, [art, held, rime])
    if not top:
        anchor_bottom(band)
    return band


def _foot(value):
    """A y in dp from the canvas top as dp from the view's foot."""
    return {"fraction": 1, "dp": round(value - 720, 2)} if isinstance(value, (int, float)) else value


def anchor_bottom(node) -> None:
    """The bottom band follows the view's foot, as fit_narrow anchors the
    Marine one: in a view narrower than 4:3 the canvas grows taller than
    720 dp. Every y under `node` (paths, masks, paints) measures from the
    foot, and generated art in a 720 dp box sits on it."""
    if isinstance(node, dict):
        if "points" in node and isinstance(node["points"], list):
            for point in node["points"]:
                if isinstance(point, list) and len(point) == 2:
                    point[1] = _foot(point[1])
        for key in ("from", "to"):
            if isinstance(node.get(key), list) and len(node[key]) == 2:
                node[key][1] = _foot(node[key][1])
        props = node.get("properties")
        if isinstance(props, dict) and props.get("top", {}).get("value") == 0 and props.get("height", {}).get("value") == 720:
            del props["top"]
            props["bottom"] = length(0)
        for value in node.values():
            if isinstance(value, (dict, list)):
                anchor_bottom(value)
    elif isinstance(node, list):
        for value in node:
            anchor_bottom(value)


def wash_shape(top: bool) -> Shape:
    """The rime wash's strip along the view's top or bottom edge, 14 u deep."""
    return _span(0, U * 14) if top else _span(U * 466, U * 480)


def _span(y0: float, y1: float) -> Shape:
    class Span(Shape):
        def commands(self):
            return [{"id": "a", "op": "move", "points": [[{"fraction": 0}, y0]]},
                    {"id": "b", "op": "line", "points": [[{"fraction": 1}, y0]]},
                    {"id": "c", "op": "line", "points": [[{"fraction": 1}, y1]]},
                    {"id": "d", "op": "line", "points": [[{"fraction": 0}, y1]]},
                    {"id": "e", "op": "close"}]
    return Span()


def fuller(index: int, spec: tuple) -> dict:
    """A fuller: 0.5 u of awakening.glacier from a horn's point back along
    the middle of its blade at 7.5 degrees, as long as the horn, 0.50 at the
    point and fading out."""
    _, x, y, f, g, run = spec
    ex, ey = x + f * run * math.cos(A2 / 2), y + g * run * math.sin(A2 / 2)
    shape = view_shape([("M", x, y), ("L", ex, ey)])
    start, end = (vx(x), U * y), (vx(ex), U * ey)
    return shape_path(f"fuller-{index}", shape, stroke=stroke(linear(start, end, [(0, tint(GLACIER, 0.5)), (1, tint(GLACIER, 0))]),
                                                              U * 0.5, minimum=1))


# ------------------------------------------------------------------- controls

def lens_vector(ident: str, cx: float, cy: float, props: dict) -> dict:
    """The lens (section 6): rings of 6.6 and 9.4 u in marine.orange at 0.90
    and 0.45, and an additive halo 13 u in radius, 0.35 at its center."""
    paths = []
    for step in range(5):
        r = U * 13 * (5 - step) / 5
        halo = Shape(0.0); circle(halo, cx, cy, r)
        paths.append(shape_path(f"halo-{step}", halo, fill=solid(tint("#E38900", 0.08)), blend="additive"))
    for name, r, w, a in (("inner", 6.6, 0.9, 0.9), ("outer", 9.4, 0.45, 0.45)):
        ring = Shape(0.0); circle(ring, cx, cy, U * r)
        paths.append(shape_path(name, ring, stroke=stroke(solid(tint("#E38900", a)), U * w, minimum=1, join="round")))
    return vector(ident, {**props, "opacity": number(0), "pointer-events": keyword("none")}, paths)


def light_lens(doc: dict, control: dict, lens: dict) -> None:
    """The lens appears and leaves with the stock focus response: it joins
    the control's hover, focus and pressed states."""
    children = control.setdefault("children", [])
    plate = next((i for i, c in enumerate(children) if c["id"].endswith("-plate")), -1)
    children.insert(plate + 1, lens)
    timelines = {t["id"]: t for t in doc["timelines"]}
    for state, ident in control["control"]["states"].items():
        timeline = timelines[ident]
        shown = number(1 if state in ("hover", "focus", "pressed") else 0)
        ease = None if state == "default" else EASE_OUT
        timeline["tracks"].append(track(lens["id"], "opacity", [(0, shown, ease), (timeline["durationMs"], shown)]))


def blade(points: list, top: float, bottom: float, end: float, fraction: float = 0.0) -> Shape:
    """A plate whose open trailing end is one sweep from its top edge down to
    its rail at `end` (section 6). `points` are the plate's other corners
    after the bottom edge's start, as (x, y), leading back to the top."""
    height = bottom - top
    shape = Shape(fraction)
    start = end - KX * height
    shape.ops = [("M", points[-1][0], top), ("L", start, top), sweep(start, top, 1, 1, height, True)[2]]
    shape.ops += [("L", x, y) for x, y in points] + [("Z",)]
    return shape


def find(root: dict, ident: str) -> dict | None:
    return next((n for n in walk_nodes(root) if n.get("id") == ident), None)


def parent_of(root: dict, ident: str):
    for node in walk_nodes(root):
        for index, child in enumerate(node.get("children", ())):
            if child.get("id") == ident:
                return node, index
    return None, None


def nav_blades(doc: dict, root: dict) -> None:
    """Navigation plates (b1_dark) end in a blade at 256 u, their fill
    thinning to 0.45 toward it and their rail running to the point; the
    focus light becomes the lens about the marker."""
    lead = 900.0
    top, bottom = U * 2.8, U * 27.2
    end = lead + U * 256
    for node in list(walk_nodes(root)):
        control = node.get("control")
        if not control:
            continue
        plate = next((c for c in node.get("children", ()) if c.get("id") == f"{node['id']}-plate"), None)
        if plate is None or len(plate.get("paths", ())) != 2 or plate["paths"][0]["id"] != "fill":
            continue
        corners = [c["points"][0] for c in plate["paths"][0]["commands"] if c.get("points")]
        if len(corners) != 4 or not all(isinstance(v, (int, float)) for p in corners for v in p) or \
                abs(corners[0][1] - top) > 0.01 or abs(corners[2][1] - bottom) > 0.01 or abs(corners[1][0] - (lead + U * 413)) > 0.5:
            continue
        fill = blade([(end, bottom), (0, bottom)], top, bottom, end)
        plate["paths"][0] = shape_path("fill", fill, fill=linear((lead, 0), (end, 0), [(0, tint(DEEP)), (0.55, tint(DEEP)),
                                                                                   (1, tint(DEEP, 0.45))]))
        rail = plate["paths"][1]
        rail["commands"][1]["points"][0][0] = round(end, 3)
        rail["stroke"]["paint"] = linear((lead, 0), (end, 0), [(0, tint(RAIL)), (0.6, tint(RAIL)), (1, tint(RAIL, 0.35))])
        focus = find(node, f"{node['id']}-focus")
        focus["paths"] = [p for p in focus["paths"] if p["id"] != "light"]
        focus["paths"][0]["commands"][1]["points"][0][0] = round(lead + U * 232, 3)
        light_lens(doc, node, lens_vector(f"{node['id']}-lens", lead + U * 38, U * 14.8,
                                          absolute(left=0, top=0, width=plate["properties"]["width"]["value"], height=45)))


def action_blades(doc: dict, root: dict) -> None:
    """Action buttons (a confirmation's) end in a blade 35 dp high and take
    the lens about their marker; their fill thins to 0.30, not to nothing."""
    for node in list(walk_nodes(root)):
        control = node.get("control")
        if not control:
            continue
        plate = find(node, f"{node['id']}-plate")
        marker = find(node, f"{node['id']}-marker")
        if plate is None or marker is None or len(plate.get("paths", ())) != 2 or plate["paths"][0]["id"] != "fill":
            continue
        commands = plate["paths"][0]["commands"]
        xs = [c["points"][0][0] for c in commands if c.get("points")]
        ys = [c["points"][0][1] for c in commands if c.get("points")]
        if len(xs) != 5 or not all(isinstance(v, (int, float)) for v in xs + ys):
            continue
        width, band_top, band_bottom, cut = xs[1], ys[0], ys[2], xs[3]
        if not (abs(band_top - 5.0) < 0.01 and abs(band_bottom - 40.0) < 0.01):
            continue
        shape = blade([(width, band_bottom), (cut, band_bottom), (0, band_bottom - cut)], band_top, band_bottom, width)
        paint = plate["paths"][0]["fill"]
        stops = paint.get("stops", [])
        if stops:
            stops[-1]["color"]["value"][3] = 0.3
        plate["paths"][0] = shape_path("fill", shape, fill=paint)
        mp = marker["properties"]
        cx, cy = mp["left"]["value"] + 4, mp["top"]["value"] + 4
        light_lens(doc, node, lens_vector(f"{node['id']}-lens", cx, cy, absolute(left=0, top=0, width=width, height=45)))


def section_blades(doc: dict, root: dict) -> None:
    """Section plates (b2_dark, the pages') end in a blade and light the lens."""
    for node in list(walk_nodes(root)):
        control = node.get("control")
        if not control:
            continue
        plate = find(node, f"{node['id']}-plate")
        marker = find(node, f"{node['id']}-marker")
        if plate is None or marker is None or not plate.get("paths") or plate["paths"][0]["id"] != "fill":
            continue
        commands = plate["paths"][0]["commands"]
        pts = [c["points"][0] for c in commands if c.get("points")]
        if len(pts) != 5 or not all(isinstance(v, (int, float)) for p in pts for v in p):
            continue
        (x0, t), (w, _), (_, b), (cx_, _), (_, cy_) = pts
        if abs(t - U * 13.0) > 0.05:
            continue
        shape = blade([(w, b), (cx_, b), (0, cy_)], t, b, w)
        paint = plate["paths"][0]["fill"]
        for stop in paint.get("stops", []):
            stop["color"]["value"][3] = max(stop["color"]["value"][3], 0.45 if stop["at"] > 0.5 else stop["color"]["value"][3])
        deepen(paint)
        plate["paths"][0] = shape_path("fill", shape, fill=paint)
        mark = marker["paths"][0]["commands"]
        mx = [c["points"][0][0] for c in mark if c.get("points")]
        my = [c["points"][0][1] for c in mark if c.get("points")]
        if not all(isinstance(v, (int, float)) for v in mx + my):
            continue
        light_lens(doc, node, lens_vector(f"{node['id']}-lens", (min(mx) + max(mx)) / 2, (min(my) + max(my)) / 2,
                                          absolute(left=0, top=0, width=plate["properties"]["width"]["value"],
                                                   height=plate["properties"]["height"]["value"])))


def horn_card(root: dict) -> None:
    """The level block's card: a horn at its top-trailing corner (a sweep
    8 u high cut back beneath the corner, then 45 degrees to the edge), in
    place of the major cut, and a needle in place of the foot's rule."""
    card = find(root, "level-card")
    if card is None:
        return
    w, h = card["properties"]["width"]["value"], card["properties"]["height"]["value"]
    major, minor, header, c = 12.0, 3.0, 40.0, U * 8
    outline = Shape(0.0)
    sx, sy, arc = sweep(w, 0, -1, 1, c, False)
    outline.ops = [("M", minor, 0), ("L", w, 0), arc, ("L", w, c + KX * c), ("L", w, h - minor), ("L", w - minor, h), ("L", major, h),
                   ("L", 0, h - major), ("L", 0, minor), ("Z",)]
    paths = {p["id"]: p for p in card["paths"]}
    body = paths["body"]; body.update({"commands": outline.commands()}); deepen(body["fill"])
    rail = paths["rail"]; rail["commands"] = outline.commands()
    wash = Shape(0.0)
    wash.ops = [("M", minor, 0.75), ("L", sx - 2, 0.75), ("L", sx - 2, header), ("L", 0.75, header), ("L", 0.75, minor), ("Z",)]
    paths["wash"]["commands"] = wash.commands()
    rule = find(root, "level-stats-rule")
    if rule and rule.get("paths"):
        line = rule["paths"][0]
        pts = [c["points"][0] for c in line["commands"] if c.get("points")]
        if len(pts) == 2 and all(isinstance(v, (int, float)) for p in pts for v in p):
            (x1, y), (x2, _) = pts
            needle = Shape(0.0)
            needle.ops = [("M", x1, y - U * 0.6), ("L", x2, y), ("L", x1, y + U * 0.6), ("Z",)]
            paint = line.get("stroke", {}).get("paint") or solid(tint(TEXT, 0.16))
            rule["paths"][0] = shape_path("rule", needle, fill=paint)


def horn_plinth(root: dict) -> None:
    """The plinth ends in a blade: its top edge runs on and one sweep falls to its foot."""
    plinth = find(root, "plinth")
    if plinth is None or plinth.get("type") != "vector":
        return
    shape_paths = [p for p in plinth["paths"] if p["id"] == "plinth"]
    if not shape_paths:
        return
    h = plinth["properties"]["height"]["value"]
    lead_cut = U * 11.2
    run = KX * h
    commands = [{"id": "c0", "op": "move", "points": [[0, 0]]},
                {"id": "c1", "op": "line", "points": [[{"fraction": 1, "dp": round(-run, 3)}, 0]]}]
    (ax, ay), (bx, by) = arc_cubic(-run, 0, KR * h, sweep(-run, 0, 1, 1, h, True)[2][2], 0, h)
    commands.append({"id": "c2", "op": "cubic", "points": [[{"fraction": 1, "dp": round(ax, 3)}, round(ay, 3)],
                                                            [{"fraction": 1, "dp": round(bx, 3)}, round(by, 3)], [{"fraction": 1}, round(h, 3)]]})
    commands += [{"id": "c3", "op": "line", "points": [[round(lead_cut, 3), round(h, 3)]]},
                 {"id": "c4", "op": "line", "points": [[0, round(h - lead_cut, 3)]]}, {"id": "c5", "op": "close"}]
    shape_paths[0]["commands"] = commands


def _axis(value) -> tuple:
    """A coordinate as (fraction of the box, dp)."""
    return (value.get("fraction", 0.0), value.get("dp", 0.0)) if isinstance(value, dict) else (0.0, float(value))


def _coord(fraction: float, dp: float):
    return round(dp, 3) if fraction == 0 else {"fraction": fraction, "dp": round(dp, 3)}


def _anchored(ops: list, fx: float, fy: float, prefix: str) -> list:
    """Line and arc ops in dp from (fx, fy) anchors, as commands; the first
    op only sets where the run starts."""
    out, at = [], ops[0][1:3]
    for index, op in enumerate(ops[1:]):
        name = f"{prefix}{index}"
        if op[0] == "L":
            out.append({"id": name, "op": "line", "points": [[_coord(fx, op[1]), _coord(fy, op[2])]]}); at = op[1:3]
        else:
            (ax, ay), (bx, by) = arc_cubic(at[0], at[1], op[1], op[2], op[3], op[4])
            out.append({"id": name, "op": "cubic", "points": [[_coord(fx, ax), _coord(fy, ay)], [_coord(fx, bx), _coord(fy, by)],
                                                               [_coord(fx, op[3]), _coord(fy, op[4])]]}); at = op[3:5]
    return out


def horn_modals(root: dict) -> None:
    """A confirmation's silhouette and black frame take the horned corner at
    the top-trailing corner and a horn under the chamfer (section 6): the
    top edge runs on to the corner, a sweep 8 u high cuts back beneath it
    and 45 degrees return to the trailing edge; the foot runs on to the
    lower-leading corner and a sweep a chamfer's height over 2.73 rises from
    it to the chamfer."""
    from build_retained_screens import MODAL_CORNER
    c8 = U * 8
    for node in walk_nodes(root):
        if node.get("type") != "vector" or not node["id"].endswith("-frame"):
            continue
        for item in node.get("paths", ()):
            commands = item.get("commands", [])
            if len(commands) < 10 or any(c["op"] not in ("move", "line", "close") for c in commands):
                continue
            pts = [[_axis(v) for v in c["points"][0]] if c.get("points") else None for c in commands]
            new, i, changed = [], 0, False
            while i < len(commands):
                p, q = pts[i], pts[i + 1] if i + 1 < len(commands) else None
                if p and q and p[0][0] == q[0][0] and p[1][0] == q[1][0] and abs((q[0][1] - p[0][1]) - MODAL_CORNER) < 0.01                         and abs((q[1][1] - p[1][1]) - MODAL_CORNER) < 0.01:
                    (fx, right), (fy, raised) = q[0], p[1]
                    new += _anchored([("M", 0, 0), ("L", right, raised), sweep(right, raised, -1, 1, c8, False)[2],
                                      ("L", right, raised + c8 + KX * c8)], fx, fy, f"{commands[i]['id']}h")
                    i, changed = i + 2, True
                    continue
                new.append(commands[i])
                i += 1
            if not changed:
                continue
            # the chamfer: the last two points before the close
            tail = [index for index, c in enumerate(new) if c["op"] == "line"][-2:]
            if len(tail) == 2:
                (ax, ay), (bx, by) = [[_axis(v) for v in new[index]["points"][0]] for index in tail]
                if ax[0] == bx[0] and ay[0] == by[0] and ax[1] > bx[1] and abs((ax[1] - bx[1]) - (ay[1] - by[1])) < 0.01:
                    under = (ax[1] - bx[1]) / (1 + KX)
                    built = _anchored([("M", ax[1], ay[1]), ("L", bx[1], ay[1]), sweep(bx[1], ay[1], 1, -1, under, False)[2],
                                       ("L", bx[1], by[1])], bx[0], ay[0], "ch")
                    new[tail[1]:tail[1] + 1] = built
            item["commands"] = new
            if item["id"] == "frame" and item.get("fill"):
                deepen(item["fill"])


# ---------------------------------------------------------- snow and haze

def snow_layer(ident: str, rand, density: float, rmin: float, rmax: float, alpha: float, speed: float) -> tuple:
    """One depth of snow (section 6): round flakes behind the instruments,
    drawn twice so one loop of falling 480 u along the artwork's 13-degree
    line returns them to where they started. Returns the vector and its
    loop's length in ms."""
    span_x = (-560.0, 1320.0)
    count = round((span_x[1] - span_x[0]) * 480 * density)
    drift = 0.23 * 480
    paths = []
    for index in range(count):
        x, y = span_x[0] + rand() * (span_x[1] - span_x[0]), rand() * 480
        r = rmin + rand() * (rmax - rmin)
        a = round(alpha * (0.4 + 0.6 * rand()), 3)
        for copy_index, (dx, dy) in enumerate(((0, 0), (drift, -480))):
            flake = Shape(0.0)
            cx, cy, rr = U * (x + dx + WIDE), U * (y + dy), U * r
            if rr < 1.5:
                # a far flake is under 1.5 dp: a square reads as the same dot
                side = rr * 0.886
                flake.ops = [("M", cx - side, cy - side), ("L", cx + side, cy - side), ("L", cx + side, cy + side), ("L", cx - side, cy + side), ("Z",)]
            else:
                circle(flake, cx, cy, rr)
            paths.append(shape_path(f"f{index}-{copy_index}", flake, fill=solid(tint(SNOW, a))))
    loop_ms = round(480 / speed * 1000)
    box = {**absolute(top=0, height=720, width=CANVAS_W + U * (2 * WIDE + 200)), "left": length(50, "%"),
           "margin-left": length(-CANVAS_W / 2 - U * WIDE)}
    return vector(ident, {**box, "transform": transform(), "pointer-events": keyword("none")}, paths), loop_ms


def haze() -> dict:
    """The low haze: awakening.haze rising from nothing at 55% of the view's
    height to 0.14 at its foot."""
    whole = _span(0, 0)
    whole.commands = lambda: [{"id": "a", "op": "move", "points": [[{"fraction": 0}, {"fraction": 0.55}]]},
                              {"id": "b", "op": "line", "points": [[{"fraction": 1}, {"fraction": 0.55}]]},
                              {"id": "c", "op": "line", "points": [[{"fraction": 1}, {"fraction": 1}]]},
                              {"id": "d", "op": "line", "points": [[{"fraction": 0}, {"fraction": 1}]]},
                              {"id": "e", "op": "close"}]
    return vector("haze", {**FULL, "pointer-events": keyword("none")}, [shape_path("haze", whole, fill=linear(
        (0, {"fraction": 0.55}), (0, {"fraction": 1}), [(0, tint(HAZE, 0)), (1, tint(HAZE, 0.14))]))])


# ------------------------------------------------------------------- motion

def timeline(doc: dict, ident: str) -> dict | None:
    return next((t for t in doc["timelines"] if t["id"] == ident), None)


def set_track(line: dict, node: str, prop: str, keys: list) -> None:
    line["tracks"] = [t for t in line["tracks"] if not (t["node"] == node and t["property"] == prop)]
    line["tracks"].append(track(node, prop, keys))
    line["durationMs"] = max(line["durationMs"], max(k[0] for k in keys))


def opposite(value: dict) -> dict:
    tx, ty = value["value"][0], value["value"][1]
    return transform(-tx, -ty)


def band_moves(doc: dict, prefix: str) -> None:
    """The Awakening motion of section 8 on the pause's bands and levels."""
    top, bottom = f"{prefix}-top", f"{prefix}-bottom"
    home = transform()
    page_top, page_bottom = transform(*TOP_TO_PAGE), transform(*BOTTOM_TO_PAGE)
    above, below = transform(0, -U * CLOSE_IN), transform(0, U * CLOSE_IN)
    # Everything that travelled on accel(a, d) takes the glide.
    for line in doc["timelines"]:
        for entry in line["tracks"]:
            for key in entry["keys"]:
                if key.get("easing") == ACCEL:
                    key["easing"] = GLIDE

    def moves(line: dict, node: str, keys: list, rime: list) -> None:
        set_track(line, node, "transform", keys)
        set_track(line, f"{node}-frost", "transform", [(k[0], opposite(k[1]), *k[2:]) for k in keys])
        set_track(line, f"{node}-rime", "opacity", [(at, number(v), *e) for at, v, *e in rime])

    # Opening: the bands close in from 30 u beyond their places (frame.close),
    # the prompt bar with the bottom band; the frost grows (frost.grow).
    opening = timeline(doc, "open")
    lit = RIME_REST + (RIME_LIT - RIME_REST) * 0.47
    moves(opening, top, [(0, above), (1, above, CLOSE), (1 + CLOSE_MS, home)], [(0, lit), (1, lit, EASE_OUT), (1 + CLOSE_MS, RIME_REST)])
    moves(opening, bottom, [(0, below), (1, below, CLOSE), (1 + CLOSE_MS, home)], [(0, lit), (1, lit, EASE_OUT), (1 + CLOSE_MS, RIME_REST)])
    set_track(opening, "prompts", "transform", [(0, below), (1, below, CLOSE), (1 + CLOSE_MS, home)])
    for side in ("top", "bottom"):
        for index in range(FROST_BINS):
            start = 1 - (1 - index / FROST_BINS) ** (1 / 3)
            end = 1 - (1 - (index + 1) / FROST_BINS) ** (1 / 3)
            keys = [(0, number(0)), (1, number(0))]
            if start * FROST_MS > 1:
                keys.append((round(start * FROST_MS), number(0)))
            keys.append((round(end * FROST_MS), number(1)))
            set_track(opening, f"{prefix}-{side}-crystals-{index}", "opacity", keys)
    # To a page: the bottom band leads from 50 ms, the top band follows 40 ms
    # later, both arriving at 550 ms; the rime swells with each one's speed.
    depart = timeline(doc, "depart")
    moves(depart, bottom, [(0, home), (50, home, GLIDE), (550, page_bottom)],
          [(0, RIME_REST), (50, RIME_REST, SWELL), (300, RIME_LIT, SWELL), (550, RIME_REST)])
    moves(depart, top, [(0, home), (90, home, GLIDE), (550, page_top)],
          [(0, RIME_REST), (90, RIME_REST, SWELL), (320, RIME_LIT, SWELL), (550, RIME_REST)])
    # Back home: the bottom band from the first frame, the top band 40 ms
    # later; the home level scrolls in its last 32 u from the trailing side.
    back = timeline(doc, "returnHome")
    moves(back, bottom, [(0, page_bottom, GLIDE), (500, home), (700, home)],
          [(0, RIME_REST, SWELL), (250, RIME_LIT, SWELL), (500, RIME_REST), (700, RIME_REST)])
    moves(back, top, [(0, page_top), (40, page_top, GLIDE), (500, home), (700, home)],
          [(0, RIME_REST), (40, RIME_REST, SWELL), (270, RIME_LIT, SWELL), (500, RIME_REST), (700, RIME_REST)])
    arrive = transform(U * ARRIVE)
    set_track(back, "home", "transform", [(0, arrive), (450, arrive, CLOSE), (450 + ARRIVE_MS, transform())])
    set_track(back, "home", "opacity", [(0, number(0)), (450, number(0)), (600, number(1)), (700, number(1))])
    # The carved slots: the prompt bar's at home, the title's and BACK's on a page.
    for line, at_page in ((opening, False), (depart, True), (back, False)):
        shown_home, shown_page = (0, 1) if at_page else (1, 0)
        when = 550 if line is depart else (450 if line is back else 1)
        for node, value in ((f"{bottom}-carve-home", shown_home), (f"{bottom}-carve-page", shown_page), (f"{top}-carve-title", shown_page)):
            set_track(line, node, "opacity", [(0, number(1 - value)), (when, number(1 - value)), (when + 1, number(value))])


def follow_bands(doc: dict) -> None:
    """Wherever a timeline moves a band, its frost takes the opposite motion
    (so the frost stays with the view) and its rime line swells to 0.90 at
    the middle of each move and settles as the band does (rime.catch)."""
    for line in doc["timelines"]:
        for entry in list(line["tracks"]):
            if entry["property"] != "transform" or entry["node"] not in ("band-top", "band-bottom"):
                continue
            node, keys = entry["node"], entry["keys"]
            frost = [{**copy.deepcopy(key), "value": opposite(key["value"])} for key in keys]
            line["tracks"] = [t for t in line["tracks"] if not (t["node"] == f"{node}-frost" and t["property"] == "transform")]
            line["tracks"].append({"node": f"{node}-frost", "property": "transform", "keys": frost})
            rime = [(keys[0]["atMs"], RIME_REST)]
            for a, b in zip(keys, keys[1:]):
                moving = a["value"]["value"][:2] != b["value"]["value"][:2]
                if moving and b["atMs"] - a["atMs"] >= 2:
                    middle = round((a["atMs"] + b["atMs"]) / 2)
                    rime[-1] = (rime[-1][0], rime[-1][1], SWELL)
                    rime += [(middle, RIME_LIT, SWELL), (b["atMs"], RIME_REST)]
                elif b["atMs"] > rime[-1][0]:
                    rime.append((b["atMs"], RIME_REST))
            set_track(line, f"{node}-rime", "opacity", [(at, number(v), *e) for at, v, *e in rime])


def awaken_frame(doc: dict, carves: dict | None = None) -> None:
    """Carry a document's frame into the family: the palette, the horned,
    frosted and rimed bands where its Marine bands stood (keeping their
    resting dock), the haze, the snow and the frost-tinted crosses, the
    Valkaryne frames on its plates, card and dialogs, the glide in place of
    accel(a, d), and frost and rime that follow every band move."""
    root = doc["root"]
    recolour(doc)
    top_ops, bottom_ops, fullers = band_edges()
    parent, _ = parent_of(root, "band-top")
    children = parent["children"]
    for index, child in enumerate(children):
        if child["id"] in ("band-top", "band-bottom"):
            top = child["id"] == "band-top"
            base = copy.deepcopy(child["properties"].get("transform") or transform())
            children[index] = awakening_band("band", top, top_ops if top else bottom_ops,
                                             [f for f in fullers if f[0] == ("top" if top else "bottom")], base, carves)
    light_index = next(i for i, c in enumerate(children) if c["id"] == "field-light")
    children.insert(light_index, haze())
    grid = find(root, "field-grid")
    for item in grid["paths"]:
        item["stroke"]["paint"] = solid(tint(FROST, 0.04))
    rand = Mulberry(SEED + 1)
    far, far_ms = snow_layer("snow-far", rand, 0.0006, 0.3, 0.8, 0.59, 6)
    near, near_ms = snow_layer("snow-near", rand, 0.0001, 0.9, 1.8, 0.78, 14)
    grid_index = next(i for i, c in enumerate(children) if c["id"] == "field-grid")
    children[grid_index + 1:grid_index + 1] = [far, near]
    drift = transform(-U * 0.23 * 480, U * 480)
    doc["timelines"].append({"id": "snow", "durationMs": far_ms, "iterations": 0, "tracks": [
        track("snow-far", "transform", [(0, transform()), (far_ms, drift)])]})
    doc["timelines"].append({"id": "snowNear", "durationMs": near_ms, "iterations": 0, "tracks": [
        track("snow-near", "transform", [(0, transform()), (near_ms, drift)])]})
    events = doc.setdefault("events", {})
    for event in ("open", "returnHome", "onActivate"):
        if event not in events and not timeline(doc, event) and event != "onActivate":
            continue
        if event not in events and timeline(doc, event):
            # A named event without a program plays the timeline of its name;
            # a program must do so itself.
            events[event] = [{"op": "playTimeline", "timeline": event}]
        events.setdefault(event, []).extend([{"op": "playTimeline", "timeline": "snow"}, {"op": "playTimeline", "timeline": "snowNear"}])
    for line in doc["timelines"]:
        for entry in line["tracks"]:
            for key in entry["keys"]:
                if key.get("easing") == ACCEL:
                    key["easing"] = GLIDE
    nav_blades(doc, root)
    action_blades(doc, root)
    section_blades(doc, root)
    horn_card(root)
    horn_plinth(root)
    horn_modals(root)
    follow_bands(doc)


# ----------------------------------------------------------------- the pause

def campaign_title(doc_state: dict, bindings: list, title: dict) -> dict:
    """The campaign's name 8 u after GAME PAUSED, in awakening.value at 0.80
    (section 13.7), placed for each language by GAME PAUSED's measured width.
    A tall view, which restacks the title, leaves it out."""
    props = title["properties"]
    left, top, height = props["left"]["value"], props["top"]["value"], props["height"]["value"]
    size = props["font-size"]["value"]
    widths = {language: text_width("marine", text, size) for language, text in merged_text("#str_230031").items()}
    value = round(left + widths.get("english", 200) + U * 8, 3)
    for language in sorted(widths, reverse=True):
        value = {"op": "select", "args": [{"op": "==", "args": [{"state": "language"}, language]}, round(left + widths[language] + U * 8, 3), value]}
    doc_state.setdefault("language", {"type": "string", "initial": "", "cvar": "sys_lang"})
    node = label("paused-campaign", "#str_230040", {**absolute(left=left, top=top, width=U * 200, height=height),
                                                    **typeface("marine", size, height, tint(RAIL, 0.8)), "white-space": keyword("nowrap"),
                                                    "display": keyword("block")})
    for key in ("font-size", "line-height", "letter-spacing"):
        if key in props:
            node["properties"][key] = copy.deepcopy(props[key])
    bindings.append({"id": "paused-campaign.left", "node": "paused-campaign", "property": "left", "value": value})
    return node


def awaken_pause(document: dict) -> dict:
    """The Marine pause carried into The Awakening's family."""
    doc = copy.deepcopy(document)
    doc["id"] = "openq4.pause_awakening"
    root = doc["root"]
    awaken_frame(doc)
    # GAME PAUSED, then the campaign's name.
    title = find(root, "paused-title")
    parent, index = parent_of(root, "paused-title")
    campaign = campaign_title(doc.setdefault("state", {}), doc.setdefault("bindings", []), title)
    parent["children"].insert(index + 1, campaign)
    for binding in [b for b in doc["bindings"] if b["node"] == "paused-title" and b["property"] in ("font-size", "line-height", "letter-spacing")]:
        doc["bindings"].append({**copy.deepcopy(binding), "id": binding["id"].replace("paused-title", "paused-campaign"), "node": "paused-campaign"})
    condition = _tall(doc)
    if condition:
        doc["bindings"].append({"id": "paused-campaign.display", "node": "paused-campaign", "property": "display",
                                "value": {"op": "select", "args": [condition, "none", "block"]}})
    # The prompt bar rides in with the bottom band.
    prompts = find(root, "prompts")
    prompts["properties"].setdefault("transform", transform())
    band_moves(doc, "band")
    return doc


def _tall(doc: dict):
    """The tall-view condition the pause's own bindings already use."""
    from build_retained_screens import is_tall
    for binding in doc["bindings"]:
        value = binding["value"]
        if isinstance(value, dict) and value.get("op") == "select" and is_tall(value["args"][0]):
            return value["args"][0]
    return None


# ------------------------------------------------------------------- pages

def awaken_page(document: dict, ident: str) -> dict:
    """A page the Awakening pause hands off to, carried into the family: its
    colors, blades and lens, horned confirmations, and its arrival scrolling
    in the last 32 u from the leading side (level.arrive)."""
    doc = copy.deepcopy(document)
    doc["id"] = ident
    root = doc["root"]
    recolour(doc)
    section_blades(doc, root)
    action_blades(doc, root)
    horn_modals(root)
    arrive, rest = timeline(doc, "arrive"), timeline(doc, "rest")
    if arrive and rest:
        panels = [t["node"] for t in arrive["tracks"] if t["property"] == "opacity" and t["node"].endswith("-panel")]
        for panel in panels:
            parent, _ = parent_of(root, panel)
            if parent is None or parent["id"] in ("screen",):
                continue
            parent["properties"].setdefault("transform", transform())
            start = transform(-U * ARRIVE)
            set_track(arrive, parent["id"], "transform", [(0, start), (1, start, CLOSE), (1 + ARRIVE_MS, transform())])
            set_track(rest, parent["id"], "transform", [(0, transform()), (1, transform())])
            arrive["durationMs"] = 1 + ARRIVE_MS
    return doc


AWAKENING_OUTPUTS = {name: PAK0 / "guis" / "menu" / f"{name}.rui"
                     for name in ("pause_awakening", "loadgame_awakening", "savegame_awakening", "restartlevel_awakening")}
AWAKENING_OUTPUTS.update({f"{name}_awakening": PAK0 / "guis" / "menu" / f"{path}_awakening.rui"
                          for name, path in {"settings": "settings", "controls": "settings/controls", "game": "settings/game",
                                             "audio": "settings/audio", "system": "settings/system"}.items()})


# The settings pages the Awakening pause opens: the SETTINGS page and its
# categories. They keep their document ids, which their application
# contracts name (the SYSTEM page's is checked by id).
SETTINGS = {"settings": "settings", "controls": "settings/controls", "game": "settings/game", "audio": "settings/audio",
            "system": "settings/system"}


def awaken_settings(document: dict) -> dict:
    """A settings page carried into the family, its band text carved out of the frost."""
    doc = copy.deepcopy(document)
    awaken_frame(doc, PAGE_CARVES)
    return doc


def awakening_documents(documents: dict) -> dict:
    """The Awakening family's documents, from the Marine ones just built."""
    from build_retained_screens import NOTICE_DOCUMENTS
    from retained_notices import with_notices

    def built(name: str) -> dict:
        # The Marine document as the generator presents it: with the
        # session's notices where it carries them, so the copy does too.
        return with_notices(copy.deepcopy(documents[name])) if name in NOTICE_DOCUMENTS else documents[name]
    return {"pause_awakening": awaken_pause(built("pause")),
            "loadgame_awakening": awaken_page(built("loadgame"), "openq4.loadgame_awakening"),
            "savegame_awakening": awaken_page(built("savegame"), "openq4.savegame_awakening"),
            "restartlevel_awakening": awaken_page(built("restartlevel"), "openq4.restartlevel_awakening"),
            **{f"{name}_awakening": awaken_settings(built(name)) for name in SETTINGS}}
