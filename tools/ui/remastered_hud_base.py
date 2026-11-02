#!/usr/bin/env python3
"""Shared construction for the Remastered HUD documents (specification 14.1-14.12).

Each document is the Remastered layout of a stock HUD GUI. The engine pairs
it with that GUI (src/ui/RetainedHud.cpp, idRemasteredHud): the document
receives the same state keys and named events the game sends the stock GUI,
and draws the atlas constructions on the section 14.4 layout - the 4:3 center
screen, side screens and bands, nine anchors, the 21:9 cap, HUD scale and the
safe inset. Classic keeps the stock GUI.

Geometry is authored in stock units (u) and written in dp (1 u = 1.5 dp).
Drawings come from the design atlas through hud_svg.py; no retail image is
read.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
from functools import lru_cache
from pathlib import Path

import build_retained_screens as ui
import hud_svg

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "content/baseoq4/pak0/guis/hud"
U = 1.5


def u(value: float) -> float:
    return round(value * U, 4)


def S(key: str) -> dict:
    return {"state": key}


def op(name: str, *args) -> dict:
    return {"op": name, "args": list(args)}


def sel(condition, yes, no) -> dict:
    return op("select", condition, yes, no)


def AND(*items):
    result = items[0]
    for item in items[1:]:
        result = op("&&", result, item)
    return result


def OR(*items):
    result = items[0]
    for item in items[1:]:
        result = op("||", result, item)
    return result


def NOT(item):
    return op("!", item)


def rgb(hex_colour: str, alpha: float = 1.0) -> list:
    return ui.rgb(hex_colour, alpha)


def mix(a: list, b: list, t: float) -> list:
    return [round(a[i] + (b[i] - a[i]) * t, 5) for i in range(4)]


# ------------------------------------------------------------------ text faces

@lru_cache(maxsize=None)
def face_metrics(face: str) -> dict:
    """Vertical metrics of a shipped face. The retained text engine sizes a
    face so that ascent plus descent equals the font size, so the baseline
    sits `ascent` of the size below the top of a line box of that height."""
    data = (ROOT / "content/baseoq4/pak0/fonts" / f"{face}.ttf").read_bytes()
    tables = {}
    for index in range(struct.unpack(">H", data[4:6])[0]):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * index:28 + 16 * index])
        tables[tag.decode("latin-1")] = offset
    hhea = tables["hhea"]
    ascent, descent = struct.unpack(">hh", data[hhea + 4:hhea + 8])
    total = ascent - descent
    os2 = tables["OS/2"]
    cap = struct.unpack(">h", data[os2 + 88:os2 + 90])[0]
    return {"ascent": ascent / total, "cap": cap / total}


# Bound text needs a language key as its base; its binding owns the property
# before the first paint, so the placeholder is never shown.
BOUND_TEXT = "#str_230007"


def text_box(ident: str, key: str, *, face: str, size: float, colour: list, baseline: float,
             x: float | None = None, right: float | None = None, center: float | None = None,
             width: float = 600.0, extra: dict | None = None) -> dict:
    """A one-line text node whose baseline sits at `baseline` (dp), its
    leading edge at x, its trailing edge at `right`, or centred on `center`."""
    top = baseline - size * face_metrics(face)["ascent"]
    props = {**ui.typeface(face, size, size, colour), "white-space": ui.keyword("nowrap")}
    if center is not None:
        props.update(ui.absolute(left=center - width / 2, top=top, width=width, height=size))
        props["text-align"] = ui.keyword("center")
    elif right is not None:
        props.update(ui.absolute(left=right - width, top=top, width=width, height=size))
        props["text-align"] = ui.keyword("right")
    else:
        props.update(ui.absolute(left=x, top=top, width=width, height=size))
    if extra:
        props.update(extra)
    return ui.label(ident, key or BOUND_TEXT, props)


def shadowed(ident: str, key: str, offset: float = 1.5, shadow_alpha: float = 1.0, **kw) -> list:
    """Text over gameplay takes the stock drop shadow: a black copy 1.5 dp
    toward the trailing bottom at the text's own alpha (section 5)."""
    base = text_box(ident, key, **kw)
    shadow_kw = dict(kw)
    shadow_kw["colour"] = [0, 0, 0, kw["colour"][3] * shadow_alpha]
    shadow_kw["baseline"] = kw["baseline"] + offset
    for side in ("x", "right", "center"):
        if shadow_kw.get(side) is not None:
            shadow_kw[side] = shadow_kw[side] + offset
    shadow = text_box(ident + "-shadow", key, **shadow_kw)
    return [shadow, base]


# ------------------------------------------------------------------- geometry

def pts_path(ident: str, points: list, *, fill=None, stroke=None, closed: bool = True, blend=None, rule=None) -> dict:
    return ui.path(ident, [(round(x, 4), round(y, 4)) for x, y in points], fill=fill, stroke=stroke, closed=closed,
                   blend=blend, fill_rule=rule)


def svg_path(ident: str, data: str, *, scale: float = U, dx: float = 0, dy: float = 0, fill=None, stroke=None,
             blend=None, rule=None, matrix=None) -> dict:
    m = matrix or (scale, 0, 0, scale, dx, dy)
    result = {"id": ident, "commands": hud_svg.commands(data, m)}
    if rule:
        result["fillRule"] = rule
    result["fill"] = fill if fill is not None else {"type": "none"}
    if stroke is not None:
        result["stroke"] = stroke
    if blend:
        result["blend"] = blend
    return result


def stroke(colour: list, width: float, *, join: str = "miter", cap: str = "butt", minimum: float = 0) -> dict:
    return {"paint": ui.solid(colour), "widthDp": round(width, 4), "minimumPixels": minimum, "join": join, "cap": cap}


def sheared_points(points: list, k: float, cy: float) -> list:
    """x' = x + k (y - cy): the stock `shear 0,k` about a centre line."""
    return [(x + k * (y - cy), y) for x, y in points]


def rounded_rect(x: float, y: float, w: float, h: float, r: float, k: float = 0.0, cy: float | None = None,
                 steps: int = 4) -> list:
    """A rounded rectangle as a polygon, optionally sheared about cy."""
    r = max(0.0, min(r, w / 2, h / 2))
    pts = []
    corners = [(x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)]
    for cx, cyy, start in corners:
        for i in range(steps + 1):
            a = math.radians(start + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cyy + r * math.sin(a)))
    if k:
        pts = sheared_points(pts, k, cy if cy is not None else y + h / 2)
    return pts


def frac_point(fx: float, dx: float, y: float) -> list:
    return [{"fraction": fx, "dp": round(dx, 4)}, round(y, 4)]


def fitted_plate_path(ident: str, h: float, *, k: float = -0.22, r: float = 1.5, fill=None, stroke_=None,
                      pad: float = 0.0, top: float = 0.0) -> dict:
    """A sheared rounded plate spanning its node's width (responsive
    coordinates), so it is never wider than its content (section 14.4)."""
    s = k * h / 2          # with k < 0 the top edge leans toward the trailing side
    top_dx, bottom_dx = -s + pad, s + pad
    cmds = []

    def p(fx, dx, y):
        return frac_point(fx, dx, y + top)
    cmds.append({"id": "c0", "op": "move", "points": [p(0, top_dx + r, 0)]})
    cmds.append({"id": "c1", "op": "line", "points": [p(1, top_dx - r, 0)]})
    cmds.append({"id": "c2", "op": "quadratic", "points": [p(1, top_dx, 0), p(1, top_dx + k * r, r)]})
    cmds.append({"id": "c3", "op": "line", "points": [p(1, bottom_dx - k * r, h - r)]})
    cmds.append({"id": "c4", "op": "quadratic", "points": [p(1, bottom_dx, h), p(1, bottom_dx - r, h)]})
    cmds.append({"id": "c5", "op": "line", "points": [p(0, bottom_dx + r, h)]})
    cmds.append({"id": "c6", "op": "quadratic", "points": [p(0, bottom_dx, h), p(0, bottom_dx - k * r, h - r)]})
    cmds.append({"id": "c7", "op": "line", "points": [p(0, top_dx + k * r, r)]})
    cmds.append({"id": "c8", "op": "quadratic", "points": [p(0, top_dx, 0), p(0, top_dx + r, 0)]})
    cmds.append({"id": "c9", "op": "close"})
    result = {"id": ident, "commands": cmds}
    result["fill"] = fill if fill is not None else {"type": "none"}
    if stroke_ is not None:
        result["stroke"] = stroke_
    return result


def vec(ident: str, paths: list, props: dict | None = None, children=(), **extra) -> dict:
    base = {**ui.FULL, "display": ui.keyword("block")}
    if props:
        base.update(props)
    return ui.vector(ident, base, paths, children, **extra)


def box(ident: str, left: float, top: float, width: float, height: float, children=(), **props) -> dict:
    p = {**ui.absolute(left=left, top=top, width=width, height=height), "display": ui.keyword("block")}
    p.update(props)
    return ui.group(ident, p, children)


def shear_group(ident: str, left: float, top: float, width: float, height: float, k: float, children: list,
                **props) -> dict:
    """An exact shear x' = x + k (y - cy) about the box centre, from two
    centred rotations around an anisotropic scale (R(t) S R(p) is the shear's
    singular value decomposition). Clipping inside follows it exactly."""
    s = math.sqrt(1 + k * k / 4) + abs(k) / 2
    theta = 0.5 * math.atan2(2 * k, k * k)
    c, sn = math.cos(theta), math.sin(theta)
    # V^T = S^-1 U^T A, with A = [[1, k], [0, 1]] and U = R(theta).
    vt00 = (c * 1 + sn * 0) / s
    vt10 = s * (-sn * 1 + c * 0)
    phi = math.atan2(vt10, vt00)
    inner = ui.transform()
    inner["value"][4] = round(math.degrees(phi), 6)
    middle = ui.transform(sx=round(s, 7), sy=round(1 / s, 7))
    outer = ui.transform()
    outer["value"][4] = round(math.degrees(theta), 6)
    full = {**ui.absolute(left=0, top=0, width=width, height=height), "display": ui.keyword("block")}
    root_props = {**ui.absolute(left=left, top=top, width=width, height=height), "display": ui.keyword("block"),
                  "transform": outer}
    root_props.update(props)
    return ui.group(ident, root_props, [
        ui.group(ident + "-s", {**full, "transform": middle}, [
            ui.group(ident + "-r", {**full, "transform": inner}, children)])])


# --------------------------------------------------------------- the document

class HudDocument(ui.Document):
    """A Remastered HUD document: legacy-named state, events and anchors."""

    def __init__(self, ident: str):
        super().__init__(ident)
        self.joins: list = []

    # state -----------------------------------------------------------------
    def number(self, key: str, initial: float = 0) -> str:
        self.state.setdefault(key, {"type": "number", "initial": initial})
        return key

    def boolean(self, key: str, initial: bool = False) -> str:
        self.state.setdefault(key, {"type": "boolean", "initial": initial})
        return key

    def string(self, key: str, initial: str = "") -> str:
        self.state.setdefault(key, {"type": "string", "initial": initial})
        return key

    def cvar(self, key: str, cvar: str, kind: str = "boolean", initial=False) -> str:
        self.state.setdefault(key, {"type": kind, "initial": initial, "cvar": cvar})
        return key

    # events ----------------------------------------------------------------
    def on(self, name: str, *steps) -> None:
        self.events.setdefault(name, []).extend(steps)

    def timeline(self, ident: str, duration: float, tracks: list, **kw) -> str:
        """A timeline whose tracks start where they are authored: a play
        retargets each track's first key to the property's current value, so a
        copy of that key 1 ms later makes the authored start take effect at
        once (the first interval's easing moves onto the copy)."""
        for track in tracks:
            keys = track["keys"]
            if len(keys) >= 2 and keys[1]["atMs"] > 1:
                hold = {"atMs": 1, "value": keys[0]["value"]}
                if "easing" in keys[0]:
                    hold["easing"] = keys[0].pop("easing")
                keys.insert(1, hold)
        return self.timelines.add(ident, duration, tracks, **kw)

    def show(self, ident: str, condition, shown: str = "block") -> None:
        self.bind(ident + ".show", ident, "display", sel(condition, shown, "none"))


def set_state(**values) -> dict:
    return {"op": "setState", "values": values}


def set_values(values: dict) -> dict:
    return {"op": "setState", "values": values}


def play(timeline: str) -> dict:
    return {"op": "playTimeline", "timeline": timeline}


def cancel(timeline: str, policy: str = "base") -> dict:
    return {"op": "cancelTimeline", "timeline": timeline, "policy": policy}


def when(condition, then: list, otherwise: list | None = None) -> dict:
    step = {"op": "if", "condition": condition, "then": then}
    if otherwise:
        step["else"] = otherwise
    return step


def call(event: str) -> dict:
    return {"op": "call", "event": event}


def num(value: float) -> dict:
    return ui.number(round(value, 5))


def tr(node: str, prop: str, keys: list) -> dict:
    return ui.track(node, prop, keys)


def transform_value(tx=0.0, ty=0.0, sx=1.0, sy=1.0, deg=0.0) -> dict:
    value = ui.transform(tx, ty, sx, sy)
    value["value"][4] = round(deg, 5)
    return value


# ---------------------------------------------------------------- the anchors

ANCHOR_POINTS = {"tl": (0, 0), "tc": (320, 0), "tr": (640, 0), "cl": (0, 240), "cc": (320, 240), "cr": (640, 240),
                 "bl": (0, 480), "bc": (320, 480), "br": (640, 480)}


def anchor(doc: HudDocument, key: str, children: list, *, scale="hud.scale", ident: str | None = None) -> dict:
    """A 640x480 u stock canvas placed so its stock anchor point lands on the
    live anchor (section 14.4) and scaled about it by the HUD scale (a state
    key or an expression). Groups inside keep their stock coordinates."""
    ident = ident or f"anchor-{key}"
    ax, ay = ANCHOR_POINTS[key]
    props = {"position": ui.keyword("absolute"), "width": ui.length(960), "height": ui.length(720),
             "display": ui.keyword("block"), "transform": ui.transform()}
    v, h = key[0], key[1]
    if h == "l":
        props["left"] = ui.length(0)
        doc.bind(ident + ".left", ident, "left", S("hud.left"))
    elif h == "r":
        props["right"] = ui.length(0)
        doc.bind(ident + ".right", ident, "right", S("hud.right"))
    else:
        props["left"] = ui.length(50, "%")
        props["margin-left"] = ui.length(-480)
    if v == "t":
        props["top"] = ui.length(0)
        doc.bind(ident + ".top", ident, "top", S("hud.top"))
    elif v == "b":
        props["bottom"] = ui.length(0)
        doc.bind(ident + ".bottom", ident, "bottom", S("hud.bottom"))
    else:
        props["top"] = ui.length(50, "%")
        props["margin-top"] = ui.length(-360)
        if h != "c":
            # An edge midpoint centres on the safe area's height (13.3), which
            # unequal top and bottom insets move off the view's; the center
            # anchor stays on the true projection center.
            doc.bind(ident + ".middle", ident, "margin-top",
                     op("+", -360, op("*", 0.5, op("-", S("hud.top"), S("hud.bottom")))))
    # Scale about the stock anchor point: t = (a - c)(1 - s), c the box centre.
    s = S(scale) if isinstance(scale, str) else scale
    one_minus = op("-", 1, s)
    doc.bind(ident + ".scale", ident, "transform",
             [op("*", u(ax) - 480, one_minus), op("*", u(ay) - 360, one_minus), s, s, 0])
    return ui.group(ident, props, children)


def layout_state(doc: HudDocument) -> None:
    for key, initial in (("hud.width", 1280), ("hud.height", 720), ("hud.left", 0), ("hud.right", 0), ("hud.top", 0),
                         ("hud.bottom", 0), ("hud.scale", 1), ("hud.centerScale", 1)):
        doc.number(key, initial)
    doc.cvar("oq4.reduced", "ui_retainedReducedMotion")
    doc.cvar("oq4.opaque", "ui_retainedOpaqueBacking")
    # 1 Remastered, 2 Minimal (section 13.9); Classic (0) never loads a document.
    doc.cvar("hud.layout", "ui_hudLayout", "number", 1)
    # The HUD settings (section 13.9): opacity and the optional groups.
    doc.cvar("hud.opacity", "ui_hudOpacity", "number", 1)
    doc.cvar("hud.damageDir", "ui_hudDamageDirection", "boolean", True)
    doc.cvar("hud.pickups", "ui_hudPickups", "boolean", True)
    doc.cvar("hud.strip", "ui_hudWeaponStrip", "boolean", True)


def gameplay_root(doc: HudDocument, ident: str, children: list) -> dict:
    """A gameplay HUD's root, faded by the HUD opacity setting (13.9)."""
    node = ui.group(ident, {**ui.FULL, "display": ui.keyword("block"), "opacity": num(1)}, children)
    doc.bind(ident + ".opacity", ident, "opacity", op("clamp", S("hud.opacity"), 0.25, 1))
    return node


def status_scale(mp) -> dict:
    """The status bar's scale (14.4, 13.9): hud.centerScale, which narrow views
    limit so every gauge shows; multiplayer's Competitive preset (ui_hudLayout
    3) enlarges it by 15% about the bottom edge within the same limit (the
    460 u bar across the span between the side screens)."""
    span = op("-", op("-", S("hud.width"), S("hud.left")), S("hud.right"))
    enlarged = op("min", op("*", 1.15, S("hud.scale")), op("max", 0.25, op("/", span, u(460))))
    return sel(AND(mp, op("==", S("hud.layout"), 3)), enlarged, S("hud.centerScale"))


def join_events(doc: HudDocument, pairs: list) -> list:
    """extensions.openq4.joinEvents: [stock window variable, event] pairs the
    engine peer replays when the variable holds at its join ("!" inverts),
    so state that only events carry survives a peer rebuilt mid-game (the
    layout switched to Classic and back). Each event must be one the document
    answers."""
    for variable, event in pairs:
        assert event in doc.events, f"join event {event} has no handler"
    return [list(pair) for pair in pairs]


def beyond_minimal(doc: HudDocument, ident: str, children: list) -> dict:
    """Groups the Minimal layout leaves out (section 13.9, ui_hudLayout 2).
    Minimal keeps health, armor, ammunition and the crosshair family, plus
    whatever answers or asks for the player's action: prompts, the save
    notice, chat and votes, held displays and the follow card. The wrapper
    fills its anchor, so the groups keep their coordinates."""
    node = ui.group(ident, {**ui.FULL, "display": ui.keyword("block")}, children)
    doc.show(ident, op("!=", S("hud.layout"), 2))
    return node
