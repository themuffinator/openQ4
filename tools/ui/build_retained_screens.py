#!/usr/bin/env python3
"""Build the retained title, single-player pause and loading screens.

These documents are the first stock-composition screens of the retained
interface (docs/dev/ui/retained-screens.md). Every measured value below cites
docs/dev/ui-visual-design.md: section 9 (screen reference), Appendix B.1/B.2
(textures and band silhouettes), section 4 (tokens) and section 8 (motion).

Stock measurements are source units (u) on the 640x480 canvas. The documents
declare a 720 dp view-height canvas, so 1 u = 1.5 dp at every resolution and
the 4:3 canvas is the central 960x720 dp; aspect expansion widens the flat
spans of the framing bands and the backdrop, never the composition.

The generated files are canonical sources. Re-run this script after editing
it; do not hand-edit its outputs.

    python tools/ui/build_retained_screens.py [--check]
"""

from __future__ import annotations

import types
import retained_menu_levels
import argparse
import json
import math
import re
import struct
import sys
from functools import lru_cache
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAK0 = ROOT / "content" / "baseoq4" / "pak0"
OUTPUTS = {
    "title": PAK0 / "guis" / "menu" / "title.q4ui",
    "pause": PAK0 / "guis" / "menu" / "pause.q4ui",
    "pause_strogg": PAK0 / "guis" / "menu" / "pause_strogg.q4ui",
    "loading": PAK0 / "guis" / "loading" / "loading.q4ui",
    "singleplayer": PAK0 / "guis" / "menu" / "singleplayer.q4ui",
    "campaigns": PAK0 / "guis" / "menu" / "campaigns.q4ui",
    "skill": PAK0 / "guis" / "menu" / "campaign_skill.q4ui",
    "chapters": PAK0 / "guis" / "menu" / "chapters.q4ui",
    "loadgame": PAK0 / "guis" / "menu" / "loadgame.q4ui",
    "savegame": PAK0 / "guis" / "menu" / "savegame.q4ui",
    "restartlevel": PAK0 / "guis" / "menu" / "restartlevel.q4ui",
    "loadgame_strogg": PAK0 / "guis" / "menu" / "loadgame_strogg.q4ui",
    "savegame_strogg": PAK0 / "guis" / "menu" / "savegame_strogg.q4ui",
    "restartlevel_strogg": PAK0 / "guis" / "menu" / "restartlevel_strogg.q4ui",
    "restartmenu": PAK0 / "guis" / "menu" / "restartmenu.q4ui",
    "credits": PAK0 / "guis" / "menu" / "credits.q4ui",
    "demos": PAK0 / "guis" / "menu" / "demos.q4ui",
    "arena": PAK0 / "guis" / "menu" / "arena.q4ui",
    "multiplayer": PAK0 / "guis" / "menu" / "multiplayer.q4ui",
    "createserver": PAK0 / "guis" / "menu" / "createserver.q4ui",
    "joingame": PAK0 / "guis" / "menu" / "joingame.q4ui",
    "audio": PAK0 / "guis" / "menu" / "settings" / "audio.q4ui",
    "game": PAK0 / "guis" / "menu" / "settings" / "game.q4ui",
    "system": PAK0 / "guis" / "menu" / "settings" / "system.q4ui",
    "controls": PAK0 / "guis" / "menu" / "settings" / "controls.q4ui",
    "settings": PAK0 / "guis" / "menu" / "settings.q4ui",
    "mp_escape": PAK0 / "guis" / "menu" / "mp_escape.q4ui",
    "mp_welcome": PAK0 / "guis" / "menu" / "mp_welcome.q4ui",
    "mp_summary": PAK0 / "guis" / "menu" / "mp_summary.q4ui",
    "arena_pause": PAK0 / "guis" / "menu" / "arena_pause.q4ui",
}

U = 1.5            # dp per stock source unit at the 720 dp canvas
CANVAS_W = 960.0   # the 4:3 canvas in dp
FAR = 1400.0       # flat spans reach this far past the view edges (21:9 and 32:9 views, band travel)

# Section 4 tokens.
def rgb(hex_colour: str, alpha: float = 1.0) -> list[float]:
    value = hex_colour.lstrip("#")
    return [round(int(value[i:i + 2], 16) / 255, 4) for i in (0, 2, 4)] + [alpha]

OLIVE = "#8B964B"
MARKER = "#909A49"
ORANGE = "#E38900"
VALUE = "#FFBE23"
RAIL_DARK = "#CCCC51"
RAIL_CARD = "#A0A040"
GLOW = "#616F26"
GLOW_MODAL = "#454E1B"   # popup_bg plateau (Appendix B.1)
RIM = "#626934"
PLINTH = "#3E4A21"
PROGRESS = "#E06C00"
PICTURE = "#414624"
OBJECTIVE_HEAD = "#FF8000"
OBJECTIVE_MARK = "#B0CD6B"   # an open objective's marker (the atlas pause block)
LEVEL_STATS = "#8C947A"
LOAD_SPILL = "#181D0A"
DOTS = "#1C2613"

# Accelerate-then-decelerate, the stock `accel(a, d)` profile with a = d
# (section 8): quadratic ease in-out.
ACCEL = [0.455, 0.03, 0.515, 0.955]
EASE_OUT = [0.16, 1, 0.3, 1]


# ---------------------------------------------------------------- typed values

def length(value: float, unit: str = "dp") -> dict:
    return {"type": "length", "value": round(value, 3), "unit": unit}

def colour(value: list[float]) -> dict:
    return {"type": "color", "value": value}

def keyword(value: str) -> dict:
    return {"type": "keyword", "value": value}

def number(value: float) -> dict:
    return {"type": "number", "value": value}

def font(name: str) -> dict:
    return {"type": "font", "value": name}

def text(key: str) -> dict:
    return {"type": "text", "value": key}

def image(source: str) -> dict:
    return {"type": "image", "value": source}

def transform(tx: float = 0, ty: float = 0, sx: float = 1, sy: float = 1) -> dict:
    return {"type": "transform", "unit": "dp", "value": [round(tx, 3), round(ty, 3), sx, sy, 0]}


def absolute(left=None, top=None, width=None, height=None, right=None, bottom=None) -> dict:
    props = {"position": keyword("absolute")}
    for name, value in (("left", left), ("top", top), ("width", width), ("height", height),
                        ("right", right), ("bottom", bottom)):
        if value is None:
            continue
        props[name] = value if isinstance(value, dict) else length(value)
    return props

FULL = {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%")}


# -------------------------------------------------------------------- geometry

def vx(u: float, extra: float = 0.0) -> dict:
    """An x on the 4:3 canvas, in a node spanning the whole view."""
    return {"fraction": 0.5, "dp": round(-CANVAS_W / 2 + U * u + extra, 3)}

def edge(side: int, extra: float = FAR) -> dict:
    """The view's leading (0) or trailing (1) edge, pushed further out."""
    return {"fraction": side, "dp": -extra if side == 0 else extra}

def point(x, y) -> list:
    def one(value):
        return round(value, 3) if isinstance(value, (int, float)) else value
    return [one(x), one(y)]

def path(ident: str, points: list, *, fill=None, stroke=None, closed=True, blend=None,
         fill_rule=None) -> dict:
    commands = []
    for index, (x, y) in enumerate(points):
        commands.append({"id": f"p{index}", "op": "move" if index == 0 else "line", "points": [point(x, y)]})
    if closed:
        commands.append({"id": "close", "op": "close"})
    result = {"id": ident, "commands": commands}
    if fill_rule:
        result["fillRule"] = fill_rule
    if fill is not None:
        result["fill"] = fill
    if stroke is not None:
        result["stroke"] = stroke
    if blend:
        result["blend"] = blend
    return result

def solid(value: list[float]) -> dict:
    return {"type": "solid", "color": colour(value)}

def linear(start, end, stops) -> dict:
    return {"type": "linear", "from": point(*start), "to": point(*end),
            "stops": [{"at": at, "color": colour(value)} for at, value in stops]}

def stroke(paint: dict, width: float, *, minimum: float = 1, join: str = "miter", cap: str = "butt") -> dict:
    return {"paint": paint, "widthDp": round(width, 3), "minimumPixels": minimum, "join": join, "cap": cap}


# ----------------------------------------------------------------------- nodes

def group(ident: str, props: dict, children=(), **extra) -> dict:
    node = {"id": ident, "type": "group", "properties": props}
    if children:
        node["children"] = list(children)
    node.update(extra)
    return node

def walk_nodes(node: dict):
    """`node` and every node under it, depth first."""
    yield node
    for child in node.get("children", ()):
        yield from walk_nodes(child)


TEXT_STATE = "view.text"   # the runtime's view source: the text size it applies to font lengths, 1 to 2
TEXT_LENGTHS = ("font-size", "line-height", "letter-spacing")   # the lengths the runtime grows by the text size itself


def fixed_text(doc: "Document", node: dict) -> None:
    """Text in fixed chrome (a prompt bar's key caps, a tab strip's slots, a
    plate's label) keeps its authored size at every text size: each font
    length under `node` divides the text size the runtime multiplies it by,
    unless something already binds or animates it."""
    doc.state[TEXT_STATE] = {"type": "number", "initial": 1, "view": "text"}
    owned = {(binding["node"], binding["property"]) for binding in doc.bindings}
    owned.update((entry["node"], entry["property"]) for timeline in doc.timelines.items for entry in timeline["tracks"])
    for item in walk_nodes(node):
        for key in TEXT_LENGTHS:
            found = item.get("properties", {}).get(key)
            if not found or found.get("type") != "length" or found.get("unit") != "dp" or (item["id"], key) in owned:
                continue
            doc.bind(f"text.{item['id']}.{key}", item["id"], key, {"op": "/", "args": [found["value"], {"state": TEXT_STATE}]})


# Section 2's text-scale contract on the page-level menus: titles and the
# labels of plates, plinths, prompt bars and card headings name furniture and
# keep their size (fixed_text); descriptions, message lines, dialog bodies and
# a card's details are content, whose boxes grow with the text above 100 %.
# Every binding below gives its node's authored value at 100 %, so those
# documents present as they did (check_text_bindings).
TEXT = {"state": TEXT_STATE}
LARGER = {"op": ">", "args": [TEXT, 1]}


def text_source(doc: "Document") -> dict:
    """The document's view.text source, declared once; its value."""
    doc.state[TEXT_STATE] = {"type": "number", "initial": 1, "view": "text"}
    return TEXT


def text_times(value):
    """`value` (dp, or an expression) times the text size."""
    return {"op": "*", "args": [value, TEXT]}


def text_sum(*terms):
    """The sum of numbers and expressions, the numbers folded into one."""
    constant = sum(term for term in terms if isinstance(term, (int, float)))
    parts = [term for term in terms if not isinstance(term, (int, float))]
    result = constant if not parts or constant else None
    for part in parts:
        result = part if result is None else {"op": "+", "args": [result, part]}
    return result


def text_buckets(values: dict) -> dict:
    """A text size's value from `values` {1.5: a, 2: b}: a up to 150 %, b
    above it, the larger size's line counts covering every size between."""
    return {"op": "select", "args": [{"op": ">", "args": [TEXT, 1.5]}, values[2], values[1.5]]}


def per_language(doc: "Document", values: dict):
    """`values[language]` for the running language (sys_lang), the largest
    of them for any other, as the plinth's width is chosen."""
    if "language" not in doc.state:
        doc.state["language"] = {"type": "string", "initial": "", "cvar": "sys_lang"}
    widest = max(values.values())
    result = widest
    for language in sorted(values, reverse=True):
        if values[language] != widest:
            result = {"op": "select", "args": [{"op": "==", "args": [{"state": "language"}, language]}, values[language], result]}
    return result


def text_lines(face: str, text: str, size: float, width: float) -> int:
    """Lines `text` takes at `width` dp, broken at spaces as the host breaks it."""
    lines, current = 1, ""
    for word in text.split(" "):
        candidate = f"{current} {word}" if current else word
        if current and text_width(face, candidate, size) > width:
            lines, current = lines + 1, word
        else:
            current = candidate
    return lines


def wrapped_lines(keys, face: str, size: float, width: float) -> dict:
    """{language: {1: lines, 1.5: lines, 2: lines}}: the most lines any of
    `keys` takes at `width` dp in each shipped language at 100, 150 and 200 %
    text."""
    texts = [merged_text(key) for key in keys]
    return {language: {scale: max(text_lines(face, found.get(language) or found["english"], size * scale, width) for found in texts)
                       for scale in (1, 1.5, 2)} for language in LANGUAGES}


def evaluate(expression, state: dict):
    """A binding expression's value for `state` (check_text_bindings)."""
    if not isinstance(expression, dict):
        return expression
    if "state" in expression:
        return state[expression["state"]]
    args = [evaluate(arg, state) for arg in expression["args"]]
    op = expression["op"]
    if op == "select":
        return args[1] if args[0] else args[2]
    if op == "!":
        return not args[0]
    operations = {"+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b, "/": lambda a, b: a / b,
                  "min": min, "max": max, "==": lambda a, b: a == b, "!=": lambda a, b: a != b, "<": lambda a, b: a < b,
                  "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b,
                  "&&": lambda a, b: a and b, "||": lambda a, b: a or b}
    return operations[op](*args)


DETAIL_CEILING = 60.0    # u: a page's content column rises no higher, under the docked top band
DETAIL_FLOOR = 420.0     # u: and runs down to here, over the docked bottom band's edge
DETAIL_LEFT = 395.0      # u: the stock content column's leading edge
DETAIL_WIDE = 224.0      # u: from 395 u to the 4:3 view's 32 dp margin


def reflow_details(doc: "Document", details: dict, keys) -> None:
    """A page's content column (section 9) above 100 % text: its description
    (each of `details`' labels, one of `keys` showing) widens to the 4:3
    view's margin and its lines grow with the text. The column keeps its
    place beside the first plate while its lines fit above the docked bottom
    band, and otherwise rises as far as the running language needs, never
    past the docked top band. Generation fails where a shipped language's
    description would outgrow that. `details` may be the description's own
    label, in a column that starts elsewhere than 395 u (the campaign's
    difficulty): it runs on to the same margin."""
    props = details["properties"]
    top, width, height = props["top"]["value"], props["width"]["value"], props["height"]["value"]
    labels = [details] if details.get("type") == "text" else [child for child in details["children"] if child.get("type") == "text"]
    style = labels[0]["properties"]
    face, size, line = style["font-family"]["value"], style["font-size"]["value"], style["line-height"]["value"]
    wide, floor, ceiling = U * (DETAIL_LEFT + DETAIL_WIDE) - props["left"]["value"], U * DETAIL_FLOOR, U * DETAIL_CEILING
    counts = wrapped_lines(keys, face, size, wide)
    for language, found in counts.items():
        if found[2] * line * 2 > floor - ceiling:
            raise SystemExit(f"{doc.ident}: a description in {language} needs {found[2]} lines at 200 % text, more than the column holds")
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    text_source(doc)
    risen = {"op": "min", "args": [top, {"op": "-", "args": [floor, text_times({"op": "*", "args": [lines, line]})]}]}
    tall = {"op": "-", "args": [floor, risen]}
    for node in [details, *(label for label in labels if label is not details)]:
        ident, own = node["id"], node["properties"]
        if node is details:
            doc.bind(f"text.{ident}.top", ident, "top", {"op": "select", "args": [LARGER, risen, own["top"]["value"]]})
        doc.bind(f"text.{ident}.width", ident, "width", {"op": "select", "args": [LARGER, wide, own["width"]["value"]]})
        doc.bind(f"text.{ident}.height", ident, "height", {"op": "select", "args": [LARGER, tall, own["height"]["value"]]})
    # Where the column starts above 100 % text, for what stands over it.
    return {"op": "select", "args": [LARGER, risen, top]}


# A page section's summary (section 13.7: a section that leads deeper
# summarizes itself in the content column, with a levelshot where one fits,
# until Accept opens it): its sentence in the description's place and, where
# the session publishes one, a levelshot over the column. The picture is
# 4:3 like the levelshots, 8 u over the column, under the docked top band.
SUMMARY_SHOT = (395.0, 77.0, 140.0, 105.0)   # u: left, top, width, height
# The sections' sentences, in every shipped language.
SUMMARY_CAMPAIGN, SUMMARY_ARENA = "#str_231552", "#str_231553"
SUMMARY_JOIN, SUMMARY_CREATE, SUMMARY_DEMOS, SUMMARY_SETUP = "#str_231554", "#str_231555", "#str_231556", "#str_231557"
SUMMARY_CONTROLS, SUMMARY_GAME, SUMMARY_DISPLAY, SUMMARY_AUDIO = "#str_231571", "#str_231572", "#str_231573", "#str_231574"
SUMMARY_NEWGAME, SUMMARY_CHAPTERS = "#str_231575", "#str_231576"


# The section the pointer is over, else the focused one, as the runtime writes
# it (the document's detail definition, without a card): its index, or -1.
SUMMARY_STATE = "summary.section"


def summary_id(ident: str) -> str:
    return f"summary-{ident}"


def section_summaries(doc: Document, details: dict, sections: list) -> tuple:
    """Each of `sections` (plate ident, sentence key, the state naming its
    levelshot or None) gets its summary: a label in `details`, laid as the
    description is, and where it has a levelshot a picture over the column.
    A control's feedback reaches only its own subtree, so the plates name
    their sections (`summary_subject`) and the runtime writes the subject's
    to SUMMARY_STATE: its summary shows, and the description (each of the
    column's labels, as many as the page has) shows for any other plate, or
    with no subject. Returns the summary keys, for the column's fit and
    reflow, and the pictures' groups, which `reflowed` (reflow_details'
    result) and the tall view keep clear of the text."""
    from retained_saves import picture_frame
    doc.state[SUMMARY_STATE] = {"type": "number", "initial": -1}
    doc.detail = {"state": SUMMARY_STATE, "none": -1}

    def shown(index: int) -> dict:
        return {"op": "select", "args": [{"op": "==", "args": [{"state": SUMMARY_STATE}, index]}, 1, 0]}
    descriptions = [child for child in details["children"] if child.get("type") == "text"]
    for description in descriptions:
        description["properties"]["opacity"] = number(1)
        doc.bind(f"{description['id']}.opacity", description["id"], "opacity", shown(-1))
    # Laid as the first is, shown whatever shows it.
    style = {key: value for key, value in descriptions[0]["properties"].items() if key not in ("text", "display")}
    shots = []
    for index, (ident, key, shot_state) in enumerate(sections):
        details["children"].append(label(summary_id(ident), key, {**style, "opacity": number(0)}))
        doc.bind(f"{summary_id(ident)}.opacity", summary_id(ident), "opacity", shown(index))
        if shot_state:
            doc.state[shot_state] = {"type": "string", "initial": ""}
            left, top, width, height = SUMMARY_SHOT
            frame = picture_frame(f"{summary_id(ident)}-shot", left, top, width, height)
            shots.append(group(f"{summary_id(ident)}-shots", {**absolute(left=0, top=0, width=CANVAS_W, height=720),
                                                                "opacity": number(0), "pointer-events": keyword("none")}, frame))
            doc.bind(f"{summary_id(ident)}-shots.opacity", f"{summary_id(ident)}-shots", "opacity", shown(index))
            doc.bind(f"{summary_id(ident)}-shot.image", f"{summary_id(ident)}-shot", "image", {"state": shot_state})
    return [key for _, key, _ in sections], shots


def summary_subject(ident: str, sections: list) -> int | None:
    """The detail a section's plate names: its index in `sections`."""
    return next((index for index, (section, _key, _shot) in enumerate(sections) if section == ident), None)


def fit_summary_shots(doc: Document, shots: list, sections: list, reflowed) -> None:
    """A section's levelshot shows while the session names one, while the
    column's text, grown, stays clear of it, and never in a tall view, where
    the column stands under the plates."""
    _left, top, _width, height = SUMMARY_SHOT
    clear = {"op": ">=", "args": [reflowed, U * (top + height + 8)]}
    for node, (ident, _key, shot_state) in zip(shots, [s for s in sections if s[2]]):
        named = {"op": "!=", "args": [{"state": shot_state}, ""]}
        node["properties"]["display"] = keyword("none")
        doc.bind(f"{node['id']}.display", node["id"], "display",
                 {"op": "select", "args": [{"op": "&&", "args": [named, clear]}, "block", "none"]})
        tall_bind(doc, node, "display", "none")


# Tall views of the menu levels (sections 3 and 13.3, "adapt, don't shrink"):
# in a tall view (the runtime's view.aspect source) a page stands its parts
# one under the other in a single column across the 960 dp canvas, scaled
# together as large as the view's height between the bands allows, up to
# TALL_MOST, instead of shrinking the 4:3 composition into a band. TALL is the
# 3:4 condition of the arrangements that grow a fixed amount; a menu level's
# column, which scales to the room, takes its own condition (tall_condition):
# any portrait view in which the column reads at least at its landscape size.
ASPECT_STATE = "view.aspect"
TALL_ASPECT, PORTRAIT_ASPECT = 0.76, 1.0
TALL = {"op": "<", "args": [{"state": ASPECT_STATE}, TALL_ASPECT]}
TALL_MOST = 1.6                       # the wordmark and the plates' labels across the canvas
VIEW_HEIGHT = {"op": "/", "args": [CANVAS_W, {"state": ASPECT_STATE}]}   # dp: a view narrower than 4:3 keeps the canvas 960 dp wide
TALL_MARGIN = 66.0                    # dp: the column's trailing margin, the plates' label inset on the leading side


def tall_source(doc: "Document") -> dict:
    """The document's view.aspect source, declared once; its tall condition."""
    doc.state[ASPECT_STATE] = {"type": "number", "initial": round(4 / 3, 4), "view": "aspect"}
    return tall_condition(doc)


def tall_condition(doc: "Document") -> dict:
    """The document's own tall condition, every binding's one object: 3:4 and
    taller until tall_column raises it (tall_fits)."""
    if getattr(doc, "tall", None) is None:
        doc.tall = {"op": "<", "args": [{"state": ASPECT_STATE}, TALL_ASPECT]}
        doc.tall_fits = []
    return doc.tall


def tall_fits(doc: "Document", height: float) -> None:
    """A column `height` dp tall, bands included, at its landscape size: the
    document stands in its column in any portrait view tall enough for that
    (a view of 3:4 and taller in any case), the least of its columns'."""
    condition = tall_condition(doc)
    doc.tall_fits.append(CANVAS_W / height)
    condition["args"][1] = round(max(TALL_ASPECT, min(PORTRAIT_ASPECT, *doc.tall_fits)), 4)


def is_tall(condition) -> bool:
    """A tall view's condition, the fixed 3:4 one or a document's own."""
    return (isinstance(condition, dict) and condition.get("op") == "<" and len(condition.get("args", ())) == 2 and
            condition["args"][0] == {"state": ASPECT_STATE})


def tall_bind(doc: "Document", node: dict, key: str, tall) -> None:
    """`node`'s `key` in a tall view: `tall`, or `tall(value)` of what it is
    in any other view (its binding's value, else its authored value), which
    it keeps there."""
    for binding in doc.bindings:
        if binding["node"] == node["id"] and binding["property"] == key:
            value = binding["value"]
            if isinstance(value, dict) and value.get("op") == "select" and is_tall(value["args"][0]):
                value = value["args"][2]       # a later tall value replaces an earlier one
            binding["value"] = {"op": "select", "args": [tall_condition(doc), tall(value) if callable(tall) else tall, value]}
            return
    authored = node["properties"][key]["value"]
    doc.bind(f"tall.{node['id']}.{key}", node["id"], key,
             {"op": "select", "args": [tall_condition(doc), tall(authored) if callable(tall) else tall, authored]})


def tall_scale(doc: "Document", node: dict, scale, own: tuple = ()) -> None:
    """Every dp length under `node` (its descendants, and its own `own`
    lengths) times `scale` in a tall view, text sizes included, but for those
    a timeline animates or the runtime writes. Vector art in a dp box keeps
    its box and takes the scale as a transform (an identity transform in any
    other view, so its art draws exactly as before); in a box of percentages
    its art turns to fractions of the box, to follow it."""
    from retained_settings_rows import _runtime_owned
    skipped = {(entry["node"], entry["property"]) for timeline in doc.timelines.items for entry in timeline["tracks"]}
    _runtime_owned(node, skipped)
    bound = {(binding["node"], binding["property"]) for binding in doc.bindings}

    def current(item: dict, key: str):
        return next((binding["value"] for binding in doc.bindings if binding["node"] == item["id"] and binding["property"] == key),
                    item["properties"][key]["value"])

    def tall_or(value, rest):
        return {"op": "select", "args": [tall_condition(doc), value, rest]}

    def walk(item: dict, parent: tuple) -> None:
        props = item.get("properties", {})
        size = []
        for key, extent in (("width", parent[0]), ("height", parent[1])):
            found = props.get(key) or {}
            unit = found.get("unit") if found.get("type") == "length" else None
            size.append(found["value"] if unit == "dp" else found["value"] * extent / 100 if unit == "%" and extent else None)
        kept = set()
        if item is not node and item.get("type") == "vector" and size[0] and size[1]:
            boxed = all(props[key].get("unit") == "dp" for key in ("width", "height"))
            if boxed and "transform" not in props and (item["id"], "transform") not in skipped:
                # Scaling acts about the box's centre: its leading and top
                # edges stay where the scaled left and top put them.
                half = [{"op": "*", "args": [{"op": "-", "args": [scale, 1]}, {"op": "*", "args": [0.5, current(item, key)]}]}
                        for key in ("width", "height")]
                props["transform"] = transform()
                doc.bind(f"tall.{item['id']}.transform", item["id"], "transform",
                         [tall_or(half[0], 0), tall_or(half[1], 0), tall_or(scale, 1), tall_or(scale, 1), 0])
                kept = {"width", "height"}
            else:
                in_fractions(item, *size)
        for key, found in list(props.items()):
            # A zero length scales only where a text binding moves it.
            moved = found.get("value") or (item["id"], key) in bound
            if (found.get("type") == "length" and found.get("unit") == "dp" and moved and (item["id"], key) not in skipped
                    and key not in kept and (item is not node or key in own or key in TEXT_LENGTHS)):
                tall_bind(doc, item, key, lambda value: {"op": "*", "args": [scale, value]})
        for child in item.get("children", []):
            walk(child, tuple(size))
    walk(node, (None, None))


def in_fractions(node: dict, width: float, height: float) -> None:
    """A vector's art in fractions of its `width` x `height` box, unrounded,
    so that it scales with the box and stands exactly where it did in it."""
    def convert(value, extent):
        if isinstance(value, (int, float)):
            return {"fraction": value / extent}
        if isinstance(value, dict) and "dp" in value:
            return {"fraction": value.get("fraction", 0) + value["dp"] / extent}
        return value

    def point(at):
        return [convert(at[0], width), convert(at[1], height)]
    for drawn in node.get("paths", []):
        for command in drawn["commands"]:
            if "points" in command:
                command["points"] = [point(at) for at in command["points"]]
        for paint in (drawn.get("fill"), (drawn.get("stroke") or {}).get("paint")):
            if paint and paint.get("type") == "linear":
                paint["from"], paint["to"] = point(paint["from"]), point(paint["to"])


def tall_dialog(doc: "Document", modal: dict, scale: float = TALL_MOST) -> None:
    """A confirmation (confirmation()) in a tall view: `scale` times its size,
    centred where it was, so it reads at the column's scale."""
    dialog = next(node for node in walk_nodes(modal) if node["id"] == f"{modal['id']}-dialog")
    props = dialog["properties"]
    width, height, top = props["width"]["value"], props["height"]["value"], props["top"]["value"]
    grown = next((binding["value"] for binding in doc.bindings if binding["node"] == dialog["id"] and binding["property"] == "height"), height)
    tall_scale(doc, dialog, scale)
    tall_bind(doc, dialog, "left", round((CANVAS_W - width * scale) / 2, 4))
    tall_bind(doc, dialog, "width", round(width * scale, 4))
    tall_bind(doc, dialog, "height", {"op": "*", "args": [scale, grown]})
    tall_bind(doc, dialog, "top", text_sum(top + height / 2, {"op": "*", "args": [-scale / 2, grown]}))


def tall_canvas(doc: "Document", node: dict) -> None:
    """A canvas group that fit_narrow centres on the view's middle runs the
    view's full height in a tall view, so its parts take view coordinates."""
    doc.bind(f"tall.{node['id']}.margin-top", node["id"], "margin-top",
             {"op": "select", "args": [tall_condition(doc), {"op": "/", "args": [-CANVAS_W / 2, {"state": ASPECT_STATE}]}, -360]})
    doc.bind(f"tall.{node['id']}.height", node["id"], "height", {"op": "select", "args": [tall_condition(doc), VIEW_HEIGHT, 720]})


def tall_column(doc: "Document", parts: list, top: float, bottom: float) -> tuple:
    """The tall column: `parts` (height, node or None for a gap) stand one
    under the other from the band's foot at `top` dp to `bottom` dp above
    the view's foot, centred in it. Heights are in dp at 100 % scale and may
    follow the text size and CONTINUE; the scale S is the largest, up to
    TALL_MOST, at which the tallest of them (200 % text) fits the view, so it
    follows the view alone. Binds each node's top; returns S and each part's
    top, in view dp."""
    tall_source(doc)
    room = text_sum(VIEW_HEIGHT, -(top + bottom))
    height = text_sum(*[part_height for part_height, _ in parts])
    tallest = max(evaluate(height, {TEXT_STATE: 2, "menu_continue": shown}) for shown in (False, True))
    tall_fits(doc, tallest + top + bottom)
    scale = {"op": "min", "args": [TALL_MOST, {"op": "/", "args": [room, round(tallest, 4)]}]}
    y = text_sum(top, {"op": "*", "args": [0.5, {"op": "-", "args": [room, {"op": "*", "args": [scale, height]}]}]})
    tops = []
    for part_height, part in parts:
        tops.append(y)
        if part is not None:
            tall_bind(doc, part, "top", y)
        y = text_sum(y, {"op": "*", "args": [scale, part_height]})
    return scale, tops


def tall_text_height(doc: "Document", keys, face: str, size: float, line: float, width: float):
    """The dp a text of `keys` needs at 100 % scale in `width` dp, at the
    running text size: the most lines any shipped language takes, at 100,
    150 or 200 % text (a larger size's count covering the sizes below it)."""
    counts = {scale: max(found[scale] for found in wrapped_lines(keys, face, size, width).values()) for scale in (1, 1.5, 2)}
    lines = {"op": "select", "args": [{"op": ">", "args": [TEXT, 1.5]}, counts[2],
                                      {"op": "select", "args": [{"op": ">", "args": [TEXT, 1]}, counts[1.5], counts[1]]}]}
    text_source(doc)
    return text_times({"op": "*", "args": [lines, line]})


def tall_page(doc: "Document", canvas: dict, navigation: dict, details: dict, keys) -> None:
    """A menu level with a content column (SETTINGS, Single Player, Campaign,
    Multiplayer) in a tall view: its plates at the column's scale across the
    canvas, the description under them over the rest of its width, both in
    the room between the docked bands."""
    tall_canvas(doc, canvas)
    labels = [child for child in details["children"] if child.get("type") == "text"]
    style = labels[0]["properties"]
    face, size, line = style["font-family"]["value"], style["font-size"]["value"], style["line-height"]["value"]
    # The description's width at the largest scale, in 100 % dp.
    narrowest = (CANVAS_W - TALL_MARGIN * (1 + TALL_MOST)) / TALL_MOST
    plates = sum(child["properties"]["height"]["value"] for child in navigation["children"])
    described = tall_text_height(doc, keys, face, size, line, narrowest)
    scale, _ = tall_column(doc, [(plates, navigation), (24, None), (described, details)], TALL_PAGE_TOP, TALL_PAGE_BOTTOM)
    tall_scale(doc, navigation, scale)
    tall_bind(doc, navigation, "width", {"op": "*", "args": [scale, navigation["properties"]["width"]["value"]]})
    width = text_sum(CANVAS_W - TALL_MARGIN, {"op": "*", "args": [-TALL_MARGIN, scale]})
    for node in [details, *labels]:
        tall_bind(doc, node, "width", width)
        tall_bind(doc, node, "height", {"op": "*", "args": [scale, described]})
    tall_bind(doc, details, "left", {"op": "*", "args": [TALL_MARGIN, scale]})
    for node in labels:
        for key in ("font-size", "line-height"):
            tall_bind(doc, node, key, {"op": "*", "args": [scale, node["properties"][key]["value"]]})


TALL_PAGE_TOP, TALL_PAGE_BOTTOM = 110.0, 100.0   # dp: under the docked top band's title, over the docked bottom band


def check_text_bindings(result: dict) -> None:
    """Fail generation unless every binding that reads the text size gives
    its node's authored value at 100 % in a view of 4:3 and wider, in every
    shipped language and for either value of each boolean state it reads."""
    nodes = {node["id"]: node for node in walk_nodes(result["root"])}
    declared = result.get("state", {})

    def reads(expression, found: set) -> set:
        if isinstance(expression, dict):
            if "state" in expression:
                found.add(expression["state"])
            for arg in expression.get("args", []):
                reads(arg, found)
        return found
    def landscape(expression):
        # The value in a view of 4:3 and wider: a tall view's branch aside.
        if isinstance(expression, dict) and expression.get("op") == "select" and is_tall(expression["args"][0]):
            return landscape(expression["args"][2])
        if isinstance(expression, dict) and "args" in expression:
            return {**expression, "args": [landscape(arg) for arg in expression["args"]]}
        if isinstance(expression, list):
            return [landscape(arg) for arg in expression]
        return expression
    for binding in result.get("bindings", []):
        value = landscape(binding["value"])
        names = reads(value, set())
        if TEXT_STATE not in names:
            continue
        authored = nodes[binding["node"]]["properties"].get(binding["property"])
        if authored is None:
            raise SystemExit(f"{result['id']}: {binding['id']} binds {binding['property']} without an authored value")
        flags = sorted(name for name in names if declared.get(name, {}).get("type") == "boolean")
        for language in LANGUAGES:
            for mask in range(1 << len(flags)):
                state = {name: declared[name]["initial"] for name in names if name in declared}
                state.update({TEXT_STATE: 1, "language": language})
                state.update({name: bool(mask >> index & 1) for index, name in enumerate(flags)})
                found = evaluate(value, state)
                expected = authored["value"]
                if found != expected:
                    raise SystemExit(f"{result['id']}: {binding['id']} gives {found!r} at 100 % text, not the authored {expected!r}")


def vector(ident: str, props: dict, paths: list, children=(), **extra) -> dict:
    node = {"id": ident, "type": "vector", "properties": props, "paths": paths}
    if children:
        node["children"] = list(children)
    node.update(extra)
    return node

def label(ident: str, key: str, props: dict) -> dict:
    return {"id": ident, "type": "text", "properties": {"text": text(key), **props}}

def picture(ident: str, source: str, props: dict, fit: str = "cover") -> dict:
    return {"id": ident, "type": "image", "properties": {"image": image(source), "image-fit": keyword(fit), **props}}


def typeface(face: str, size: float, line: float, tint: list[float], **more) -> dict:
    props = {"font-family": font(face), "font-size": length(size), "line-height": length(line), "color": colour(tint)}
    props.update(more)
    return props


# ------------------------------------------------------------------- timelines

class Timelines:
    def __init__(self):
        self.items: list[dict] = []

    def add(self, ident: str, duration: float, tracks: list, *, iterations: int | None = None,
            essential: bool | None = None, complete: str | None = None) -> str:
        """`complete` names an event the runtime runs when the timeline plays
        to its end; cancellation and a takeover of every track never complete."""
        timeline = {"id": ident, "durationMs": duration, "tracks": tracks}
        if iterations is not None:
            timeline["iterations"] = iterations
        if essential is not None:
            timeline["essential"] = essential
        if complete is not None:
            timeline["complete"] = complete
        self.items.append(timeline)
        return ident


def track(node: str, prop: str, keys: list) -> dict:
    """keys: (ms, typed value[, easing of the interval starting here])."""
    result = []
    for key in keys:
        entry = {"atMs": key[0], "value": key[1]}
        if len(key) > 2 and key[2] is not None:
            entry["easing"] = key[2]
        result.append(entry)
    return {"node": node, "property": prop, "keys": result}


# --------------------------------------------------------------- framing bands

# Appendix B.2 inner edges on the canvas, home state (band-local plus origin).
TOP_HOME = [(-297.9, 89.7), (-283.7, 103.8), (30.7, 103.8), (40.9, 94.7), (76.6, 94.7), (85.8, 103.8),
            (127.6, 103.8), (137.8, 94.7), (172.5, 94.7), (181.7, 103.8), (223.5, 103.8), (233.7, 94.7),
            (268.4, 94.7), (277.6, 103.8), (315.4, 103.8), (352.1, 68.5)]
BOTTOM_HOME = [(-232.7, 421.5), (-222.5, 430.6), (-28.6, 430.6), (2.1, 400.4), (344.9, 400.4),
               (389.8, 443.7), (582.7, 443.7), (591.9, 434.6)]
# Page dock minus home dock (Appendix B.2): top (-77,-63) - (-400,0), bottom (-26,386) - (-399,351).
TOP_TO_PAGE = (323 * U, -63 * U)
BOTTOM_TO_PAGE = (373 * U, 35 * U)


def band_edge(points: list, lead_y: float, trail_y: float) -> list:
    inner = [(edge(0), U * lead_y)]
    inner += [(vx(x), U * y) for x, y in points]
    inner.append((edge(1), U * trail_y))
    return inner


def rim(edge_points: list, tint: str = RIM, peak: float = 0.37, reach: float = 19.0) -> list:
    """marine.rim: 0.37 falling to nothing across 19 u outside the inner edge,
    measured from the edge (section 6). Ten nested strokes of equal alpha
    accumulate a near-linear falloff; the opaque band fill hides their inner halves."""
    paths = []
    steps = 10
    alpha = 1 - (1 - peak) ** (1 / steps)
    for index in range(steps, 0, -1):
        width = 2 * reach * U * index / steps
        paths.append(path(f"rim-{index}", edge_points, closed=False,
                          stroke=stroke(solid(rgb(tint, round(alpha, 4))), width, minimum=0, join="round")))
    return paths


def framing_bands(prefix: str, family: dict | None = None) -> list:
    """The framing bands with their rim light. `family` swaps the Marine
    silhouettes and rim for another family's (the Strogg pause)."""
    family = family or {}
    top_edge = band_edge(family.get("top", TOP_HOME), *family.get("top_ends", (89.7, 68.5)))
    bottom_edge = band_edge(family.get("bottom", BOTTOM_HOME), *family.get("bottom_ends", (421.5, 434.6)))
    lit = {key: family[key] for key in ("tint", "peak", "reach") if key in family}
    top_fill = [(edge(1), -400)] + [(edge(0), -400)] + top_edge
    bottom_fill = bottom_edge + [(edge(1), U * 480 + 400), (edge(0), U * 480 + 400)]
    top = vector(f"{prefix}-top", {**FULL, "transform": transform()},
                 rim(top_edge, **lit) + [path("band", top_fill, fill=solid([0, 0, 0, 1]))] + family.get("etched_top", []))
    bottom = vector(f"{prefix}-bottom", {**FULL, "transform": transform()},
                    rim(bottom_edge, **lit) + [path("band", bottom_fill, fill=solid([0, 0, 0, 1]))] + family.get("etched_bottom", []))
    return [top, bottom]


# --------------------------------------------------------------------- backdrop

def grid_path(opacity: float = 0.04, tint: list[float] | None = None, arm_u: float = 6.5) -> dict:
    """The `+` reticle grid: 13 u crosses on a 30 u pitch (section 2, trait 1).
    It covers views up to 21:9 (-240 to 880 u), the side-screen cap of section
    14.4; wider views keep the backdrop and bands but not the faint grid."""
    commands = []
    count = 0
    arm = arm_u * U
    for column in range(-8, 29):
        for row in range(0, 16):
            cx = vx(15 + 30 * column)
            cy = U * (15 + 30 * row)
            for (ax, ay), (bx, by) in (((-arm, 0), (arm, 0)), ((0, -arm), (0, arm))):
                commands.append({"id": f"m{count}", "op": "move", "points": [[{"fraction": 0.5, "dp": round(cx["dp"] + ax, 3)}, round(cy + ay, 3)]]})
                commands.append({"id": f"l{count}", "op": "line", "points": [[{"fraction": 0.5, "dp": round(cx["dp"] + bx, 3)}, round(cy + by, 3)]]})
                count += 1
    return {"id": "crosses", "commands": commands,
            "stroke": stroke(solid((tint or [1, 1, 1])[:3] + [opacity]), 1.0, minimum=1)}


def lit_field(prefix: str, peak: float, glow: str = GLOW, grid: dict | None = None) -> list:
    """Vignette, additive light band and reticle grid of the Marine menus;
    `glow` and `grid` give another family's light and crosses."""
    vignette = vector(f"{prefix}-vignette", dict(FULL), [path("shade", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
        fill=linear((0, 0), (0, {"fraction": 1}), [(0, [0, 0, 0, 0.97]), (0.3, [0, 0, 0, 0]), (0.7, [0, 0, 0, 0]), (1, [0, 0, 0, 0.97])]))])
    light = vector(f"{prefix}-light", dict(FULL), [path("band", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
        fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(glow, 0)), (0.1, rgb(glow, 0)), (0.5, rgb(glow, peak)), (0.9, rgb(glow, 0)), (1, rgb(glow, 0))]),
        blend="additive")])
    grid = vector(f"{prefix}-grid", dict(FULL), [grid_path(**(grid or {}))])
    return [vignette, light, grid]


# -------------------------------------------------------------------- controls

def marker_path(ident: str, x: float, y: float, size: float, tint: list[float]) -> dict:
    """The upper-trailing triangle marker (section 2, trait 5)."""
    return path(ident, [(x, y), (x + size, y), (x + size, y + size)], fill=solid(tint))


class Document:
    def __init__(self, ident: str):
        self.ident = ident
        self.timelines = Timelines()
        self.state: dict = {}
        self.actions: dict = {}
        self.events: dict = {}
        self.bindings: list = []
        # Vector art several nodes draw (a vector node's "symbol" names one): ID -> paths.
        self.symbols: dict = {}
        # The document's detail card (state, card, none): see Document.h's DetailSpec.
        self.detail: dict | None = None

    def bind(self, ident: str, node: str, prop: str, value) -> None:
        self.bindings.append({"id": ident, "node": node, "property": prop, "value": value})

    def session(self, action: str, command: str) -> str:
        self.actions[action] = {"operation": "session.menu", "arguments": {"command": command}}
        return action

    def states(self, control: str, tracks: dict) -> dict:
        """Button feedback timelines. `tracks` maps state -> [(node, prop, value)].
        Hover responds at once (hover.enter 0 ms: a 1 ms step, the shortest a
        timeline can run) and focus over 80 ms; every return to rest is linear
        over 300 ms (hover.leave); press insets 1 dp over 60 ms (section 8)."""
        durations = {"default": 300, "hover": 1, "focus": 80, "pressed": 60, "disabled": 150}
        ids = {}
        for state, duration in durations.items():
            ident = f"{control}.{state}"
            keys = []
            for node, prop, value in tracks[state]:
                ease = None if state == "default" else EASE_OUT
                keys.append(track(node, prop, [(0, value, ease), (duration, value)]))
            self.timelines.add(ident, duration, keys)
            ids[state] = ident
        return ids

    def build(self, root: dict, extras: dict | None = None) -> dict:
        result = {"format": "openq4-ui", "version": 1, "id": self.ident, "canvas": {"height": 720}}
        if self.state:
            result["state"] = self.state
        if self.actions:
            result["actions"] = self.actions
        if self.events:
            result["events"] = self.events
        if self.bindings:
            result["bindings"] = self.bindings
        if self.detail:
            result["detail"] = self.detail
        if self.symbols:
            result["symbols"] = self.symbols
        result["timelines"] = self.timelines.items
        result["root"] = root
        if extras:
            result.update(extras)
        return result


NAV_LEAD = 900.0  # a navigation button starts this far left of the canvas, past any view's leading edge


def navigation_plate(doc: Document, ident: str, key: str, *, action: str | None = None,
                     event: str | None = None, detail: dict | None = None, subject: int | None = None) -> dict:
    """A dark navigation plate that bleeds off the leading edge (section 9):
    b1_dark drawn at 413x52 u on a 30 u pitch, its visible band rows 16-45 of
    64, opaque to 44 %, half at 62 %, gone by 88 % of the width (Appendix B.1);
    the corner marker at 32 u and a Marine 24 dp label at 44 u. The whole
    30 u pitch is the target, from the view's leading edge to 357 u. `detail`
    is the item's message-line explanation, shown while it has hover or focus;
    `subject` the detail it names, a page section's (section_summaries)."""
    lead = NAV_LEAD
    width = lead + 357 * U
    x0 = lead                                     # canvas x = 0 in button coordinates
    fade = [(0, [0, 0, 0, 1]), (0.44, [0, 0, 0, 1]), (0.62, [0, 0, 0, 0.5]), (0.88, [0, 0, 0, 0]), (1, [0, 0, 0, 0])]
    rail = [(0, rgb(RAIL_DARK)), (0.44, rgb(RAIL_DARK)), (0.62, rgb(RAIL_DARK, 0.5)), (0.88, rgb(RAIL_DARK, 0)), (1, rgb(RAIL_DARK, 0))]
    # Rows are 30 u windows centred on b1_dark's visible band (215-239.4 u for
    # the first row, drawn at 202 u): the band sits 2.8-27.2 u into its window.
    top = U * 2.8
    bottom = U * 27.2
    span = 413 * U
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0.4)}, [
        path("fill", [(0, top), (x0 + span, top), (x0 + span, bottom), (0, bottom)],
             fill=linear((x0, 0), (x0 + span, 0), fade)),
        path("rail", [(0, bottom - 0.75), (x0 + span, bottom - 0.75)], closed=False,
             stroke=stroke(linear((x0, 0), (x0 + span, 0), rail), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)}, [
        path("inset", [(0, bottom - 3.2), (x0 + 348, bottom - 3.2)], closed=False,
             stroke=stroke(solid(rgb(ORANGE)), 1.2)),
        # Focus light: a soft additive marine.glow beside the rail (section 13.8).
        path("light", [(x0 + 20, top - 2), (x0 + 70, top - 2), (x0 + 70, bottom + 2), (x0 + 20, bottom + 2)],
             fill=linear((x0 + 20, 0), (x0 + 70, 0), [(0, rgb(GLOW, 0)), (0.5, rgb(GLOW, 0.5)), (1, rgb(GLOW, 0))]), blend="additive"),
    ])
    # The corner marker's solid triangle: 35-41 u across, 11.8 u into the row.
    marker_rest = vector(f"{ident}-marker", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0.4)}, [
        marker_path("mark", x0 + U * 35, U * 11.8, U * 6, rgb(MARKER))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)}, [
        marker_path("mark", x0 + U * 35, U * 11.8, U * 6, rgb(ORANGE))])
    text_node = label(f"{ident}-label", key, {
        **absolute(left=x0 + U * 44, top=top, width=U * 314, height=bottom - top),
        **typeface("marine", 24, bottom - top, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"),
        "transform": transform()})
    parts = [plate, focus, marker_rest, marker_hot, text_node]
    detail_id = None
    if detail is not None:
        detail_id = detail["id"]
        parts.append(detail)
    lit, rest = number(1), number(0)
    def with_detail(tracks, shown):
        return tracks + ([(detail_id, "opacity", lit if shown else rest)] if detail_id else [])
    ids = doc.states(ident, {
        "default": with_detail([(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                                (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-focus", "opacity", number(0)),
                                (f"{ident}-label", "color", colour([1, 1, 1, 0.8])), (f"{ident}-label", "transform", transform())], False),
        "hover": with_detail([(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)),
                              (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(0)),
                              (f"{ident}-label", "color", colour(rgb(ORANGE))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))], True),
        "focus": with_detail([(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)),
                              (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                              (f"{ident}-label", "color", colour(rgb(ORANGE))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))], True),
        "pressed": with_detail([(f"{ident}-plate", "opacity", number(1)), (f"{ident}-marker", "opacity", number(0)),
                                (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                                (f"{ident}-label", "color", colour(rgb(ORANGE))), (f"{ident}-label", "transform", transform(tx=1, ty=1, sx=1.03, sy=1.03))], True),
        "disabled": with_detail([(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                                 (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-focus", "opacity", number(0)),
                                 (f"{ident}-label", "color", colour([1, 1, 1, 0.4])), (f"{ident}-label", "transform", transform())], False),
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    if subject is not None:
        control["detail"] = subject
    # The label keeps its size in the plate's band on the stock 30 u pitch
    # at any text size, as the title it carries into does.
    fixed_text(doc, text_node)
    return group(ident, {"position": keyword("relative"), "display": keyword("block"),
                         "margin-left": length(-lead), "width": length(width), "height": length(30 * U)},
                 parts, control=control)


def message_line(ident: str, offset_u: float, children: list) -> dict:
    """The stock message-of-the-day slot at 44,364 u (section 13.7), placed
    inside a navigation button so its hover and focus can show it. It never
    takes the pointer: only the plate is the button's target."""
    return group(ident, {**absolute(left=NAV_LEAD + U * 44, top=U * offset_u, width=U * 433, height=U * 33),
                         "opacity": number(0), "pointer-events": keyword("none")}, children)


def link(doc: Document, ident: str, key: str, *, action: str | None = None, event: str | None = None, size: float = 16) -> dict:
    """A secondary link on the plinth (section 9): Marine 16 dp (or `size`) at
    0.50 behind a 4.5 u marker at 0.40; hover and focus turn both orange."""
    marker = vector(f"{ident}-marker", {**absolute(left=0, top=0, width=U * 7, height=U * 17), "opacity": number(0.4)}, [
        marker_path("mark", 0, U * 7.25, U * 4.5, rgb(MARKER))])
    hot = vector(f"{ident}-marker-hot", {**absolute(left=0, top=0, width=U * 7, height=U * 17), "opacity": number(0)}, [
        marker_path("mark", 0, U * 7.25, U * 4.5, rgb(ORANGE))])
    text_node = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
        "margin-left": length(U * 7), **typeface("marine", size, U * 17, [1, 1, 1, 0.5]), "white-space": keyword("nowrap")})
    ids = doc.states(ident, {
        "default": [(f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-label", "color", colour([1, 1, 1, 0.5]))],
        "hover": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "focus": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-label", "color", colour([1, 1, 1, 1]))],
        "disabled": [(f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-label", "color", colour([1, 1, 1, 0.25]))],
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    fixed_text(doc, text_node)   # the link keeps its size on the plinth
    return group(ident, {"position": keyword("relative"), "display": keyword("block"),
                         "height": length(U * 17), "margin-right": length(U * 14)},
                 [marker, hot, text_node], control=control)


def merged_text(key: str) -> dict:
    """`key` in every shipped language, as the engine resolves it: a
    language's openQ4 table, else the stock GUI table openQ4 ships for it."""
    found = localized(key)
    for language in LANGUAGES:
        if language not in found:
            for line in (STRINGS / f"{language}_guis.lang").read_text(encoding="utf-8").splitlines():
                match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
                if match and match.group(1) == key:
                    found[language] = match.group(2)
    return found


# The plinth's links: Marine 16 dp after a 7 u marker, 14 u apart (link()).
LINK_MARKER_U, LINK_GAP_U = 7, 14


def plinth_widths(keys: tuple, width: float, lead: float, trail_cut: float) -> dict:
    """The plinth's width for each shipped language whose links outrun the
    stock width: the last link's text ends where the trailing cut crosses
    the text's middle."""
    texts = {key: merged_text(key) for key in keys}
    widths = {}
    for language in LANGUAGES:
        end = lead + sum(U * LINK_MARKER_U + text_width("marine", texts[key].get(language) or texts[key]["english"], 16) + U * LINK_GAP_U
                         for key in keys) - U * LINK_GAP_U
        needed = round(end + trail_cut / 2, 1)
        if needed > width + 1:
            widths[language] = needed
    return widths


def fit_narrow(result: dict, pages: bool = False, stretch: tuple = ()) -> dict:
    """Section 9's 4:3 composition in a view narrower than 4:3 (5:4,
    portrait), as the campaign pages fit it (retained_menu_levels.fit_page):
    the canvas shrinks to the view's width (canvas.minWidth 960), each canvas
    group (centred on the view at 960x720 dp) but the title slot's centres
    vertically, and the bottom band and a prompt bar outside the canvas groups
    keep the view's bottom edge. In a
    view of 4:3 or wider every one of these is unchanged.

    With `pages` (a page whose prompt bar and bottom-band controls sit in its
    canvas group) the canvas groups instead run the view's full height: each
    control or part they place directly, or through full-size groups, centres
    by its own offset from the canvas's middle; one that lies in the top band
    (above 80 dp, as Demos' title) keeps the view's top edge and one that
    starts in the bottom band (from 640 dp) keeps its distance from the
    view's bottom, so the bar stays on the bottom band. A detail card keeps
    its place: the runtime sets it beside its subject.

    With `stretch` (a page's scrolling list, by its viewport's id) a view
    taller than the canvas (5:4, portrait) gives its extra height to the
    list rather than to empty space around a centred page: the list and its
    scrollbar keep their top and run to their distance from the view's
    bottom, the parts that start above the list's foot keep the view's top,
    and the parts below it keep its bottom. At 720 dp tall nothing moves."""
    def value(props: dict, key: str):
        found = props.get(key)
        return (found.get("value"), found.get("unit")) if isinstance(found, dict) else None

    detail = (result.get("detail") or {}).get("card")

    def find(node: dict, wanted: str):
        if node.get("id") == wanted:
            return node
        return next((found for child in node.get("children", []) if (found := find(child, wanted)) is not None), None)

    def scrollbars(node: dict) -> list:
        control = node.get("control") or {}
        own = [node["id"]] if control.get("role") == "scrollbar" and control.get("viewport") in stretch else []
        return own + [ident for child in node.get("children", []) for ident in scrollbars(child)]

    stretchy, foot = set(), None
    if stretch:
        stretchy = set(stretch) | set(scrollbars(result["root"]))
        for ident in stretch:
            props = find(result["root"], ident)["properties"]
            bottom = value(props, "top")[0] + value(props, "height")[0]
            foot = bottom if foot is None else max(foot, bottom)

    def place(node: dict) -> None:
        props = node.get("properties", {})
        if detail and node.get("id") == detail:
            return
        if (value(props, "left") == (0, "dp") and value(props, "top") == (0, "dp") and value(props, "width") == (100, "%") and
                value(props, "height") == (100, "%")):
            for child in node.get("children", []):
                place(child)
            return
        top = value(props, "top")
        if top is None or top[1] != "dp" or "bottom" in props:
            return
        height = value(props, "height")
        if node.get("id") in stretchy:
            props["bottom"] = length(round(720 - top[0] - height[0], 4))
            props.pop("height")
            return
        if height is not None and height[1] == "dp" and top[0] + height[0] <= 80:
            return
        if foot is not None:
            if top[0] >= foot - 0.5 and height is not None:
                props.pop("top")
                props["bottom"] = length(round(720 - top[0] - height[0], 4))
            return
        if top[0] >= 640 and value(props, "height") is not None:
            props.pop("top")
            props["bottom"] = length(round(720 - top[0] - props["height"]["value"], 4))
        else:
            props["top"] = length(50, "%")
            props["margin-top"] = length(round(top[0] - 360, 4))
            # A length that follows the text size (a confirmation growing with
            # its body) moves the offset from the middle instead.
            for binding in result.get("bindings", []):
                if binding["node"] == node.get("id") and binding["property"] == "top":
                    binding["property"] = "margin-top"
                    binding["value"] = text_sum(-360, binding["value"])

    def walk(node: dict, in_canvas: bool = False) -> None:
        props = node.get("properties", {})
        canvas = (value(props, "left") == (50, "%") and value(props, "margin-left") == (-CANVAS_W / 2, "dp") and
                  value(props, "top") == (0, "dp") and value(props, "height") == (720, "dp"))
        # The page titles and the label carried into the title slot belong to
        # the top band, which keeps the view's top edge: they stay with it.
        if canvas and pages and node.get("id") not in ("titles", "carry"):
            props["height"] = length(100, "%")
            for child in node.get("children", []):
                place(child)
            return
        if canvas and node.get("id") not in ("titles", "carry"):
            props["top"] = length(50, "%")
            props["margin-top"] = length(-360)
        if node.get("id") == "prompts" and not in_canvas and "top" in props and "bottom" not in props:
            top = props.pop("top")["value"]
            props["bottom"] = length(round(720 - top - props["height"]["value"], 4))
        if node.get("id", "").endswith("band-bottom"):
            for drawn in node.get("paths", []):
                for command in drawn["commands"]:
                    for point in command.get("points", []):
                        if isinstance(point[1], (int, float)):
                            point[1] = {"fraction": 1, "dp": round(point[1] - 720, 4)}
        for child in node.get("children", []):
            walk(child, in_canvas or canvas)
    walk(result["root"])
    result["canvas"]["minWidth"] = 960
    return result


# The window's safe area (section 13.3): the runtime's safe view sources give
# its insets from the view's edges in dp, and fits the canvas to it.
SAFE_EDGES = (("left", "view.safeLeft", "safeLeft"), ("top", "view.safeTop", "safeTop"),
              ("right", "view.safeRight", "safeRight"), ("bottom", "view.safeBottom", "safeBottom"))
PAINT = ("background-color", "backdrop-blur", "backdrop-saturate", "backdrop-streak")


def safe_frame(result: dict) -> dict:
    """Section 13.3: essential content, text and targets stay inside the
    window's safe area, while bands, backdrop and imagery run full-bleed under
    cutouts, rounded corners and system bars. The root becomes the safe area,
    which the runtime's canvas already fits, so every part placed in it keeps
    clear of the insets: titles and crumbs, notices, columns, lists, prompt
    bars and dialogs. A backdrop that fills the root (a vector, picture,
    painted group or soft focus filling it through groups that fill it)
    reaches back out to the view's edges, and a band, drawn past its box,
    already covers them. The root's own backing moves to a node that reaches
    out. With no inset (every desktop window) every box is where it was."""
    def value(props: dict, key: str):
        found = props.get(key)
        return (found.get("value"), found.get("unit")) if isinstance(found, dict) else None

    def fills(node: dict) -> bool:
        props = node.get("properties", {})
        return (value(props, "left") == (0, "dp") and value(props, "top") == (0, "dp") and value(props, "width") == (100, "%") and
                value(props, "height") == (100, "%") and not any(key in props for key in ("right", "bottom", "margin-left", "margin-top")))

    def drawn_past(node: dict) -> bool:
        """A vector drawn above its box's top or below its foot: a band."""
        for drawn in node.get("paths", []):
            for command in drawn["commands"]:
                for at in command.get("points", []):
                    y = at[1]
                    if (isinstance(y, (int, float)) and y < 0) or (isinstance(y, dict) and y.get("fraction", 0) >= 1 and y.get("dp", 0) > 0):
                        return True
        return False

    owned = {(binding["node"], binding["property"]) for binding in result.get("bindings", [])}
    owned |= {(entry["node"], entry["property"]) for timeline in result.get("timelines", []) for entry in timeline["tracks"]}
    bindings = result.setdefault("bindings", [])
    state = result.setdefault("state", {})
    for _, ident, view in SAFE_EDGES:
        state[ident] = {"type": "number", "initial": 0, "view": view}

    def anchor(node: dict, sign: int) -> None:
        props = node["properties"]
        for key in ("left", "top", "width", "height"):
            if (node["id"], key) in owned:
                raise SystemExit(f"the safe area cannot place {node['id']}.{key}: something else owns it")
        props.pop("width")
        props.pop("height")
        for key, ident, _ in SAFE_EDGES:
            props[key] = length(0)
            inset = {"state": ident} if sign > 0 else {"op": "-", "args": [0, {"state": ident}]}
            bindings.append({"id": f"safe.{node['id']}.{key}", "node": node["id"], "property": key, "value": inset})

    def reach(node: dict) -> None:
        for child in node.get("children", []):
            if not fills(child):
                continue
            props = child.get("properties", {})
            if child["type"] == "image" or (child["type"] == "vector" and not drawn_past(child)) or any(key in props for key in PAINT):
                anchor(child, -1)
            elif child["type"] == "group":
                reach(child)

    root = result["root"]
    if not fills(root):
        raise SystemExit(f"{result['id']}'s root does not fill the view")
    backing = root["properties"].pop("background-color", None)
    reach(root)
    anchor(root, 1)
    if backing is not None:
        node = group(f"{root['id']}-backing", {**FULL, "background-color": backing, "pointer-events": keyword("none")})
        root.setdefault("children", []).insert(0, node)
        anchor(node, -1)
    return result


def wrap_menu(column: list, row: list = ()) -> None:
    """Section 13.4: a menu of up to seven items wraps. Up and Down step
    through `column` (its controls top to bottom) and on to the plinth's
    links in `row`, and from there back to the top, so the column and the
    plinth form one loop; Left and Right wrap along the row. Each move is
    authored, and a control that cannot take focus (CONTINUE without a save,
    a disabled entry) passes it on along its own (Interaction::Authored)."""
    if not 1 <= len(column) <= 7 or len(row) > 7:
        raise SystemExit(f"a wrapping menu holds up to seven items and links: {len(column)} and {len(row)}")
    ids = [node["id"] for node in column]
    links = [node["id"] for node in row]
    for index, node in enumerate(column):
        up = ids[index - 1] if index > 0 else (links[0] if links else ids[-1])
        down = ids[index + 1] if index + 1 < len(ids) else (links[0] if links else ids[0])
        node["control"].setdefault("navigation", {}).update({"up": up, "down": down})
    for index, node in enumerate(row):
        node["control"].setdefault("navigation", {}).update({
            "up": ids[-1], "down": ids[0], "left": links[index - 1], "right": links[(index + 1) % len(links)]})


def plinth(doc: Document, links: list, keys: tuple = ()) -> dict:
    """The link plinth (bg2, Appendix B.1): 24-361 u by 403-422 u, a 45-degree
    lower-leading cut and a parallel trailing cut, marine.plinth at 0.30.
    Where a language's link labels (`keys`) outrun it, the plinth runs on to
    hold them, its trailing cut moving with its end."""
    left, right, top, bottom = 24.0, 361.2, 403.3, 422.1
    width, height = U * (right - left), U * (bottom - top)
    lead_cut, trail_cut = U * 11.2, U * 19.2
    shape = path("plinth", [(0, 0), ({"fraction": 1, "dp": round(-trail_cut, 3)}, 0), ({"fraction": 1}, height), (lead_cut, height),
                            (0, height - lead_cut)], fill=solid(rgb(PLINTH, 0.3)))
    row = group("plinth-links", {**absolute(left=U * (32 - left), top=U * -0.3, height=U * 17),
                                  "display": keyword("flex"), "flex-direction": keyword("row"),
                                  "align-items": keyword("flex-start")}, links)
    widths = plinth_widths(keys, width, U * (32 - left), trail_cut) if keys else {}
    if widths:
        if "language" not in doc.state:
            doc.state["language"] = {"type": "string", "initial": "", "cvar": "sys_lang"}
        value = round(width, 3)
        for language in sorted(widths, reverse=True):
            value = {"op": "select", "args": [{"op": "==", "args": [{"state": "language"}, language]}, widths[language], value]}
        doc.bind("plinth.width", "plinth", "width", value)
    return vector("plinth", {**absolute(left=U * left, top=U * top, width=width, height=height), "opacity": number(1)}, [shape], [row])


def action_plate(doc: Document, ident: str, key: str, left: float, top: float, *, action: str | None = None,
                 event: str | None = None, width: float = 180, tint: str = OLIVE, hot: str = ORANGE,
                 face: str = "marine") -> dict:
    """An action button (section 6): a light plate, 35 dp visible in a 45 dp
    target, a 20 dp lower-leading cut, rails on the leading edge, cut and
    bottom, fill 0.49 fading from 33 % to nothing; 8 dp marker, Marine 20 dp."""
    band_top, band_bottom = 5.0, 40.0
    cut = 20.0
    fill = [(0, rgb(tint, 0.49)), (0.33, rgb(tint, 0.49)), (1, rgb(tint, 0))]
    rail = [(0, rgb(tint)), (0.33, rgb(tint)), (1, rgb(tint, 0))]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0.4)}, [
        path("fill", [(0, band_top), (width, band_top), (width, band_bottom), (cut, band_bottom), (0, band_bottom - cut)],
             fill=linear((0, 0), (width, 0), fill)),
        path("rail", [(0.75, band_top), (0.75, band_bottom - cut), (cut, band_bottom - 0.75), (width, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (width, 0), rail), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)}, [
        path("inset", [(3.5, band_top + 3), (3.5, band_bottom - cut + 1.5), (cut + 1.5, band_bottom - 3.5), (width - 20, band_bottom - 3.5)],
             closed=False, stroke=stroke(solid(rgb(hot)), 1.2))])
    marker_rest = vector(f"{ident}-marker", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(0.4)}, [
        marker_path("mark", 0, 0, 8, rgb(MARKER if tint == OLIVE else tint))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(0)}, [
        marker_path("mark", 0, 0, 8, rgb(hot))])
    text_node = label(f"{ident}-label", key, {**absolute(left=26, top=0, width=width - 30, height=45),
        **typeface(face, 20, 45, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "transform": transform()})
    ids = doc.states(ident, {
        "default": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)),
                    (f"{ident}-focus", "opacity", number(0)), (f"{ident}-label", "color", colour([1, 1, 1, 0.8])), (f"{ident}-label", "transform", transform())],
        "hover": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)),
                  (f"{ident}-focus", "opacity", number(0)), (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "focus": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)),
                  (f"{ident}-focus", "opacity", number(1)), (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "pressed": [(f"{ident}-plate", "opacity", number(1)), (f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)),
                    (f"{ident}-focus", "opacity", number(1)), (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(tx=1, ty=1, sx=1.03, sy=1.03))],
        "disabled": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)),
                     (f"{ident}-focus", "opacity", number(0)), (f"{ident}-label", "color", colour([1, 1, 1, 0.4])), (f"{ident}-label", "transform", transform())],
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, absolute(left=left, top=top, width=width, height=45), [plate, focus, marker_rest, marker_hot, text_node], control=control)


def outlined_label(ident: str, key: str, box: dict, style: dict, fill: list[float],
                   outline: list[float], extrude: float = 1.0) -> list:
    """Text with a thin outline drawn behind its fill (section 6, the modal
    title): eight opaque copies extruded `extrude` dp around the strokes,
    composited as one group at the outline's alpha, so the copies' overlaps
    never darken it past that. The group's own box encloses every copy,
    because its composite is clipped to that box. Returns the outline group,
    then the fill."""
    diagonal = round(extrude * math.sqrt(0.5), 4)
    offsets = [(-extrude, 0), (extrude, 0), (0, -extrude), (0, extrude),
               (-diagonal, -diagonal), (diagonal, -diagonal), (-diagonal, diagonal), (diagonal, diagonal)]
    pad = extrude + 2
    left, top = box["left"]["value"], box["top"]["value"]
    enclosing = absolute(left=left - pad, top=top - pad, width=box["width"]["value"] + 2 * pad, height=box["height"]["value"] + 2 * pad)
    copies = [label(f"{ident}-outline-{index}", key, {**box, "left": length(pad + dx), "top": length(pad + dy),
                                                      **style, "color": colour([*outline[:3], 1])})
              for index, (dx, dy) in enumerate(offsets, start=1)]
    return [group(f"{ident}-outline", {**enclosing, "opacity": number(outline[3]), "pointer-events": keyword("none")}, copies),
            label(ident, key, {**box, **style, "color": colour(fill)})]


SOFT_FOCUS_BLUR = 5 * U   # modal.softfocus: a 5 u Gaussian (section 4)
SOFT_FOCUS_SATURATION = 0.8


CLOSE_W = 210.0  # a notice's lone action is wider than YES and NO, so every language's CLOSE fits


def modal_silhouette(width: float, height: float):
    """The stock popup art's silhouette in a `width` x `height` dp dialog:
    popup_top, popup_mid and popup_btm stretch 512 texels across the rect, the
    art starting at texel 7 and ending after 504 (6.5625 dp in from each side
    at 480 dp); the raised sections start 4 of popup_top's 32 rows down
    (3.5625 dp) and the bottom edge leaves 6 of popup_btm's 64 rows (5.34 dp).
    The tooth's top ends at texel 14 and the slot opens to texel 388, about 73%
    of the width; black at 0.70 with a 17 dp slot and a 38 dp chamfer."""
    inset, raised, bottom = width * 7 / 512, 3.5625, height - 5.34375
    slot_lead, slot_trail = width * 14 / 512, width * 388 / 512
    slot_depth, chamfer = 17.0, 38.0
    right = width - inset
    return path("frame", [(inset, raised), (slot_lead, raised), (slot_lead + slot_depth, raised + slot_depth),
                          (slot_trail - slot_depth, raised + slot_depth), (slot_trail, raised), (right, raised),
                          (right, bottom), (inset + chamfer, bottom), (inset, bottom - chamfer)],
                fill=solid([0, 0, 0, 0.7]))


def from_foot(paths: list, height: float, foot: float, axis: int = 1) -> list:
    """`paths` with each point at or below `foot` measured from their box's
    foot (with `axis` 0, at or past `foot` from its trailing edge), so the
    shapes follow a box that grows; in a box `height` tall (or wide) every
    point stands where it did."""
    for drawn in paths:
        for command in drawn["commands"]:
            for at in command.get("points", []):
                if isinstance(at[axis], (int, float)) and at[axis] >= foot:
                    at[axis] = {"fraction": 1, "dp": round(at[axis] - height, 4)}
    return paths


def confirmation(doc: Document, ident: str, title_key: str, body_key: str, yes_action: str | None,
                 frame_leave_ms: float = 250, dim: tuple = (), *, yes_event: str | None = None,
                 no_event: str | None = None, close_key: str | None = None, wide_title: bool = False,
                 height_u: float = 136.0, top_u: float = 155.0, actions_u: float = 96.0, contents: list | None = None,
                 yes_key: str = "#str_200157", no_key: str = "#str_200158", initial: str | None = None,
                 width_dp: float = 480.0, action_w: float = 180, action_text: float = 20.0, close_w: float = CLOSE_W,
                 strogg: bool = False, contents_grow=None) -> dict:
    """A stock confirmation modal (section 6) over the soft-focused screen: an
    additive glow behind the 480 dp dialog, and inside it the stock popup
    art's black 0.70 silhouette, inset so a 6 dp lit margin shows beside it,
    with a leading tooth, a recessed title slot, a raised trailing section and
    a 38 dp lower-leading chamfer. The title hangs from the raised top line
    into the lit slot with a 1 dp black outline at 0.85; YES leads, NO trails.

    The screen beneath takes modal.softfocus, a 5 u blur at 0.80 saturation
    that never dims it, and the glow is cut to the dialog: graded only
    vertically, half strength at the rectangle's top and bottom edges and gone
    40 dp beyond them, fading out over a further 16 dp past each side. With
    the opaque-backing option, or where the renderer cannot soften the screen
    (ui_retainedSoftFocus is 0), the stock 0.94 marine.scrim covers it instead
    and the glow keeps the stock column, exactly the rectangle's width.

    Motion (section 8): modal.enter brings the soft focus (or the scrim), the
    glow and the frame in over 200 ms and shows the title, body and actions
    together at its end; modal.leave hides them at once and, after 50 ms,
    releases the soft focus or scrim and the glow over 250 ms and the frame
    over `frame_leave_ms` (200 ms for Exit). Completion programs show the
    contents and close the modal, so a Show during leave or a Hide during
    enter takes over the other run, which then never completes. Static bases
    are the hidden values, so the first key's retarget never flashes the
    scrim. `dim` lists (node, property, lit, dimmed) that dim over 200 ms on
    enter and return from 50 ms over 150 ms on leave, as the Exit and Mods
    modals dim the wordmark to 40% gray. With `close_key` the dialog is a
    notice: one action with that label stands in YES's place and closes it,
    as the stock overwrite popup's CLOSE does. With `wide_title` a title too
    long for the lit slot in some language runs on over the raised trailing
    section to the frame's edge, as the stock title ran past its rectangle.
    A list or entry dialog grows to `height_u` (146-400 u) at `top_u`, with
    its actions at `actions_u`, its own `contents` in place of the body and
    its own action labels and first focus; a ledger grows to `width_dp`, and
    actions whose labels run long widen to `action_w` with `action_text` dp
    labels, or a notice's lone action to `close_w`.

    With `strogg` the dialog takes the Strogg pause's family (section 13.7):
    the silhouette's slot flanks and lower-leading chamfer become 30-degree
    shoulders in the Strogg card fill with its #F59512 rail, the glow takes the
    Strogg backdrop's light, the title and body are R_Strogg in #FCFFC8 and
    the actions are Strogg plates (strogg_action_plate). Every label arrives
    in runes as the contents show and plays the short credits translation:
    the title over 300 ms, the actions 45 ms apart after it; the wrapped body
    fades in. Ids, events, focus and timing are the stock dialog's."""
    body_nodes = contents
    visible, contents = f"{ident}.visible", f"{ident}.contents"
    doc.state[visible] = {"type": "boolean", "initial": False}
    doc.state[contents] = {"type": "boolean", "initial": False}
    doc.state["soft_focus"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedSoftFocus"}
    show, shown, hide, hidden = f"{ident}Show", f"{ident}Shown", f"{ident}Hide", f"{ident}Hidden"
    enter, leave = f"{ident}Enter", f"{ident}Leave"
    doc.events[show] = [{"op": "setState", "values": {visible: True, contents: False}}, {"op": "playTimeline", "timeline": enter}]
    doc.events[shown] = [{"op": "setState", "values": {contents: True}}]
    doc.events[hide] = [{"op": "setState", "values": {contents: False}}, {"op": "playTimeline", "timeline": leave}]
    doc.events[hidden] = [{"op": "setState", "values": {visible: False}}]
    width, height = width_dp, U * height_u  # 320x136 u, or grown
    left = (CANVAS_W - width) / 2
    top = U * top_u                         # 155 u: 17 u above centre
    # The silhouette (modal_silhouette) and the title's place in its slot.
    inset = width * 7 / 512
    slot_lead, slot_trail = width * 14 / 512, width * 388 / 512
    slot_depth = 17.0
    # A Strogg shoulder keeps the slot's depth and runs 30 degrees.
    flank = round(slot_depth * STROGG_SHOULDER, 4) if strogg else slot_depth
    right = width - inset
    silhouette = strogg_modal_silhouette(width, height) if strogg else [modal_silhouette(width, height)]
    # Pop-ups carry a neat, thin, opaque black frame (user rule, 5 November
    # 2026): the silhouette's outline drawn in black on top of the glow, which
    # stays as it is around it, 3 dp so it reads at a glance. The Strogg frame
    # lies under its rail, so the rail keeps its color with a black edge
    # either side. The title's tab (below) joins it.
    rim_w = 4.5 if strogg else 3.0
    rim = {"id": "rim", "commands": json.loads(json.dumps(silhouette[0]["commands"])),
           "stroke": stroke(solid([0, 0, 0, 1]), rim_w)}
    silhouette = [silhouette[0], rim, *silhouette[1:]]
    glow_tint = ST_LIGHT if strogg else GLOW_MODAL
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    glow_top, glow_bottom = -U * 87, height + U * 108
    glow = vector(f"{ident}-glow", {**absolute(left=0, top=glow_top, width=width, height=glow_bottom - glow_top), "opacity": number(0),
                                    "display": keyword("block")}, [
        path("column", whole,
             fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(glow_tint, 0)), (0.28, rgb(glow_tint, 0.5)), (0.41, rgb(glow_tint, 0.9)),
                                                        (0.58, rgb(glow_tint, 0.9)), (0.72, rgb(glow_tint, 0.5)), (1, rgb(glow_tint, 0))]),
             blend="additive")])
    # The soft-focus glow keeps the stock column's plateau inside the rectangle
    # and grades from half strength at its edges to nothing 40 dp beyond them.
    # Its side fade is an alpha mask, so the two gradients multiply.
    reach, side = 40.0, 16.0
    plateau = [glow_top + stop * (glow_bottom - glow_top) for stop in (0.41, 0.58)]
    soft_width, soft_height = width + 2 * side, height + 2 * reach
    def soft_stop(y: float) -> float:
        return round((y + reach) / soft_height, 4)
    soft_glow = vector(f"{ident}-glow-soft", {**absolute(left=-side, top=-reach, width=soft_width, height=soft_height),
                                              "opacity": number(0), "display": keyword("none")}, [
        path("column", whole,
             fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(glow_tint, 0)), (soft_stop(0), rgb(glow_tint, 0.5)),
                                                        (soft_stop(plateau[0]), rgb(glow_tint, 0.9)), (soft_stop(plateau[1]), rgb(glow_tint, 0.9)),
                                                        (soft_stop(height), rgb(glow_tint, 0.5)), (1, rgb(glow_tint, 0))]),
             blend="additive")],
        mask={"paths": [path("sides", whole, fill=linear((0, 0), ({"fraction": 1}, 0), [
            (0, [1, 1, 1, 0]), (round(side / soft_width, 4), [1, 1, 1, 1]), (round(1 - side / soft_width, 4), [1, 1, 1, 1]), (1, [1, 1, 1, 0])]))]})
    frame = vector(f"{ident}-frame", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(0)}, silhouette)
    # The title starts at the foot of the slot's leading flank; its capitals
    # hang from the raised top line into the lit slot, as the stock title's
    # do, its baseline 13.5 dp down where the stock one sits.
    foot = slot_lead + flank
    title_end = right - 6 if wide_title else slot_trail - flank
    face, title_tint = ("r_strogg", rgb(ST_READ, 0.9)) if strogg else ("marine", [1, 1, 1, 0.9])
    if title_key.startswith("#str_"):
        # 20 dp at -0.075 em: every shipped language's title must fit its
        # box, which clips the outline while the fill runs on.
        needed, language = max((text_width(face, text, 20) - 1.5 * (len(text) - 1), language)
                               for language, text in merged_text(title_key).items())
        if needed > title_end - foot:
            raise ValueError(f"{ident}: the {language} title {title_key} needs {needed:.1f} dp of {title_end - foot:.1f}"
                             + ("" if wide_title else "; pass wide_title=True"))
    # The title's tab (user rule, 5 November 2026): the frame fills the slot
    # in opaque black from the raised top line, behind the title, and steps
    # down to the slot's floor just past the title's end, at 45 degrees (the
    # Strogg family's 30), so the title stands on its own background. Its run
    # follows the running language's title, capped where the slot's trailing
    # flank begins; a title that is not a localization key takes the whole
    # slot.
    raised, tab_pad = 3.5625, 10.0
    tab_left = slot_lead - rim_w / 2
    tab_cap = slot_trail - flank
    def tab_width(run: float) -> float:
        return round(min(foot + run + tab_pad, tab_cap) + flank - tab_left, 3)
    if title_key.startswith("#str_"):
        widths = {language: tab_width(text_width(face, text, 20) - 1.5 * (len(text) - 1))
                  for language, text in merged_text(title_key).items()}
    else:
        widths = {"english": tab_width(tab_cap)}
    tab_edge = [(0, 0), ({"fraction": 1, "dp": -flank}, 0), ({"fraction": 1}, slot_depth)]
    tab_paths = [path("tab", [*tab_edge, (0, slot_depth)], fill=solid([0, 0, 0, 1])),
                 path("tab-rim", tab_edge, closed=False, stroke=stroke(solid([0, 0, 0, 1]), rim_w))]
    if strogg:
        tab_paths.append(path("tab-rail", tab_edge, closed=False, stroke=stroke(solid(rgb(ST_LINE, 0.75)), 1.5)))
    tab = vector(f"{ident}-tab", {**absolute(left=round(tab_left, 4), top=raised, width=max(widths.values()), height=slot_depth),
                                  "pointer-events": keyword("none")}, tab_paths)
    if len(set(widths.values())) > 1:
        doc.bind(f"{ident}-tab.width", f"{ident}-tab", "width", per_language(doc, widths))
    frame["children"] = [*frame.get("children", []), tab]
    title_style = {**typeface(face, 20, 24, title_tint), "letter-spacing": length(-0.075, "em"), "white-space": keyword("nowrap")}
    title_box = absolute(left=round(foot, 4), top=-4, width=round(title_end - foot, 4), height=24)
    if strogg:
        # The outline and its fill translate as one, so the pop keeps them
        # registered; the rune copy shares their baseline.
        pad = 3.0
        title = [label(f"{ident}-title-rune", title_key, {**absolute(left=round(foot, 4), top=round(-4 - rune_rise(20), 3),
                                                                     width=round(title_end - foot, 4), height=24),
                       **typeface("strogg", round(20 * RUNE_SCALE, 3), 24, [*rgb(ST_SELECT)[:3], 0]), "white-space": keyword("nowrap"),
                       "transform": transform(), "pointer-events": keyword("none")}),
                 group(f"{ident}-title-latin", {**absolute(left=round(foot - pad, 4), top=-4 - pad, width=round(title_end - foot + 2 * pad, 4),
                                                           height=24 + 2 * pad), "opacity": number(1), "transform": transform(),
                                                "pointer-events": keyword("none")},
                       outlined_label(f"{ident}-title", title_key, absolute(left=pad, top=pad, width=round(title_end - foot, 4), height=24),
                                      title_style, title_tint, [0, 0, 0, 0.85]))]
    else:
        title = outlined_label(f"{ident}-title", title_key, title_box, title_style, title_tint, [0, 0, 0, 0.85])
    # The stock body box starts 45 u down (first baseline 87.5 dp) and runs
    # to the actions at 96 u. R_Strogg runs wider than Lowpixel, so the
    # Strogg body sets a size smaller on the same lines.
    body_tint = rgb(ST_READ, 0.85) if strogg else [1, 1, 1, 0.8]
    body = [label(f"{ident}-body", body_key, {**absolute(left=33, top=71, width=width - 66, height=73),
        **(typeface("r_strogg", 15, 22, body_tint) if strogg else typeface("lowpixel", 17, 22, body_tint))})] if body_nodes is None else body_nodes
    # The modal's own programs may answer for it: `yes_event` in place of the
    # session action, `no_event` for No and back; each must hide the modal.
    plate_kind = strogg_action_plate if strogg else action_plate
    if close_key:
        actions = [plate_kind(doc, f"{ident}_close", close_key, 30, U * actions_u, event=no_event or hide, width=close_w)]
        initial = initial or f"{ident}_close"
    else:
        yes = plate_kind(doc, f"{ident}_yes", yes_key, 30, U * actions_u, action=None if yes_event else yes_action, event=yes_event,
                         width=action_w)
        no = plate_kind(doc, f"{ident}_no", no_key, width - 33 - action_w, U * actions_u, event=no_event or hide, width=action_w)
        actions, initial = [yes, no], initial or f"{ident}_no"
        # Section 13.4: the two actions wrap, Left from YES to NO and Right
        # from NO to YES.
        yes["control"].setdefault("navigation", {})["left"] = f"{ident}_no"
        no["control"].setdefault("navigation", {})["right"] = f"{ident}_yes"
        if action_text != 20:
            for plate in actions:
                plate_label = next(node for node in walk_nodes(plate) if node.get("id", "").endswith("-label"))
                plate_label["properties"]["font-size"] = length(action_text)
    if strogg:
        # The labels arrive in runes as the contents show, and translate:
        # the title over 300 ms, the actions 45 ms apart after it; the wrapped
        # body fades in. While the contents are hidden, the reset puts every
        # line back in runes, so the first shown frame never flashes the
        # translated text.
        translation = Translation()
        translation.add(f"{ident}-title", 0, 300, width=round(title_end - foot + 2 * pad, 4))
        if body_nodes is None:
            translation.add(f"{ident}-body", 60, 200, rest=body_tint, runes=False)
        for index, plate in enumerate(actions):
            translation.add(plate["id"], 100 + 45 * index, 200)
        tracks, end = translation.tracks(0)
        doc.timelines.add(f"{ident}Translate", end, tracks)
        doc.timelines.add(f"{ident}Runes", 1, translation.reset_tracks())
        doc.events[show].append({"op": "playTimeline", "timeline": f"{ident}Runes"})
        doc.events[shown].append({"op": "playTimeline", "timeline": f"{ident}Translate"})
    # Section 2's text scale: the title and the actions' labels keep their
    # size in their slot and plates. A body of known text grows with the
    # text, and the dialog with it: the frame, glows and actions move down by
    # what its lines need past the stock box, the dialog staying centred.
    for part in title:
        fixed_text(doc, part)
    for plate in actions:
        fixed_text(doc, plate)
    shown_contents = group(f"{ident}-contents", {**FULL, "display": keyword("none")}, [*title, *body, *actions])
    dialog = group(f"{ident}-dialog", absolute(left=left, top=top, width=width, height=height),
                   [glow, soft_glow, frame, shown_contents])
    known_body = body_nodes is None and body_key.startswith("#str_") and body_key != "#str_230030"
    if known_body or contents_grow is not None:
        text_source(doc)
        if known_body:
            box = body[0]["properties"]
            body_face, body_size = box["font-family"]["value"], box["font-size"]["value"]
            body_line, body_h = box["line-height"]["value"], box["height"]["value"]
            counts = wrapped_lines((body_key,), body_face, body_size, box["width"]["value"])
            lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()})
                                  for scale in (1.5, 2)})
            extra = {"op": "select", "args": [LARGER, {"op": "max", "args": [0, text_sum(text_times({"op": "*", "args": [lines, body_line]}),
                                                                                            -body_h)]}, 0]}
            if 2 * body_line * max(found[2] for found in counts.values()) - body_h > 720 - height - 2 * U * 12:
                raise SystemExit(f"{ident}: the body {body_key} would outgrow the view at 200 % text")
        else:
            # Contents that grow with the text (an entry dialog's field) say by
            # how much; the dialog grows with them as with a body.
            extra = {"op": "select", "args": [LARGER, contents_grow, 0]}
        from_foot(frame["paths"], height, height / 2)
        grown = lambda node, key: doc.bind(f"text.{node['id']}.{key}", node["id"], key, text_sum(node["properties"][key]["value"], extra))
        for node in (dialog, frame, glow, soft_glow, *(body[:1] if known_body else ())):
            grown(node, "height")
        for plate in actions:
            grown(plate, "top")
        doc.bind(f"text.{dialog['id']}.top", dialog["id"], "top",
                 text_sum(dialog["properties"]["top"]["value"], {"op": "*", "args": [extra, -0.5]}))
    stage = group(f"{ident}-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"),
                                     "margin-left": length(-CANVAS_W / 2)}, [dialog])
    # The soft focus is the first layer's backdrop filter, so it softens
    # everything beneath the modal; hidden, it costs no pass at all.
    softened = group(f"{ident}-softfocus", {**FULL, "display": keyword("none"), "backdrop-blur": length(0),
                                            "backdrop-saturate": number(1)})
    scrim = group(f"{ident}-scrim", {**FULL, "display": keyword("block"), "background-color": colour([0, 0, 0, 0])})
    node = group(ident, {**FULL, "display": keyword("none")}, [softened, scrim, stage],
                 modal={"initialFocus": initial, "back": no_event or hide})
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"state": visible}, "block", "none"]})
    doc.bind(f"{ident}.contents", f"{ident}-contents", "display", {"op": "select", "args": [{"state": contents}, "block", "none"]})
    for part, soft in ((f"{ident}-softfocus", True), (f"{ident}-scrim", False), (f"{ident}-glow", False), (f"{ident}-glow-soft", True)):
        doc.bind(f"{part}.display", part, "display", {"op": "select", "args": [{"state": "soft_focus"}, *(("block", "none") if soft else ("none", "block"))]})
    scrim_on, scrim_off = colour([0, 0, 0, 0.94]), colour([0, 0, 0, 0])
    blur_on, blur_off = length(SOFT_FOCUS_BLUR), length(0)
    tint_on, tint_off = number(SOFT_FOCUS_SATURATION), number(1)
    glows = (f"{ident}-glow", f"{ident}-glow-soft")
    doc.timelines.add(enter, 200, [
        track(f"{ident}-scrim", "background-color", [(0, scrim_off), (200, scrim_on)]),
        track(f"{ident}-softfocus", "backdrop-blur", [(0, blur_off), (200, blur_on)]),
        track(f"{ident}-softfocus", "backdrop-saturate", [(0, tint_off), (200, tint_on)]),
        *[track(part, "opacity", [(0, number(0)), (200, number(1))]) for part in glows],
        track(f"{ident}-frame", "opacity", [(0, number(0)), (200, number(1))]),
    ] + [track(node, prop, [(0, lit), (200, dimmed)]) for node, prop, lit, dimmed in dim], complete=shown)
    frame_end = 50 + frame_leave_ms
    doc.timelines.add(leave, 300, [
        track(f"{ident}-scrim", "background-color", [(0, scrim_on), (50, scrim_on), (300, scrim_off)]),
        track(f"{ident}-softfocus", "backdrop-blur", [(0, blur_on), (50, blur_on), (300, blur_off)]),
        track(f"{ident}-softfocus", "backdrop-saturate", [(0, tint_on), (50, tint_on), (300, tint_off)]),
        *[track(part, "opacity", [(0, number(1)), (50, number(1)), (300, number(0))]) for part in glows],
        track(f"{ident}-frame", "opacity", [(0, number(1)), (50, number(1)), (frame_end, number(0))] +
              ([(300, number(0))] if frame_end < 300 else [])),
    ] + [track(node, prop, [(0, dimmed), (50, dimmed), (200, lit), (300, lit)]) for node, prop, lit, dimmed in dim],
        complete=hidden)
    return node


# MODS (section 6, the list dialog): the modal grown from 136 u to 280 u, its
# centre 17 u above the view's as the modal's is.
MOD_ROWS = 32                             # RETAINED_MOD_ROWS in the session
MODS_TOP, MODS_HEIGHT = 83.0, 280.0       # u
MODS_LIST = (22.0, 25.0, 274.0, 147.0)    # u, in the dialog: the stock pop_mods_list
MODS_HEAD_TOP, MODS_ROW_H, MODS_VERSION_X = 12.0, 20.0, 200.0
MODS_DETAIL_TOP, MODS_DETAIL_PITCH, MODS_VALUE_X = 178.0, 15.0, 98.0
MODS_DETAILS = (("author", "#str_41112"), ("date", "#str_41113"), ("engine", "#str_41114"), ("directory", "#str_41115"))


def grow_mods(doc: Document, parts: list, column: dict) -> None:
    """Section 2's reflow in the MODS dialog: above 100 % text the mods' rows
    grow with it (retained_saves.grow_rows), and the selected mod's detail
    lines stand on a pitch that grows with it from the same foot over the
    actions, each value ending in an ellipsis past the dialog; the list gives
    them the room, scrolling through fewer rows. NAME, VERSION and the
    details' labels are furniture and keep their size, each label in its
    grown line. Every binding gives the authored value at 100 %
    (check_text_bindings)."""
    from retained_saves import grow_rows
    grow_rows(doc, column)
    text_source(doc)
    nodes = {node["id"]: node for part in parts for node in walk_nodes(part)}
    growth = {"op": "*", "args": [U * MODS_DETAIL_PITCH * len(MODS_DETAILS), {"op": "-", "args": [TEXT, 1]}]}
    for index, (detail, _) in enumerate(MODS_DETAILS):
        top = U * (MODS_DETAIL_TOP + MODS_DETAIL_PITCH * index)
        moved = text_sum(top - U * MODS_DETAIL_PITCH * index, {"op": "*", "args": [U * MODS_DETAIL_PITCH * index, TEXT]},
                         {"op": "*", "args": [-1, growth]})
        for ident in (f"mods-{detail}-label", f"mods-{detail}"):
            props = nodes[ident]["properties"]
            doc.bind(f"text.{ident}.top", ident, "top", {"op": "select", "args": [LARGER, moved, top]})
            doc.bind(f"text.{ident}.height", ident, "height", text_times(props["height"]["value"]))
        # The label keeps its size, centred in its line as the value's line grows.
        label_props = nodes[f"mods-{detail}-label"]["properties"]
        doc.bind(f"text.mods-{detail}-label.font-size", f"mods-{detail}-label", "font-size",
                 {"op": "/", "args": [label_props["font-size"]["value"], TEXT]})
        value = nodes[f"mods-{detail}"]["properties"]
        value["text-overflow"] = keyword("clip")
        doc.bind(f"text.mods-{detail}.text-overflow", f"mods-{detail}", "text-overflow", {"op": "select", "args": [LARGER, "ellipsis", "clip"]})
    for ident in ("mods-list", "mods_scroll"):
        height = nodes[ident]["properties"]["height"]["value"]
        doc.bind(f"text.{ident}.height", ident, "height", {"op": "select", "args": [LARGER, text_sum(height, {"op": "*", "args": [-1, growth]}),
                                                                                    height]})
    for ident in ("mods-head-name", "mods-head-version"):
        fixed_text(doc, nodes[ident])


def mods_modal(doc: Document) -> dict:
    """MODS (section 6): the stock modal construction grown into a list dialog,
    its title MODIFICATIONS. Its body holds the installed mods under NAME and
    VERSION headings and an olive rule, on 20 u rows with the list's light and
    a scrollbar, then the selected mod's author, release date, required engine
    and directory, and LOAD MOD and CANCEL stand where YES and NO do. The
    session publishes the mods before the dialog shows (mods.*), the current
    game's selected. A row's first Accept selects its mod; Accept on the
    selected mod, or LOAD MOD, loads it. The wordmark dims to 40% gray, as for
    EXIT."""
    from retained_saves import row_plates, row_states
    doc.state.update({"mods.count": {"type": "number", "initial": 0}, "mods.selected": {"type": "number", "initial": -1},
                      "mods.pick": {"type": "number", "initial": -1}})
    for detail, _ in MODS_DETAILS:
        doc.state[f"mods.{detail}"] = {"type": "string", "initial": ""}
    doc.session("modsSelect", "modsSelect")
    doc.session("modsLoad", "modsLoad")
    # LOAD MOD and CANCEL plates take every language's longest label and
    # 50 dp, at least the stock 180 dp, and the dialog both plates beside
    # each other with its 30 and 33 dp margins and a 12 dp gap; the list grows
    # with the dialog.
    longest = max(text_width("marine", text, 20) for key in ("#str_200167", "#str_200168") for text in merged_text(key).values())
    action_w = round(max(180.0, longest + 50), 1)
    dialog_w = round(max(480.0, 2 * action_w + 30 + 33 + 12), 1)
    left, top, list_w, list_h = MODS_LIST
    list_w = round(list_w + (dialog_w - 480.0) / U, 1)
    heading = {**typeface("lowpixel", 13, U * 12, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    parts = [
        label("mods-head-name", "#str_41110", {**absolute(left=U * left, top=U * MODS_HEAD_TOP, width=U * (MODS_VERSION_X - 4), height=U * 12), **heading}),
        label("mods-head-version", "#str_41111", {**absolute(left=U * (left + MODS_VERSION_X), top=U * MODS_HEAD_TOP,
              width=U * (list_w - MODS_VERSION_X), height=U * 12), **heading}),
        vector("mods-rule", absolute(left=U * left, top=U * top - 0.6, width=U * list_w, height=1.2), [
            path("rule", [(0, 0.6), ({"fraction": 1}, 0.6)], closed=False, stroke=stroke(solid(rgb(OLIVE, 0.26)), 1.2))]),
    ]
    rows = []
    width, height = U * (list_w - 16), U * MODS_ROW_H
    cell = {"white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")}
    tints = ([1, 1, 1, 0.9], [1, 1, 1, 0.65])
    for index in range(MOD_ROWS):
        ident, key = f"mods-row-{index}", f"mods.row{index}"
        cells = []
        for column, left_u, column_w, tint in (("name", 4, MODS_VERSION_X - 8, tints[0]), ("version", MODS_VERSION_X, list_w - 16 - MODS_VERSION_X - 4, tints[1])):
            doc.state[f"{key}.{column}"] = {"type": "string", "initial": ""}
            node = f"{ident}-{column}"
            cells.append(label(node, "#str_230030", {**absolute(left=U * left_u, top=0, width=U * column_w, height=height),
                                                     **typeface("lowpixel", 15.8, height, tint), **cell}))
            doc.bind(f"{node}.text", node, "text", {"state": f"{key}.{column}"})
        doc.bind(f"{ident}.display", ident, "display",
                 {"op": "select", "args": [{"op": "<", "args": [index, {"state": "mods.count"}]}, "block", "none"]})
        doc.bind(f"{ident}-selected.display", f"{ident}-selected", "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "mods.selected"}, index]}, "block", "none"]})
        event = f"modsRow{index}"
        doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": "mods.selected"}, index]},
                              "then": [{"op": "call", "event": "modsLoadConfirmed"}],
                              "else": [{"op": "setState", "values": {"mods.pick": index}}, {"op": "action", "action": "modsSelect"}]}]
        ids = row_states(doc, ident, (f"{ident}-name", f"{ident}-version"), tints)
        rows.append(group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                                  "flex-shrink": number(0)}, [*row_plates(ident, width, height), *cells],
                          control={"role": "button", "label": "#str_230030", "event": event, "states": ids}))
    column_node = group("mods-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")}, rows)
    parts.append(group("mods-list", {**absolute(left=U * left, top=U * top, width=width, height=U * list_h), "overflow": keyword("auto")},
                       [column_node]))
    parts.append(objectives_scrollbar(doc, "mods_scroll", "mods-list", OLIVE, ORANGE, U * (left + list_w) - 24, U * top, U * list_h,
                                      line_step=U * MODS_ROW_H, key="#str_200166"))
    # The values start where every language's longest detail label ends.
    value_x = max(MODS_VALUE_X, math.ceil(max(text_width("lowpixel", text, 13) for _, key in MODS_DETAILS
                                              for text in merged_text(key).values()) / U) + 4)
    for index, (detail, key) in enumerate(MODS_DETAILS):
        y = U * (MODS_DETAIL_TOP + MODS_DETAIL_PITCH * index)
        parts.append(label(f"mods-{detail}-label", key, {**absolute(left=U * left, top=y, width=U * (value_x - 4), height=U * 14),
                           **typeface("lowpixel", 13, U * 14, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
        value = f"mods-{detail}"
        parts.append(label(value, "#str_230030", {**absolute(left=U * (left + value_x), top=y, width=U * (list_w - value_x), height=U * 14),
                           **typeface("lowpixel", 15, U * 14, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
        doc.bind(f"{value}.text", value, "text", {"state": f"mods.{detail}"})
    grow_mods(doc, parts, column_node)
    modal = confirmation(doc, "modsModal", "#str_200166", "#str_230030", None, yes_event="modsLoadConfirmed",
                         dim=(("wordmark", "image-color", colour([1, 1, 1, 1]), colour([0.4, 0.4, 0.4, 1])),), height_u=MODS_HEIGHT, top_u=MODS_TOP,
                         actions_u=MODS_HEIGHT - 40, contents=parts, yes_key="#str_200167", no_key="#str_200168",
                         width_dp=dialog_w, action_w=action_w)
    # The dialog opens on CANCEL, as the stock modals open on NO, and moves to
    # the selected mod once its contents show.
    doc.events["modsModalShown"] = doc.events["modsModalShown"] + [
        {"op": "if", "condition": {"op": "==", "args": [{"state": "mods.selected"}, index]},
         "then": [{"op": "focus", "control": f"mods-row-{index}"}]} for index in range(MOD_ROWS)]
    doc.events["modsLoadConfirmed"] = [{"op": "if", "condition": {"op": ">=", "args": [{"state": "mods.selected"}, 0]},
                                        "then": [{"op": "call", "event": "modsModalHide"}, {"op": "action", "action": "modsLoad"}]}]
    doc.bind("modsModal_yes.enabled", "modsModal_yes", "enabled", {"op": ">=", "args": [{"state": "mods.selected"}, 0]})
    return modal


# ------------------------------------------------------------- device glyphs

# Prompts follow the active device (section 13.5). The adapter owns the state
# prompt.glyphs: 0 shows the keyboard's keycaps, 1-5 a controller family's
# face buttons in this order (RetainedUI_PromptGlyphs).
PROMPT_GLYPHS = "prompt.glyphs"
# Whether menus confirm on the east face button (Swap Confirm and Back): the
# Accept and Back prompts then show each other's position.
PROMPT_SWAPPED = "prompt.swapped"
SWAPPED_FACE = {"south": "east", "east": "south"}
GLYPH_FAMILIES = ("xbox", "playstation", "nintendo", "steamdeck", "generic")
# The label each family prints at each face position, as SDL reports it.
FACE_LETTERS = {
    "xbox": {"south": "A", "east": "B", "west": "X", "north": "Y"},
    "nintendo": {"south": "B", "east": "A", "west": "Y", "north": "X"},
    "steamdeck": {"south": "A", "east": "B", "west": "X", "north": "Y"},
}
PS_SYMBOLS = {"south": "cross", "east": "circle", "west": "square", "north": "triangle"}
# The label each family prints on its shoulder buttons (the bumpers).
SHOULDER_LABELS = {
    "xbox": {"left": "LB", "right": "RB"},
    "playstation": {"left": "L1", "right": "R1"},
    "nintendo": {"left": "L", "right": "R"},
    "steamdeck": {"left": "L1", "right": "R1"},
}
# The label each family prints on its triggers.
TRIGGER_LABELS = {
    "xbox": {"left": "LT", "right": "RT"},
    "playstation": {"left": "L2", "right": "R2"},
    "nintendo": {"left": "ZL", "right": "ZR"},
    "steamdeck": {"left": "L2", "right": "R2"},
}
# The label each family prints for a press of its sticks, as the HUD's
# prompts name them (UserInterfaceRetained.cpp's glyph table).
STICK_LABELS = {
    "xbox": {"left": "LS", "right": "RS"},
    "playstation": {"left": "L3", "right": "R3"},
    "nintendo": {"left": "LS", "right": "RS"},
    "steamdeck": {"left": "L3", "right": "R3"},
    "generic": {"left": "L3", "right": "R3"},
}
# A prompt's key and the button that does the same in menus: Enter is
# Accept (south), Escape is Back (east), and the cards' Q and E are the
# previous and next tab (the shoulders).
FACE_FOR_KEY = {"#str_107019": "south", "#str_107020": "east", "#str_231009": "left", "#str_231010": "right"}
GLYPH_INK = [0.04, 0.05, 0.035, 1]   # the keycap's key-name color


def circle_path(ident: str, cx: float, cy: float, r: float, *, fill=None, line=None) -> dict:
    """A circle as four cubic quarter arcs, in the node's own dp."""
    k = 0.5523 * r

    def at(dx, dy):
        return point(cx + dx, cy + dy)
    result = {"id": ident, "commands": [
        {"id": "m", "op": "move", "points": [at(r, 0)]},
        {"id": "a", "op": "cubic", "points": [at(r, k), at(k, r), at(0, r)]},
        {"id": "b", "op": "cubic", "points": [at(-k, r), at(-r, k), at(-r, 0)]},
        {"id": "c", "op": "cubic", "points": [at(-r, -k), at(-k, -r), at(0, -r)]},
        {"id": "d", "op": "cubic", "points": [at(k, -r), at(r, -k), at(r, 0)]},
        {"id": "z", "op": "close"}]}
    if fill is not None:
        result["fill"] = fill
    if line is not None:
        result["stroke"] = line
    return result


def face_mark(prefix: str, mark: str, scale: float, ink: list) -> list:
    """A face button's letter or symbol drawn as strokes in a 22 dp box:
    letters 9 dp tall, the PlayStation symbols 7.4-9 dp across."""
    width = stroke(solid(ink), 1.9 * scale, minimum=1)

    def line(name: str, points: list, closed: bool = False) -> dict:
        return path(f"{prefix}-{name}", [(x * scale, y * scale) for x, y in points], closed=closed, stroke=width)

    def curve(name: str, commands: list) -> dict:
        built = []
        for index, (op, points) in enumerate(commands):
            built.append({"id": f"c{index}", "op": op, "points": [point(x * scale, y * scale) for x, y in points]})
        return {"id": f"{prefix}-{name}", "commands": built, "stroke": width}
    if mark == "A":
        return [line("a1", [(7.4, 15.5), (11, 6.5), (14.6, 15.5)]), line("a2", [(8.8, 12.3), (13.2, 12.3)])]
    if mark == "B":
        return [curve("b1", [("move", [(8.2, 15.5)]), ("line", [(8.2, 6.5)]), ("line", [(11.3, 6.5)]),
                             ("cubic", [(13.7, 6.5), (13.7, 10.9), (11.3, 10.9)]), ("line", [(8.2, 10.9)])]),
                curve("b2", [("move", [(8.2, 10.9)]), ("line", [(11.8, 10.9)]),
                             ("cubic", [(14.5, 10.9), (14.5, 15.5), (11.8, 15.5)]), ("line", [(8.2, 15.5)])])]
    if mark == "X":
        return [line("x1", [(7.6, 6.6), (14.4, 15.4)]), line("x2", [(14.4, 6.6), (7.6, 15.4)])]
    if mark == "Y":
        return [line("y1", [(7.6, 6.6), (11, 11.2), (14.4, 6.6)]), line("y2", [(11, 11.2), (11, 15.4)])]
    if mark == "cross":
        return [line("x1", [(7.3, 7.3), (14.7, 14.7)]), line("x2", [(14.7, 7.3), (7.3, 14.7)])]
    if mark == "circle":
        return [circle_path(f"{prefix}-o", 11 * scale, 11 * scale, 4.5 * scale, line=width)]
    if mark == "square":
        return [line("s", [(7.3, 7.3), (14.7, 7.3), (14.7, 14.7), (7.3, 14.7)], closed=True)]
    if mark == "triangle":
        return [line("t", [(11, 6.3), (15.5, 14.5), (6.5, 14.5)], closed=True)]
    raise ValueError(mark)


def face_glyph(ident: str, family: str, position: str, tint: list, size: float = 22) -> dict:
    """One controller family's face button at `position` (section 13.5),
    vector symbols in the family text color whose shape and letter carry the
    identity: Xbox and Nintendo print letters on a disc (their east and south
    letters swapped), PlayStation its four symbols, Steam Deck letters in a
    ring, and a generic controller the four positions as a diamond with this
    one filled."""
    scale = size / 22
    if family == "generic":
        paths = []
        for spot, (cx, cy) in (("north", (11, 4.3)), ("west", (4.3, 11)), ("east", (17.7, 11)), ("south", (11, 17.7))):
            if spot == position:
                paths.append(circle_path(f"dot-{spot}", cx * scale, cy * scale, 3.6 * scale, fill=solid(tint)))
            else:
                paths.append(circle_path(f"dot-{spot}", cx * scale, cy * scale, 2.7 * scale,
                                         line=stroke(solid(tint), 1.3 * scale, minimum=1)))
    elif family == "steamdeck":
        paths = [circle_path("ring", 11 * scale, 11 * scale, 9.9 * scale, line=stroke(solid(tint), 1.5 * scale, minimum=1))]
        paths += face_mark("mark", FACE_LETTERS[family][position], scale, tint)
    else:
        mark = PS_SYMBOLS[position] if family == "playstation" else FACE_LETTERS[family][position]
        paths = [circle_path("disc", 11 * scale, 11 * scale, 10.6 * scale, fill=solid(tint))]
        paths += face_mark("mark", mark, scale, GLYPH_INK)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(size),
                          "height": length(size)}, paths)


def small_letters(prefix: str, text: str, left: float, top: float, height: float, ink: list, width: float) -> list:
    """Shoulder, trigger and stick labels (L, R, B, S, T, Z, 1, 2 and 3) drawn as strokes, `height` dp tall
    from (`left`, `top`), each glyph 0.55 of its height wide and 0.25 apart."""
    line = stroke(solid(ink), width, minimum=1)
    glyph_w, gap = height * 0.55, height * 0.25
    paths = []
    x = left
    for index, char in enumerate(text):
        name = f"{prefix}-{index}"
        bottom, middle, right = top + height, top + height * 0.5, x + glyph_w
        if char == "L":
            paths.append(path(name, [(x, top), (x, bottom), (right, bottom)], closed=False, stroke=line))
        elif char == "1":
            paths.append(path(name, [(x + glyph_w * 0.2, top + height * 0.22), (x + glyph_w * 0.6, top),
                                     (x + glyph_w * 0.6, bottom)], closed=False, stroke=line))
        elif char == "2":
            paths.append({"id": name, "commands": [
                {"id": "m", "op": "move", "points": [point(x, top + height * 0.25)]},
                {"id": "a", "op": "cubic", "points": [point(x, top - height * 0.05), point(right, top - height * 0.05),
                                                       point(right, top + height * 0.3)]},
                {"id": "b", "op": "line", "points": [point(x, bottom)]},
                {"id": "c", "op": "line", "points": [point(right, bottom)]}], "stroke": line})
        elif char == "3":
            # Two bowls opening to the leading side, meeting at the middle.
            paths.append({"id": name, "commands": [
                {"id": "m", "op": "move", "points": [point(x, top + height * 0.1)]},
                {"id": "a", "op": "cubic", "points": [point(x + glyph_w * 0.3, top - height * 0.05), point(right, top),
                                                       point(right, top + height * 0.25)]},
                {"id": "b", "op": "cubic", "points": [point(right, middle - height * 0.05), point(x + glyph_w * 0.6, middle),
                                                       point(x + glyph_w * 0.35, middle)]},
                {"id": "c", "op": "move", "points": [point(x + glyph_w * 0.35, middle)]},
                {"id": "d", "op": "cubic", "points": [point(x + glyph_w * 0.7, middle), point(right, middle + height * 0.05),
                                                       point(right, bottom - height * 0.25)]},
                {"id": "e", "op": "cubic", "points": [point(right, bottom), point(x + glyph_w * 0.3, bottom + height * 0.05),
                                                       point(x, bottom - height * 0.1)]}], "stroke": line})
        elif char == "S":
            paths.append({"id": name, "commands": [
                {"id": "m", "op": "move", "points": [point(right, top + height * 0.15)]},
                {"id": "a", "op": "cubic", "points": [point(right - glyph_w * 0.2, top - height * 0.02), point(x, top),
                                                       point(x, top + height * 0.25)]},
                {"id": "b", "op": "cubic", "points": [point(x, middle - height * 0.05), point(right, middle + height * 0.05),
                                                       point(right, bottom - height * 0.25)]},
                {"id": "c", "op": "cubic", "points": [point(right, bottom), point(x + glyph_w * 0.2, bottom + height * 0.02),
                                                       point(x, bottom - height * 0.15)]}], "stroke": line})
        elif char == "T":
            paths.append(path(f"{name}-bar", [(x, top), (right, top)], closed=False, stroke=line))
            paths.append(path(name, [(x + glyph_w * 0.5, top), (x + glyph_w * 0.5, bottom)], closed=False, stroke=line))
        elif char == "Z":
            paths.append(path(name, [(x, top), (right, top), (x, bottom), (right, bottom)], closed=False, stroke=line))
        elif char in "BR":
            bowl = right - glyph_w * 0.15
            commands = [{"id": "m", "op": "move", "points": [point(x, bottom)]},
                        {"id": "s", "op": "line", "points": [point(x, top)]},
                        {"id": "t", "op": "line", "points": [point(x + glyph_w * 0.5, top)]},
                        {"id": "u", "op": "cubic", "points": [point(bowl, top), point(bowl, middle), point(x + glyph_w * 0.5, middle)]},
                        {"id": "v", "op": "line", "points": [point(x, middle)]}]
            if char == "B":
                commands += [{"id": "w", "op": "move", "points": [point(x + glyph_w * 0.5, middle)]},
                             {"id": "x", "op": "cubic", "points": [point(right, middle), point(right, bottom),
                                                                   point(x + glyph_w * 0.5, bottom)]},
                             {"id": "y", "op": "line", "points": [point(x, bottom)]}]
            else:
                commands += [{"id": "w", "op": "move", "points": [point(x + glyph_w * 0.45, middle)]},
                             {"id": "x", "op": "line", "points": [point(right, bottom)]}]
            paths.append({"id": name, "commands": commands, "stroke": line})
        else:
            raise ValueError(char)
        x = right + gap
    return paths


def shoulder_glyph(ident: str, family: str, side: str, tint: list, width: float = 22, height: float = 22) -> dict:
    """One controller family's shoulder button (`side` "left" or "right"),
    the bumper drawn as a 13 dp plate rounded on its top corners: Xbox,
    PlayStation and Nintendo print their label on the plate, Steam Deck in
    an outlined plate, and a generic controller shows an outlined plate with
    a chevron pointing to its side."""
    top, bottom, inset, radius = (height - 13) / 2, (height + 13) / 2, 1.0, 4.0
    k = 0.5523 * radius
    right = width - inset
    plate = [{"id": "m", "op": "move", "points": [point(inset, bottom)]},
             {"id": "a", "op": "line", "points": [point(inset, top + radius)]},
             {"id": "b", "op": "cubic", "points": [point(inset, top + radius - k), point(inset + radius - k, top), point(inset + radius, top)]},
             {"id": "c", "op": "line", "points": [point(right - radius, top)]},
             {"id": "d", "op": "cubic", "points": [point(right - radius + k, top), point(right, top + radius - k), point(right, top + radius)]},
             {"id": "e", "op": "line", "points": [point(right, bottom)]},
             {"id": "z", "op": "close"}]
    letters_h = 6.6
    if family == "generic":
        paths = [{"id": "plate", "commands": plate, "stroke": stroke(solid(tint), 1.3, minimum=1)}]
        mid, tip, back = (top + bottom) / 2, (width / 2 - 2.6 if side == "left" else width / 2 + 2.6), \
            (width / 2 + 1.4 if side == "left" else width / 2 - 1.4)
        paths.append(path("chevron", [(back, mid - 3.2), (tip, mid), (back, mid + 3.2)], closed=False,
                          stroke=stroke(solid(tint), 1.5, minimum=1)))
    else:
        text = SHOULDER_LABELS[family][side]
        text_w = len(text) * letters_h * 0.55 + (len(text) - 1) * letters_h * 0.25
        left = (width - text_w) / 2
        letters_top = (top + bottom - letters_h) / 2
        if family == "steamdeck":
            paths = [{"id": "plate", "commands": plate, "stroke": stroke(solid(tint), 1.3, minimum=1)}]
            paths += small_letters("label", text, left, letters_top, letters_h, tint, 1.4)
        else:
            paths = [{"id": "plate", "commands": plate, "fill": solid(tint)}]
            paths += small_letters("label", text, left, letters_top, letters_h, GLYPH_INK, 1.4)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                          "height": length(height)}, paths)


def trigger_glyph(ident: str, family: str, side: str, tint: list, width: float = 22, height: float = 22) -> dict:
    """One controller family's trigger (`side` "left" or "right"), taller
    than a bumper: an 18 dp plate whose top corners round by 7 dp, the label
    in its lower half. Xbox, PlayStation and Nintendo print their label on
    the plate, Steam Deck in an outlined plate, and a generic controller
    shows an outlined plate with a chevron pointing to its side."""
    top, bottom, radius = (height - 18) / 2, (height + 18) / 2, 7.0
    inset = max(1.0, (width - 18) / 2)
    k = 0.5523 * radius
    right = width - inset
    plate = [{"id": "m", "op": "move", "points": [point(inset, bottom)]},
             {"id": "a", "op": "line", "points": [point(inset, top + radius)]},
             {"id": "b", "op": "cubic", "points": [point(inset, top + radius - k), point(inset + radius - k, top), point(inset + radius, top)]},
             {"id": "c", "op": "line", "points": [point(right - radius, top)]},
             {"id": "d", "op": "cubic", "points": [point(right - radius + k, top), point(right, top + radius - k), point(right, top + radius)]},
             {"id": "e", "op": "line", "points": [point(right, bottom)]},
             {"id": "z", "op": "close"}]
    letters_h = 6.6
    letters_top = bottom - 3 - letters_h
    if family == "generic":
        paths = [{"id": "plate", "commands": plate, "stroke": stroke(solid(tint), 1.3, minimum=1)}]
        mid, tip, back = letters_top + letters_h / 2, (width / 2 - 2.6 if side == "left" else width / 2 + 2.6), \
            (width / 2 + 1.4 if side == "left" else width / 2 - 1.4)
        paths.append(path("chevron", [(back, mid - 3.2), (tip, mid), (back, mid + 3.2)], closed=False,
                          stroke=stroke(solid(tint), 1.5, minimum=1)))
    else:
        text = TRIGGER_LABELS[family][side]
        text_w = len(text) * letters_h * 0.55 + (len(text) - 1) * letters_h * 0.25
        left = (width - text_w) / 2
        if family == "steamdeck":
            paths = [{"id": "plate", "commands": plate, "stroke": stroke(solid(tint), 1.3, minimum=1)}]
            paths += small_letters("label", text, left, letters_top, letters_h, tint, 1.4)
        else:
            paths = [{"id": "plate", "commands": plate, "fill": solid(tint)}]
            paths += small_letters("label", text, left, letters_top, letters_h, GLYPH_INK, 1.4)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                          "height": length(height)}, paths)


def stick_glyph(ident: str, family: str, side: str, tint: list, size: float = 22) -> dict:
    """A press of one controller family's stick (`side` "left" or "right"):
    the stick's cap seen from above as a circle with its press label (LS and
    RS, or L3 and R3). Xbox, PlayStation and Nintendo print the label on a
    filled cap, Steam Deck and a generic controller in an outlined one, the
    cap's rim doubled so it reads as a stick rather than a face button."""
    scale = size / 22
    letters_h = 6.6 * scale
    text = STICK_LABELS[family][side]
    text_w = len(text) * letters_h * 0.55 + (len(text) - 1) * letters_h * 0.25
    left, top = (size - text_w) / 2, (size - letters_h) / 2
    outlined = family in ("steamdeck", "generic")
    rim = circle_path("rim", 11 * scale, 11 * scale, 10.4 * scale, line=stroke(solid(tint), 1.1 * scale, minimum=1))
    if outlined:
        paths = [rim, circle_path("cap", 11 * scale, 11 * scale, 7.6 * scale, line=stroke(solid(tint), 1.3 * scale, minimum=1))]
        paths += small_letters("label", text, left, top, letters_h, tint, 1.3 * scale)
    else:
        paths = [rim, circle_path("cap", 11 * scale, 11 * scale, 8.2 * scale, fill=solid(tint))]
        paths += small_letters("label", text, left, top, letters_h, GLYPH_INK, 1.3 * scale)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(size),
                          "height": length(size)}, paths)


def glyph_pill(scale: float, tint: list, fill: bool) -> dict:
    """The upright pill of a glyph's 22 dp box at `scale`: PlayStation's
    Options and Create buttons filled, a generic controller's outlined."""
    left, right, top, bottom, radius = 5.5 * scale, 16.5 * scale, 1.5 * scale, 20.5 * scale, 5.5 * scale
    k = 0.5523 * radius
    commands = [{"id": "m", "op": "move", "points": [point(left, top + radius)]},
                {"id": "a", "op": "cubic", "points": [point(left, top + radius - k), point(left + radius - k, top), point(left + radius, top)]},
                {"id": "b", "op": "cubic", "points": [point(right - radius + k, top), point(right, top + radius - k), point(right, top + radius)]},
                {"id": "c", "op": "line", "points": [point(right, bottom - radius)]},
                {"id": "d", "op": "cubic", "points": [point(right, bottom - radius + k), point(right - radius + k, bottom), point(right - radius, bottom)]},
                {"id": "e", "op": "cubic", "points": [point(left + radius - k, bottom), point(left, bottom - radius + k), point(left, bottom - radius)]},
                {"id": "z", "op": "close"}]
    return ({"id": "pill", "commands": commands, "fill": solid(tint)} if fill else
            {"id": "pill", "commands": commands, "stroke": stroke(solid(tint), 1.3 * scale, minimum=1)})


def menu_glyph(ident: str, family: str, tint: list, size: float = 22) -> dict:
    """One controller family's Menu button (section 13.5), drawn in the
    family text color: Xbox's three lines on a disc, PlayStation's Options
    button as three lines on an upright pill, Nintendo's plus on a disc,
    Steam Deck's three lines in a ring, and a generic controller's three
    lines in an outlined pill."""
    scale = size / 22
    lines_ink = GLYPH_INK if family in ("xbox", "playstation", "nintendo") else tint

    def bars(width: float) -> list:
        line = stroke(solid(lines_ink), 1.6 * scale, minimum=1)
        return [path(f"bar-{index}", [((11 - width / 2) * scale, (11 + dy) * scale), ((11 + width / 2) * scale, (11 + dy) * scale)],
                     closed=False, stroke=line) for index, dy in enumerate((-3.2, 0.0, 3.2))]

    if family == "xbox":
        paths = [circle_path("disc", 11 * scale, 11 * scale, 10.6 * scale, fill=solid(tint))] + bars(9.0)
    elif family == "playstation":
        paths = [glyph_pill(scale, tint, True)] + bars(6.0)
    elif family == "nintendo":
        line = stroke(solid(GLYPH_INK), 1.8 * scale, minimum=1)
        paths = [circle_path("disc", 11 * scale, 11 * scale, 10.6 * scale, fill=solid(tint)),
                 path("plus-h", [(6 * scale, 11 * scale), (16 * scale, 11 * scale)], closed=False, stroke=line),
                 path("plus-v", [(11 * scale, 6 * scale), (11 * scale, 16 * scale)], closed=False, stroke=line)]
    elif family == "steamdeck":
        paths = [circle_path("ring", 11 * scale, 11 * scale, 9.9 * scale, line=stroke(solid(tint), 1.5 * scale, minimum=1))] + bars(8.0)
    else:
        paths = [glyph_pill(scale, tint, False)] + bars(6.0)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(size),
                          "height": length(size)}, paths)


def view_glyph(ident: str, family: str, tint: list, size: float = 22) -> dict:
    """One controller family's View button (section 13.5), drawn in the
    family text color: Xbox's two overlapping windows on a disc,
    PlayStation's Create button as three rays on an upright pill, Nintendo's
    minus on a disc, Steam Deck's two windows in a ring, and a generic
    controller's two windows in an outlined pill."""
    scale = size / 22
    ink = GLYPH_INK if family in ("xbox", "playstation", "nintendo") else tint

    def windows(span: float) -> list:
        line = stroke(solid(ink), 1.4 * scale, minimum=1)
        offset = span * 0.32
        back = (11 - span / 2, 11 - span / 2, 11 + span / 2 - offset, 11 + span / 2 - offset)
        front = (11 - span / 2 + offset, 11 - span / 2 + offset, 11 + span / 2, 11 + span / 2)
        # The front window hides the back one's lower-right corner.
        return [path("back", [(back[0] * scale, back[3] * scale), (back[0] * scale, back[1] * scale), (back[2] * scale, back[1] * scale),
                              (back[2] * scale, front[1] * scale)], closed=False, stroke=line),
                path("back-foot", [(back[0] * scale, back[3] * scale), (front[0] * scale, back[3] * scale)], closed=False, stroke=line),
                path("front", [(front[0] * scale, front[1] * scale), (front[2] * scale, front[1] * scale), (front[2] * scale, front[3] * scale),
                               (front[0] * scale, front[3] * scale)], stroke=line)]

    if family == "xbox":
        paths = [circle_path("disc", 11 * scale, 11 * scale, 10.6 * scale, fill=solid(tint))] + windows(9.0)
    elif family == "playstation":
        line = stroke(solid(ink), 1.4 * scale, minimum=1)
        paths = [glyph_pill(scale, tint, True)] + [path(f"ray-{index}", [(x0 * scale, y0 * scale), (x1 * scale, y1 * scale)], closed=False, stroke=line)
                                for index, (x0, y0, x1, y1) in enumerate(((11, 14.5, 11, 7.5), (9.3, 14.8, 7.9, 9.2),
                                                                          (12.7, 14.8, 14.1, 9.2)))]
    elif family == "nintendo":
        line = stroke(solid(GLYPH_INK), 1.8 * scale, minimum=1)
        paths = [circle_path("disc", 11 * scale, 11 * scale, 10.6 * scale, fill=solid(tint)),
                 path("minus", [(6 * scale, 11 * scale), (16 * scale, 11 * scale)], closed=False, stroke=line)]
    elif family == "steamdeck":
        paths = [circle_path("ring", 11 * scale, 11 * scale, 9.9 * scale, line=stroke(solid(tint), 1.5 * scale, minimum=1))] + windows(8.0)
    else:
        paths = [glyph_pill(scale, tint, False)] + windows(7.0)
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(size),
                          "height": length(size)}, paths)


def device_glyphs(doc: Document, ident: str, position: str, tint: list, size: float = 22,
                  width: float | None = None) -> list:
    """Each controller family's button at `position` (a face position, the
    "left" or "right" shoulder, the "left-trigger" or "right-trigger", the
    "left-stick" or "right-stick" press, "menu" or "view"),
    each shown only while prompt.glyphs names its family. A shoulder or a
    trigger is `width` dp wide (default `size`). An
    Accept or Back prompt (south or east) also carries each family's button
    at the other of the two, shown while prompt.swapped says confirm has
    moved east (`<ident>-<family>-swapped`)."""
    doc.state.setdefault(PROMPT_GLYPHS, {"type": "number", "initial": 0})
    swaps = position in SWAPPED_FACE
    if swaps:
        doc.state.setdefault(PROMPT_SWAPPED, {"type": "boolean", "initial": False})
    nodes = []
    for index, family in enumerate(GLYPH_FAMILIES, 1):
        same = {"op": "==", "args": [{"state": PROMPT_GLYPHS}, index]}
        variants = [(f"{ident}-{family}", position, same)]
        if swaps:
            variants = [(f"{ident}-{family}", position, {"op": "&&", "args": [same, {"op": "!", "args": [{"state": PROMPT_SWAPPED}]}]}),
                        (f"{ident}-{family}-swapped", SWAPPED_FACE[position], {"op": "&&", "args": [same, {"state": PROMPT_SWAPPED}]})]
        for node_id, spot, condition in variants:
            if spot in ("left", "right"):
                node = shoulder_glyph(node_id, family, spot, tint, width or size, size)
            elif spot in ("left-trigger", "right-trigger"):
                node = trigger_glyph(node_id, family, spot.split("-")[0], tint, width or size, size)
            elif spot in ("left-stick", "right-stick"):
                node = stick_glyph(node_id, family, spot.split("-")[0], tint, size)
            elif spot == "menu":
                node = menu_glyph(node_id, family, tint, size)
            elif spot == "view":
                node = view_glyph(node_id, family, tint, size)
            else:
                node = face_glyph(node_id, family, spot, tint, size)
            doc.bind(f"{node_id}.display", node_id, "display", {"op": "select", "args": [condition, "block", "none"]})
            nodes.append(node)
    return nodes


def prompt_glyph(doc: Document, ident: str, key_name: str, tint: list[float], extra: dict | None = None) -> dict:
    """A prompt's glyph for the active device (section 13.5), 6 dp before its
    verb: the keyboard's keycap, `key_name` in Lowpixel on a 22 dp plate with
    a 45-degree cut, or the controller family's button that does the same.
    `extra` adds properties to the glyph's group."""
    cap = vector(f"{ident}-cap", {"position": keyword("relative"), "display": keyword("block"),
                                  "height": length(22), "padding-left": length(8), "padding-right": length(8)}, [
        path("cap", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (6, {"fraction": 1}), (0, {"fraction": 1, "dp": -6})],
             fill=solid(tint))],
        [label(f"{ident}-key", key_name, {"position": keyword("relative"), "display": keyword("block"),
                **typeface("lowpixel", 13, 22, [0.04, 0.05, 0.035, 1]), "white-space": keyword("nowrap")})])
    doc.bind(f"{ident}-cap.display", f"{ident}-cap", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": PROMPT_GLYPHS}, 0]}, "block", "none"]})
    return group(f"{ident}-glyph", {"position": keyword("relative"), "display": keyword("flex"),
                                    "flex-direction": keyword("row"), "align-items": keyword("center"),
                                    "height": length(22), "margin-right": length(6), **(extra or {})},
                 [cap] + device_glyphs(doc, ident, FACE_FOR_KEY[key_name], tint))


def prompt_bar(doc: Document, prompts: list, cap_tint: list[float] | None = None,
               verb_face: tuple = ("marine", 14, [1, 1, 1, 0.8])) -> dict:
    """The prompt bar in the bottom band's raised trailing section (section
    13.4): per prompt the active device's glyph, the keyboard's keycap plate
    with a 45-degree cut or the controller family's face button that does
    the same (section 13.5), then its verb. The prompts are legends, which
    never take the pointer from what lies under them."""
    items = []
    tint = cap_tint or [1, 1, 1, 0.85]
    for key_name, verb in prompts:
        name = verb[5:]
        glyph = prompt_glyph(doc, f"prompt-{name}", key_name, tint)
        face, size, verb_tint = verb_face
        verb_node = label(f"prompt-{name}-verb", verb, {"position": keyword("relative"), "display": keyword("block"),
            "margin-right": length(22), **typeface(face, size, 22, verb_tint), "white-space": keyword("nowrap")})
        items += [glyph, verb_node]
    bar = group("prompts", {**absolute(top=U * 447, height=22, right=U * 12), "opacity": number(1), "pointer-events": keyword("none"),
                            "display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("center")}, items)
    # As on a page's bar, the key caps and verbs keep their size.
    fixed_text(doc, bar)
    return bar


# A page's prompt bar (section 13.4) in the docked bottom band's raised
# trailing section, which spans 375.1-717.9 u of the canvas at every aspect:
# Back's 45 dp target where the stock BACK plate's stood, from 532,441 u, its
# glyph 8 dp in and its verb after it, and SELECT leading it 22 dp before.
# The bar's box runs from past the section's riser to the 4:3 view's
# trailing edge, under no control of the page's own.
PROMPT_SELECT, PROMPT_BACK = "#str_231042", "#str_230045"
KEY_ENTER, KEY_ESCAPE = "#str_107019", "#str_107020"
# Accept's verb follows focus (section 13.4): the adapter publishes the
# focused button's prompt to PROMPT_ACCEPT, SELECT for a control that names
# none, or nothing while no focused control can act. The pages' short verbs:
PROMPT_ACCEPT = "prompt.accept"
VERB_LOAD, VERB_DELETE, VERB_SAVE = "#str_231520", "#str_231521", "#str_231522"
VERB_PLAY, VERB_JOIN = "#str_231523", "#str_231033"


def row_prompt(verb: str, chosen: dict) -> dict:
    """A list row's Accept verb: `verb` while the row holds the choice
    (`chosen`), when Accept acts on it, otherwise SELECT, which chooses it."""
    return {"op": "select", "args": [chosen, verb, PROMPT_SELECT]}
PROMPT_TARGET = 45.0             # the stock plate's target height
PROMPT_PAD = 8.0                 # Back's target either side of its glyph and verb
PAGE_BACK_X = U * 532 + PROMPT_PAD   # canvas dp: Back's glyph
PAGE_BAR_TOP = U * 441           # the stock BACK plate's top
PAGE_BAR_LEFT = U * 380          # 5 u past the riser
PAGE_BAR_W = CANVAS_W - PAGE_BAR_LEFT


def engine_text(key: str) -> dict:
    """`key` in every shipped language as the engine resolves it: the last of
    the language's tables to define it (openQ4's own sort last), else
    English."""
    texts = {}
    for language in ("english",) + LANGUAGES:
        for table in sorted(STRINGS.glob(f"{language}_*.lang")):
            for line in table.read_text(encoding="utf-8").splitlines():
                match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
                if match and match.group(1) == key:
                    texts[language] = match.group(2)
    return {language: texts.get(language, texts["english"]) for language in LANGUAGES}


def check_page_prompts(select: bool, face: str, size: float, verbs: tuple = (), lead_limit: float = PAGE_BAR_LEFT) -> None:
    """Every shipped language's prompts fit a page's bar with the wider of
    each keycap and a controller button: Back's verb ends 8 dp inside the 4:3
    view's trailing edge, and Accept, with SELECT or any of the page's
    `verbs`, starts inside the bar and after `lead_limit` (canvas dp), past
    whatever the page sets beside the bar. Generation fails otherwise."""
    texts = {key: engine_text(key) for key in (KEY_ENTER, KEY_ESCAPE, PROMPT_SELECT, PROMPT_BACK, *verbs)}

    def glyph(key: str, language: str) -> float:
        return max(22.0, 16 + text_width("lowpixel", texts[key][language], 13))
    for language in LANGUAGES:
        end = PAGE_BACK_X + glyph(KEY_ESCAPE, language) + 6 + text_width(face, texts[PROMPT_BACK][language], size)
        if end > CANVAS_W - 8:
            raise SystemExit(f"the prompt bar's Back in {language} ends at {end:.0f} dp, past {CANVAS_W - 8:g}")
        for verb in (PROMPT_SELECT, *verbs):
            start = PAGE_BACK_X - 22 - glyph(KEY_ENTER, language) - 6 - text_width(face, texts[verb][language], size)
            if select and start < max(PAGE_BAR_LEFT, lead_limit):
                raise SystemExit(f"the prompt bar's {verb} in {language} starts at {start:.0f} dp, "
                                 f"before {max(PAGE_BAR_LEFT, lead_limit):g}")


def page_prompts(doc: Document, back: str | None = None, *, action: str | None = None, event: str | None = None,
                 select: bool = True, in_canvas: bool = True, ident: str = "prompts", cap_tint: list[float] | None = None,
                 verb_face: tuple = ("marine", 14, [1, 1, 1, 0.8]), hot: str = ORANGE, verbs: tuple = (),
                 lead_limit: float = PAGE_BAR_LEFT, accept_shown: dict | None = None) -> dict:
    """A page's prompt bar (section 13.4): Accept, then BACK at the trailing
    end, in the glyphs of the active device. Accept names what the focused
    control does: its own prompt verb (one of the page's `verbs`), SELECT for
    a control without one, and Accept leaves the bar while no focused control
    can act. With `action` or `event` Back is the button `back`, standing in
    for the stock BACK plate in its 45 dp target: the pointer or focus turns
    its verb `hot` and lights its glyph, focus underlines it and a press
    insets it 1 dp. Otherwise both are legends. Legends never take the
    pointer. `select` False leaves Accept out where it has nothing to do.
    The bar sits in a 4:3 canvas group, or with `in_canvas` False in a node
    spanning the view; Accept starts after `lead_limit` (canvas dp).
    `accept_shown` adds a condition to Accept's own: a page whose Accept
    does nothing for a while takes it off the bar."""
    face, size, verb_tint = verb_face
    check_page_prompts(select, face, size, verbs, lead_limit)
    tint = cap_tint or [1, 1, 1, 0.85]
    middle = (PROMPT_TARGET - 22) / 2
    back_x = PAGE_BACK_X - PAGE_BAR_LEFT

    def verb_label(node: str, key: str, more: dict | None = None) -> dict:
        return label(node, key, {"position": keyword("relative"), "display": keyword("block"), **typeface(face, size, 22, verb_tint),
                                 "white-space": keyword("nowrap"), **(more or {})})
    lead = []
    if select:
        name = f"prompt-{PROMPT_SELECT[5:]}"
        lead = [prompt_glyph(doc, name, KEY_ENTER, tint), verb_label(f"{name}-verb", PROMPT_SELECT)]
        doc.state[PROMPT_ACCEPT] = {"type": "string", "initial": PROMPT_SELECT}
        doc.bind(f"{name}-verb.text", f"{name}-verb", "text", {"state": PROMPT_ACCEPT})
        idle = {"op": "==", "args": [{"state": PROMPT_ACCEPT}, ""]}
        if accept_shown is not None:
            idle = {"op": "||", "args": [idle, {"op": "!", "args": [accept_shown]}]}
        doc.bind(f"{ident}-lead.display", f"{ident}-lead", "display", {"op": "select", "args": [idle, "none", "flex"]})
    leading = group(f"{ident}-lead", {**absolute(top=middle, height=22, right=PAGE_BAR_W - back_x + 22), "display": keyword("flex"),
                                      "flex-direction": keyword("row"), "align-items": keyword("center"),
                                      "pointer-events": keyword("none")}, lead)
    legends = action is None and event is None
    if legends:
        name = f"prompt-{PROMPT_BACK[5:]}"
        back_node = group(f"{ident}-back", {**absolute(left=back_x, top=middle, height=22), "display": keyword("flex"),
                                            "flex-direction": keyword("row"), "align-items": keyword("center")},
                          [prompt_glyph(doc, name, KEY_ESCAPE, tint), verb_label(f"{name}-verb", PROMPT_BACK)])
    else:
        # The glyph is painted opaque and rests at the legends' 0.85.
        glyph = prompt_glyph(doc, back, KEY_ESCAPE, tint[:3] + [1], {"opacity": number(0.85), "transform": transform()})
        focus = vector(f"{back}-focus", {**absolute(left=PROMPT_PAD, top=middle + 22 + 2, height=2), "right": length(PROMPT_PAD),
                                         "opacity": number(0), "pointer-events": keyword("none")}, [
            path("inset", [(0, 1), ({"fraction": 1}, 1)], closed=False, stroke=stroke(solid(rgb(hot)), 1.2))])
        lit, pressed, rest = colour(rgb(hot)), transform(tx=1, ty=1), transform()

        def feedback(glyph_alpha: float, tint_value: dict, moved: dict, underline: float) -> list:
            return [(f"{back}-glyph", "opacity", number(glyph_alpha)), (f"{back}-glyph", "transform", moved),
                    (f"{back}-label", "color", tint_value), (f"{back}-label", "transform", moved), (f"{back}-focus", "opacity", number(underline))]
        ids = doc.states(back, {"default": feedback(0.85, colour(verb_tint), rest, 0), "hover": feedback(1, lit, rest, 0),
                                "focus": feedback(1, lit, rest, 1), "pressed": feedback(1, lit, pressed, 1),
                                "disabled": feedback(0.4, colour(verb_tint[:3] + [0.4]), rest, 0)})
        control = {"role": "button", "label": PROMPT_BACK, "states": ids}
        control.update({"action": action} if action else {"event": event})
        back_node = group(back, {**absolute(left=back_x - PROMPT_PAD, top=0, height=PROMPT_TARGET), "display": keyword("flex"),
                                 "flex-direction": keyword("row"), "align-items": keyword("center"), "padding-left": length(PROMPT_PAD),
                                 "padding-right": length(PROMPT_PAD)},
                          [glyph, verb_label(f"{back}-label", PROMPT_BACK, {"transform": transform()}), focus], control=control)
    box = absolute(left=PAGE_BAR_LEFT, top=PAGE_BAR_TOP, width=PAGE_BAR_W, height=PROMPT_TARGET)
    if not in_canvas:
        box.update({"left": length(50, "%"), "margin-left": length(-CANVAS_W / 2 + PAGE_BAR_LEFT)})
    if legends:
        box["pointer-events"] = keyword("none")
    bar = group(ident, {**box, "opacity": number(1)}, [leading, back_node])
    # The key caps and verbs keep their size in the bar's fixed slots, so BACK
    # stays on the screen at any text size.
    fixed_text(doc, bar)
    return bar


# --------------------------------------------------------------- choreography

def band_motion(doc: Document, prefix: str, content: str, extra_home: list, quick: tuple = ()) -> None:
    """Section 8 stock choreography, sampled every presented frame:
    open        menu.fade from black over 250 ms, bands already home, the
                quick nodes shown at once;
    depart      content out over 250 ms, the `quick` nodes (the plinth and
                the prompt bar, which a page's own BACK replaces) over 50 ms;
                from 50 ms frame.dock and
                screen.depart, both accel(250, 250) over 500 ms;
    departPopup content out over 150 ms, bands stay home;
    returnHome  bands from their page dock home over 500 ms accel(250, 250);
                the home content, already in place, fades in at 500 ms (150 ms)."""
    top, bottom = f"{prefix}-top", f"{prefix}-bottom"
    home, page_top, page_bottom = transform(), transform(*TOP_TO_PAGE), transform(*BOTTOM_TO_PAGE)
    lights = [(node, prop, lit, dark) for node, prop, lit, dark in extra_home]
    doc.timelines.add("open", 250, [
        track("fade", "background-color", [(0, colour([0, 0, 0, 1])), (250, colour([0, 0, 0, 0]))]),
        track(content, "opacity", [(0, number(1)), (250, number(1))]),
        track(content, "transform", [(0, transform()), (250, transform())]),
        track(top, "transform", [(0, home), (250, home)]),
        track(bottom, "transform", [(0, home), (250, home)]),
    ] + [track(node, prop, [(0, lit), (250, lit)]) for node, prop, lit, dark in lights]
      + [track(node, "opacity", [(0, number(1)), (1, number(1)), (250, number(1))]) for node in quick])
    doc.timelines.add("depart", 550, [
        track(content, "opacity", [(0, number(1)), (250, number(0)), (550, number(0))]),
        track(content, "transform", [(0, transform()), (50, transform(), ACCEL), (550, transform(640 * U, 0))]),
        track(top, "transform", [(0, home), (50, home, ACCEL), (550, page_top)]),
        track(bottom, "transform", [(0, home), (50, home, ACCEL), (550, page_bottom)]),
        track("fade", "background-color", [(0, colour([0, 0, 0, 0])), (550, colour([0, 0, 0, 0]))]),
    ] + [track(node, prop, [(0, lit), (250, dark), (550, dark)]) for node, prop, lit, dark in lights]
      + [track(node, "opacity", [(0, number(1)), (50, number(0)), (550, number(0))]) for node in quick])
    doc.timelines.add("departPopup", 200, [
        track(content, "opacity", [(0, number(1)), (150, number(0)), (200, number(0))]),
    ])
    doc.timelines.add("returnHome", 650, [
        track(top, "transform", [(0, page_top, ACCEL), (500, home), (650, home)]),
        track(bottom, "transform", [(0, page_bottom, ACCEL), (500, home), (650, home)]),
        track(content, "transform", [(0, transform()), (650, transform())]),
        track(content, "opacity", [(0, number(0)), (500, number(0)), (650, number(1))]),
        track("fade", "background-color", [(0, colour([0, 0, 0, 0])), (650, colour([0, 0, 0, 0]))]),
    ] + [track(node, prop, [(0, dark), (500, dark), (650, lit)]) for node, prop, lit, dark in lights]
      + [track(node, "opacity", [(0, number(0)), (500, number(0)), (650, number(1))]) for node in quick])


# ------------------------------------------------------------------- depth

def depth_layer(doc: Document, ident: str, depth: float, children: list) -> dict:
    """Section 13.8 depth: the backdrop, light, grid and frame layers lean up
    to 6 dp away from the pointer, deeper layers further; content never
    moves. The session publishes pointer_x and pointer_y in -1..1. Reduced
    motion holds every layer still. A wrapper group carries the lean, so a
    band's own dock timelines keep their transform."""
    if "pointer_x" not in doc.state:
        doc.state["pointer_x"] = {"type": "number", "initial": 0}
        doc.state["pointer_y"] = {"type": "number", "initial": 0}
    if "motion_reduced" not in doc.state:
        doc.state["motion_reduced"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"}

    def axis(state: str) -> dict:
        return {"op": "select", "args": [{"state": "motion_reduced"}, 0, {"op": "*", "args": [{"state": state}, round(-6 * depth, 3)]}]}
    doc.bind(f"{ident}.transform", ident, "transform", [axis("pointer_x"), axis("pointer_y"), 1, 1, 0])
    return group(ident, {**FULL, "transform": transform(), "pointer-events": keyword("none")}, children)


# ------------------------------------------------------------- title carry

TITLE_SLOT = (39.0, 19.0)   # the page state's screen title (section 9)


def title_carry(doc: Document, items: list, continue_rows: bool = False, face: str = "marine",
                tint: str = ORANGE) -> dict:
    """Section 13.8 title continuity (title.carry, section 8): during
    frame.dock the activated navigation label travels from its row into the
    page's title slot at 39,19 u and scales from the 24 dp label to the 18 dp
    screen title in text.title, where the stock page then shows its title.
    `items` lists (session request, label key, row top in u); with
    `continue_rows` the rows sit one pitch lower while CONTINUE is shown. The
    session plays carry_<request> only for a real page hand-off."""
    width, height, scale = U * 314, U * 24.4, 0.75
    # Scaling acts about the box center: keep the left and top edges in place.
    end = {"type": "transform", "unit": "dp", "value": [round(-(1 - scale) * width / 2, 3), round(-(1 - scale) * height / 2, 3),
                                                         scale, scale, 0]}
    text_value = items[-1][1]
    for index, (_, key, _) in reversed(list(enumerate(items[:-1], start=1))):
        text_value = {"op": "select", "args": [{"op": "==", "args": [{"state": "carry_item"}, index]}, key, text_value]}
    doc.state["carry_item"] = {"type": "number", "initial": 0}

    def carry_timeline(ident: str, top_u: float) -> str:
        start = {"type": "transform", "unit": "dp", "value": [round(U * (44 - TITLE_SLOT[0]), 3),
                                                               round(U * (top_u + 2.8 - TITLE_SLOT[1]), 3), 1, 1, 0]}
        # A play retargets its first key to the current value, so the label
        # reaches its row by 1 ms, then travels with frame.dock from 50 ms.
        return doc.timelines.add(ident, 550, [
            track("title-carry", "opacity", [(0, number(1)), (1, number(1)), (550, number(1))]),
            track("title-carry", "transform", [(0, start), (1, start), (50, start, ACCEL), (550, end)]),
            track("title-carry", "color", [(0, colour(rgb(tint))), (1, colour(rgb(tint))), (50, colour(rgb(tint)), ACCEL),
                                           (550, colour([1, 1, 1, 0.5]))]),
        ])

    for index, (request, _, top_u) in enumerate(items, start=1):
        steps = [{"op": "setState", "values": {"carry_item": index}}]
        plain = carry_timeline(f"carry-{request}", top_u)
        if continue_rows:
            lowered = carry_timeline(f"carry-{request}-continue", top_u + 30)
            steps.append({"op": "if", "condition": {"state": "menu_continue"},
                          "then": [{"op": "playTimeline", "timeline": lowered}],
                          "else": [{"op": "playTimeline", "timeline": plain}]})
        else:
            steps.append({"op": "playTimeline", "timeline": plain})
        doc.events[f"carry_{request}"] = steps
    doc.bind("title-carry.text", "title-carry", "text", text_value)
    carry = group("carry", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"),
                            "margin-left": length(-CANVAS_W / 2), "pointer-events": keyword("none")}, [
        label("title-carry", items[-1][1], {**absolute(left=U * TITLE_SLOT[0], top=U * TITLE_SLOT[1], width=width, height=height),
              **typeface(face, 24, height, rgb(tint)), "white-space": keyword("nowrap"), "opacity": number(0),
              "transform": transform()}),
    ])
    # The label travels from its plate to the title slot, furniture at both.
    fixed_text(doc, carry)
    return carry


def hide_carry_at_home(doc: Document) -> None:
    """Opening the menu, or coming back from a page, starts without a carried
    title: a page's title leaves at once on Back (anim_loadOut drops it in
    0 ms). A play retargets its first key to the current value, so the label
    is hidden by 1 ms rather than faded out."""
    for timeline in doc.timelines.items:
        if timeline["id"] in ("open", "returnHome"):
            timeline["tracks"].append(track("title-carry", "opacity", [(0, number(0)), (1, number(0)), (timeline["durationMs"], number(0))]))


# ------------------------------------------------------------------ the emblem

# The Quake emblem traced from q4logo in its 260x260 u window at 380,125 u
# (visual specification 1.7). The source path lives beside the atlas.
EMBLEM = [
    (91.28, 3.55), (84.55, 7.11), (77.31, 11.81), (70.46, 17.27), (66.65, 20.82), (61.45, 26.41), (57.13, 31.87), (52.81, 38.34),
    (49.26, 44.69), (46.34, 51.29), (44.18, 57.26), (42.66, 62.59), (41.13, 69.95), (40.12, 79.60), (40.12, 89.50), (41.13, 99.02),
    (42.15, 104.23), (44.18, 111.72), (47.23, 119.84), (49.51, 124.54), (47.23, 130.51), (47.10, 131.78), (53.45, 141.43), (60.68, 149.93),
    (68.17, 149.55), (72.24, 153.23), (76.81, 156.79), (77.57, 163.64), (78.20, 164.40), (85.06, 168.47), (95.09, 173.04), (97.88, 174.05),
    (98.64, 174.05), (104.10, 170.50), (104.74, 170.37), (110.96, 172.02), (120.48, 173.42), (120.86, 173.80), (120.86, 212.77), (121.11, 213.28),
    (121.37, 217.98), (122.38, 225.98), (124.92, 239.31), (129.11, 256.57), (129.75, 257.33), (130.25, 256.57), (134.95, 236.89), (136.98, 225.98),
    (138.00, 217.98), (138.25, 213.28), (138.51, 212.77), (138.51, 173.80), (138.89, 173.42), (146.50, 172.40), (154.88, 170.37), (160.72, 174.05),
    (161.48, 174.05), (171.26, 169.99), (177.23, 166.82), (181.67, 163.90), (182.56, 156.79), (191.19, 149.55), (198.68, 149.93), (205.54, 141.93),
    (212.27, 131.78), (212.27, 130.89), (209.85, 124.54), (212.14, 119.84), (215.19, 111.72), (217.22, 104.23), (218.23, 99.02), (219.25, 89.50),
    (219.25, 79.60), (218.23, 69.95), (215.69, 58.91), (212.14, 49.13), (208.08, 40.88), (204.90, 35.67), (200.71, 29.83), (193.86, 21.96),
    (188.91, 17.27), (184.59, 13.71), (174.81, 7.11), (167.71, 3.55), (167.83, 3.94), (168.34, 3.94), (168.72, 4.44), (173.93, 7.49),
    (180.78, 12.70), (186.24, 17.77), (191.45, 23.61), (196.52, 30.72), (200.08, 36.82), (203.12, 43.54), (205.66, 50.91), (207.57, 59.03),
    (208.58, 67.54), (208.71, 75.54), (208.20, 82.14), (207.06, 89.12), (205.54, 95.09), (203.51, 101.05), (199.57, 109.69), (194.49, 117.94),
    (188.27, 125.68), (181.80, 132.03), (174.81, 137.49), (168.21, 141.68), (158.44, 146.25), (148.92, 149.30), (138.89, 151.07), (138.51, 150.69),
    (138.51, 128.35), (137.87, 124.54), (136.47, 118.96), (134.44, 112.86), (130.38, 103.09), (129.62, 102.32), (128.98, 103.09), (124.92, 112.86),
    (122.89, 118.96), (121.49, 124.54), (120.86, 128.35), (120.86, 150.69), (120.35, 151.07), (114.51, 150.19), (106.39, 148.15), (99.40, 145.62),
    (91.15, 141.68), (82.77, 136.22), (76.43, 131.02), (71.22, 125.81), (67.54, 121.49), (64.87, 117.94), (60.94, 111.72), (58.78, 107.78),
    (55.86, 101.05), (52.69, 90.90), (50.91, 79.60), (50.65, 70.08), (51.67, 59.79), (53.83, 50.53), (57.26, 41.13), (61.95, 32.12),
    (67.41, 24.25), (74.27, 16.63), (82.39, 9.65), (87.60, 6.09), (91.79, 3.68),
]


# The ring's center in the emblem window, and a reach from it past the spike's foot.
RING_CENTER = (129.7, 89.5)
GLINT_REACH = 170.0
GLINT_PERIOD_MS = 9000
GLINT_FRACTION = 0.12


def rotation(degrees: float) -> dict:
    return {"type": "transform", "unit": "dp", "value": [0, 0, 1, 1, degrees]}


def emblem(doc: Document) -> dict:
    """The watermark's darkening blend multiplies the lit field by
    rgb(168,166,57); a rim light at 0.20 follows its outline, and a glint
    runs around it every 9 s (section 13.8).

    The glint is the rim drawn bright under a wedge mask 12% of a turn wide
    that turns about the ring's center; the rim inside it turns back, so the
    outline stays put and only the lit stretch moves. Its light falls off
    toward the wedge's edges. Reduced motion hides it, keeping only the rim."""
    outline = [(U * x, U * y) for x, y in EMBLEM]
    cx, cy = RING_CENTER
    side = U * 2 * GLINT_REACH
    centre = side / 2
    shifted = [(U * (x - cx) + centre, U * (y - cy) + centre) for x, y in EMBLEM]
    half = math.pi * GLINT_FRACTION
    reach = side * 0.55

    def spoke(angle: float) -> tuple:
        return (round(centre + reach * math.sin(angle), 3), round(centre - reach * math.cos(angle), 3))

    wedge = [(centre, centre), spoke(-half), spoke(0), spoke(half)]
    left, right = spoke(-half), spoke(half)
    falloff = linear(left, right, [(0, [1, 1, 1, 0]), (0.5, [1, 1, 1, 1]), (1, [1, 1, 1, 0])])
    glint = group("emblem-glint", {**absolute(left=U * cx - centre, top=U * cy - centre, width=side, height=side),
                                   "transform": rotation(0), "display": keyword("block")}, [
        vector("emblem-glint-rim", {**absolute(left=0, top=0, width=side, height=side), "transform": rotation(0)}, [
            path("glint", shifted, stroke=stroke(solid(rgb(RAIL_DARK, 0.8)), 1.5, minimum=1, join="round"), blend="additive")]),
    ], mask={"paths": [path("wedge", wedge, fill=falloff)]})
    doc.state["motion_reduced"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"}
    doc.bind("emblem-glint.display", "emblem-glint", "display", {"op": "select", "args": [{"state": "motion_reduced"}, "none", "block"]})
    doc.timelines.add("emblemGlint", GLINT_PERIOD_MS, [
        track("emblem-glint", "transform", [(0, rotation(0)), (GLINT_PERIOD_MS, rotation(360))]),
        track("emblem-glint-rim", "transform", [(0, rotation(0)), (GLINT_PERIOD_MS, rotation(-360))])], iterations=0)
    doc.events.setdefault("onInit", []).append({"op": "playTimeline", "timeline": "emblemGlint"})
    return group("emblem", absolute(left=U * 380, top=U * 125, width=U * 260, height=U * 260), [
        vector("emblem-art", absolute(left=0, top=0, width=U * 260, height=U * 260), [
            path("shadow", outline, fill=solid(rgb("#A8A639")), blend="multiply"),
            path("rim", outline, stroke=stroke(solid(rgb(RAIL_DARK, 0.2)), 1.35, minimum=1, join="round"), blend="additive"),
        ]),
        glint,
    ])


# ------------------------------------------------------------------- the title

MENU_BACKGROUNDS = ["gfx/guis/mainmenu/level_mcc.dds", "gfx/guis/mainmenu/level_convoy1.dds", "gfx/guis/mainmenu/level_core1.dds",
                    "gfx/guis/mainmenu/level_process1_first.dds", "gfx/guis/mainmenu/level_tram1.dds"]


def montage(doc: Document) -> dict:
    """Backdrop levelshots cross-fade over 2000 ms every 5000 ms (section 8,
    ambient loops); decorative, so reduced motion holds the first picture."""
    shots = []
    period = 5000 * len(MENU_BACKGROUNDS)
    tracks = []
    for index, source in enumerate(MENU_BACKGROUNDS):
        ident = f"shot-{index}"
        # The pictures keep a white tint. Animate isolated opacity so the
        # runtime can replay the fade without relaying out the menu tree.
        shots.append(picture(ident, source, {**FULL, "opacity": number(1 if index == 0 else 0)}))
        if index == 0:
            continue
        start = 5000 * index
        last = index == len(MENU_BACKGROUNDS) - 1
        if not last:
            keys = [(0, number(0)), (start, number(0)), (start + 2000, number(1)),
                    (period - 3000, number(1)), (period - 2990, number(0)), (period, number(0))]
        else:
            # The newest picture fades out as the loop wraps, revealing the first.
            keys = [(0, number(1)), (2000, number(0)), (start, number(0)),
                    (start + 2000, number(1)), (period, number(1))]
            shots[-1]["properties"]["opacity"] = number(1)
        tracks.append(track(ident, "opacity", keys))
    doc.timelines.add("montage", period, tracks, iterations=0)
    doc.events.setdefault("onInit", []).append({"op": "playTimeline", "timeline": "montage"})
    return group("shots", dict(FULL), shots)


MESSAGE_TOP = 364.0     # u: the message line (message_line)
MESSAGE_END = 619.0     # u: above 100 % text it runs on to the 4:3 view's 32 dp margin


def reflow_message_line(doc: Document, nav: dict, sentences: list, base: dict) -> None:
    """Section 2's text scale on the home's message line: above 100 % each
    item's sentence runs on to the 4:3 view's margin and its lines grow with
    the text, and CONTINUE's save title and detail stand on lines grown with
    theirs, a little tighter. Where the running language's lines would reach
    the plinth, the plinth steps down to clear them, toward the prompt bar;
    at 100 % nothing moves."""
    nodes = {node["id"]: node for node in walk_nodes(nav)}
    text_source(doc)
    wide = U * (MESSAGE_END - 44)
    needs, deepest = [], 0.0
    for key in sentences:
        ident = [name for name in nodes if name.startswith("detail-") and name.endswith("-text") and
                 nodes[name]["properties"]["text"]["value"] == key][0]
        line = nodes[ident]["properties"]["line-height"]["value"]
        counts = wrapped_lines((key,), "lowpixel", nodes[ident]["properties"]["font-size"]["value"], wide)
        lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
        need = text_times({"op": "*", "args": [lines, line]})
        needs.append(need)
        deepest = max(deepest, 2 * line * max(found[2] for found in counts.values()))
        for part in (ident, ident[:-len("-text")]):
            own = nodes[part]["properties"]
            doc.bind(f"text.{part}.width", part, "width", {"op": "select", "args": [LARGER, wide, own["width"]["value"]]})
            doc.bind(f"text.{part}.height", part, "height", {"op": "select", "args": [LARGER, need, own["height"]["value"]]})
    # CONTINUE: the title's 24 dp line tightens to 20 dp and the detail's to
    # 17 dp of the text size by 200 % (34 and 28 dp type on 40 and 34 dp).
    title_line, detail_line = text_sum(28, text_times(-4)), text_sum(31, text_times(-7))
    title_h, detail_h = text_times(title_line), text_times(detail_line)
    shot_w = nodes["detail-continue-title"]["properties"]["left"]["value"]
    for ident, top, line, height in (("detail-continue-title", 0, title_line, title_h),
                                     ("detail-continue-detail", title_h, detail_line, detail_h)):
        own = nodes[ident]["properties"]
        doc.bind(f"text.{ident}.width", ident, "width", {"op": "select", "args": [LARGER, wide - shot_w, own["width"]["value"]]})
        doc.bind(f"text.{ident}.line-height", ident, "line-height", line)
        doc.bind(f"text.{ident}.height", ident, "height", height)
        if top:
            doc.bind(f"text.{ident}.top", ident, "top", top)
        # The save's name and description are the player's and the game's,
        # unmeasured here: past the margin they end in an ellipsis.
        own.update({"overflow": keyword("visible"), "text-overflow": keyword("clip")})
        doc.bind(f"text.{ident}.overflow", ident, "overflow", {"op": "select", "args": [LARGER, "hidden", "visible"]})
        doc.bind(f"text.{ident}.text-overflow", ident, "text-overflow", {"op": "select", "args": [LARGER, "ellipsis", "clip"]})
    held = text_sum(title_h, detail_h)
    own = nodes["detail-continue"]["properties"]
    doc.bind("text.detail-continue.width", "detail-continue", "width", {"op": "select", "args": [LARGER, wide, own["width"]["value"]]})
    doc.bind("text.detail-continue.height", "detail-continue", "height", {"op": "select", "args": [LARGER, held, own["height"]["value"]]})
    # The plinth clears the tallest message, 3 dp under it.
    tallest = {"op": "select", "args": [{"state": "menu_continue"}, held, 0]}
    for need in needs:
        tallest = {"op": "max", "args": [need, tallest]}
    rest = base["properties"]["top"]["value"]
    doc.bind("text.plinth.top", "plinth", "top",
             {"op": "select", "args": [LARGER, {"op": "max", "args": [rest, text_sum(U * MESSAGE_TOP + 3, tallest)]}, rest]})
    if U * MESSAGE_TOP + 3 + deepest + base["properties"]["height"]["value"] > U * 447:
        raise SystemExit("the home's message line would push the plinth into the prompt bar at 200 % text")


TALL_HOME_TOP, TALL_HOME_BOTTOM = 190.0, 150.0   # dp: under the home's top band, over its bottom band


def tall_home_plinth(doc: Document, base: dict, scale) -> None:
    """The plinth at the column's scale: its links grow with it, and its
    shape, drawn from its foot and trailing edge, keeps its cuts."""
    props = base["properties"]
    from_foot(base["paths"], props["height"]["value"], 0.001)
    from_foot(base["paths"], props["width"]["value"], props["width"]["value"] / 2, axis=0)
    # The running language's links (the plinth's width) must fit the canvas
    # inside the column's margins, which a long language's do at less.
    width = next((binding["value"] for binding in doc.bindings if binding["node"] == base["id"] and binding["property"] == "width"),
                 props["width"]["value"])
    fitted = {"op": "min", "args": [scale, {"op": "/", "args": [CANVAS_W - 2 * props["left"]["value"], width]}]}
    tall_scale(doc, base, fitted, own=("left", "width", "height"))


def tall_pause(doc: Document, home: dict, title: dict, nav: dict, block: dict, base: dict) -> None:
    """A pause in a tall view: GAME PAUSED, the actions, EXIT on its plinth
    and the level block under them, centred, in one column between the
    bands."""
    tall_canvas(doc, home)
    plates = sum(child["properties"]["height"]["value"] for child in nav["children"])
    card_w, card_h = block["properties"]["width"]["value"], block["properties"]["height"]["value"]
    scale, _ = tall_column(doc, [(title["properties"]["height"]["value"], title), (6, None), (plates, nav), (6, None),
                                 (base["properties"]["height"]["value"], base), (24, None), (card_h, block)],
                           TALL_HOME_TOP, TALL_HOME_BOTTOM)
    tall_scale(doc, title, scale, own=("left", "width", "height"))
    tall_scale(doc, nav, scale, own=("width",))
    tall_home_plinth(doc, base, scale)
    tall_scale(doc, block, scale, own=("width", "height"))
    tall_bind(doc, block, "left", {"op": "*", "args": [0.5, text_sum(CANVAS_W, {"op": "*", "args": [-card_w, scale]})]})


def tall_title(doc: Document, stage: dict, home: dict, nav: dict, base: dict, sentences: list) -> None:
    """The title in a tall view: the wordmark, the navigation plates, the
    focused item's message line and the plinth in one column across the
    canvas between the bands; the emblem gives way to the column."""
    nodes = {node["id"]: node for node in walk_nodes(nav)}
    for group_node in (stage, home):
        tall_canvas(doc, group_node)
    wordmark = next(child for child in stage["children"] if child["id"] == "wordmark")
    emblem = next(child for child in stage["children"] if child["id"] == "emblem")
    emblem["properties"]["display"] = keyword("block")
    doc.bind("tall.emblem.display", "emblem", "display", {"op": "select", "args": [tall_condition(doc), "none", "block"]})
    # The message line's lines at the column's narrowest, in 100 % dp.
    narrowest = (CANVAS_W - TALL_MARGIN * (1 + TALL_MOST)) / TALL_MOST
    # Every item's sentence takes the room the longest one needs.
    need = tall_text_height(doc, sentences, "lowpixel", 17, 22, narrowest)
    held = text_sum(text_times(text_sum(28, text_times(-4))), text_times(text_sum(31, text_times(-7))))
    tallest = {"op": "max", "args": [need, {"op": "select", "args": [{"state": "menu_continue"}, held, 0]}]}
    plate = U * 30
    rows = {"op": "select", "args": [{"state": "menu_continue"}, 5 * plate, 4 * plate]}
    mark_w, mark_h = wordmark["properties"]["width"]["value"], wordmark["properties"]["height"]["value"]
    scale, tops = tall_column(doc, [(mark_h, wordmark), (24, None), (rows, nav), (6, None), (tallest, None), (10, None),
                                    (base["properties"]["height"]["value"], base)], TALL_HOME_TOP, TALL_HOME_BOTTOM)
    tall_bind(doc, wordmark, "left", {"op": "*", "args": [0.5, text_sum(CANVAS_W, {"op": "*", "args": [-mark_w, scale]})]})
    for key, value in (("width", mark_w), ("height", mark_h)):
        tall_bind(doc, wordmark, key, {"op": "*", "args": [scale, value]})
    tall_scale(doc, nav, scale, own=("width",))
    tall_home_plinth(doc, base, scale)
    # Each message line stands under the plates, 6 dp of the column below the
    # last, across the rest of the canvas (its plate starts at the view's
    # leading edge less the bleed, which the column scales too).
    width = text_sum(CANVAS_W - TALL_MARGIN, {"op": "*", "args": [-TALL_MARGIN, scale]})
    for index, ident in enumerate(["detail-continue", *[name[:-len("-text")] for name in nodes
                                                         if name.startswith("detail-") and name.endswith("-text")]]):
        below = 5 * plate - index * plate + 6 if index else 5 * plate + 6
        tall_bind(doc, nodes[ident], "top", {"op": "*", "args": [scale, below]})
        tall_bind(doc, nodes[ident], "width", width)
        tall_bind(doc, nodes[ident], "height", {"op": "*", "args": [scale, held if index == 0 else need]})
        if index:
            text_node = nodes[f"{ident}-text"]
            tall_bind(doc, text_node, "width", width)
            tall_bind(doc, text_node, "height", {"op": "*", "args": [scale, need]})
    shot_w = nodes["detail-continue-title"]["properties"]["left"]["value"]
    for ident in ("detail-continue-title", "detail-continue-detail"):
        tall_bind(doc, nodes[ident], "width", text_sum(width, {"op": "*", "args": [-shot_w, scale]}))


def title_document() -> dict:
    doc = Document("openq4.title")
    doc.state.update({
        "menu_continue": {"type": "boolean", "initial": False},
        "menu_continue_title": {"type": "string", "initial": ""},
        "menu_continue_detail": {"type": "string", "initial": ""},
        "menu_continue_shot": {"type": "string", "initial": ""},
    })
    doc.session("continue", "continue")
    doc.session("singlePlayer", "singlePlayer")
    doc.session("loadGame", "loadGame")
    doc.session("multiplayer", "multiplayer")
    doc.session("settings", "settings")
    doc.session("mods", "mods")
    doc.session("updates", "updates")
    doc.session("credits", "credits")
    doc.session("quit", "quit")
    exit_modal = confirmation(doc, "exitModal", "#str_200169", "#str_200170", "quit", frame_leave_ms=200,
                              dim=(("wordmark", "image-color", colour([1, 1, 1, 1]), colour([0.4, 0.4, 0.4, 1])),))
    mods_dialog = mods_modal(doc)
    doc.events["onBack"] = [{"op": "call", "event": "exitModalShow"}]

    # The message line at 44,364 u explains the focused item (section 13.7):
    # the newest save's levelshot, title and time for CONTINUE, else a sentence.
    nav_top = 212.2
    def offset(row: int) -> float:
        return 364 - (nav_top + 30 * row)
    continue_detail = message_line("detail-continue", offset(0), [
        picture("detail-continue-shot", "", {**absolute(left=0, top=U * 1, width=U * 56, height=U * 31.5)}),
        vector("detail-continue-frame", absolute(left=0, top=U * 1, width=U * 56, height=U * 31.5), [
            path("frame", [(0.5, 0.5), ({"fraction": 1, "dp": -0.5}, 0.5), ({"fraction": 1, "dp": -0.5}, {"fraction": 1, "dp": -0.5}), (0.5, {"fraction": 1, "dp": -0.5})],
                 stroke=stroke(solid(rgb(PICTURE)), 1.5))]),
        label("detail-continue-title", "#str_200984", {**absolute(left=U * 64, top=0, width=U * 369, height=U * 16),
              **typeface("lowpixel", 17, U * 16, [1, 1, 1, 0.8]), "white-space": keyword("nowrap")}),
        label("detail-continue-detail", "#str_200984", {**absolute(left=U * 64, top=U * 16, width=U * 369, height=U * 16),
              **typeface("lowpixel", 14, U * 16, [0.72, 0.76, 0.62, 1]), "white-space": keyword("nowrap")}),
    ])
    doc.bind("detail-continue-shot.image", "detail-continue-shot", "image", {"state": "menu_continue_shot"})
    doc.bind("detail-continue-title.text", "detail-continue-title", "text", {"state": "menu_continue_title"})
    doc.bind("detail-continue-detail.text", "detail-continue-detail", "text", {"state": "menu_continue_detail"})
    rows = [("nav_singleplayer", "#str_42000", "singlePlayer", "#str_230026"), ("nav_loadgame", "#str_200001", "loadGame", "#str_230027"),
            ("nav_multiplayer", "#str_200002", "multiplayer", "#str_230028"), ("nav_settings", "#str_200009", "settings", "#str_230029")]
    buttons = [navigation_plate(doc, "nav_continue", "#str_200984", action="continue", detail=continue_detail)]
    for index, (ident, key, action, sentence) in enumerate(rows, start=1):
        detail_id = f"detail-{ident[4:]}"
        detail = message_line(detail_id, offset(index), [
            label(f"{detail_id}-text", sentence, {**absolute(left=0, top=0, width=U * 433, height=U * 33),
                  **typeface("lowpixel", 17, 22, [1, 1, 1, 0.8])})])
        # Without a save CONTINUE collapses and every row rises one pitch.
        doc.bind(f"{detail_id}.top", detail_id, "top", {"op": "select", "args": [
            {"state": "menu_continue"}, round(U * offset(index), 3), round(U * offset(index - 1), 3)]})
        buttons.append(navigation_plate(doc, ident, key, action=action, detail=detail))
    nav = group("nav", {**absolute(left=0, top=U * nav_top, width=U * 413), "display": keyword("flex"),
                        "flex-direction": keyword("column")}, buttons)
    doc.bind("nav_continue.display", "nav_continue", "display", {"op": "select", "args": [{"state": "menu_continue"}, "block", "none"]})
    # The plinth's secondary links (section 13.7); DEMOS is a Multiplayer
    # section (section 9).
    links = [link(doc, "link_mods", "#str_200010", action="mods"), link(doc, "link_updates", "#str_200011", action="updates"),
             link(doc, "link_credits", "#str_200012", action="credits"), link(doc, "link_exit", "#str_200013", event="exitModalShow")]
    wrap_menu(buttons, links)
    stage = group("home-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2),
                                 "opacity": number(1)}, [
        emblem(doc),
        picture("wordmark", "gfx/guis/mainmenu/q4text", {**absolute(left=U * 6, top=U * 119, width=U * 376, height=U * 92),
                                                          "image-blend": keyword("additive"), "image-color": colour([1, 1, 1, 1])}, fit="fill"),
    ])
    base = plinth(doc, links, ("#str_200010", "#str_200011", "#str_200012", "#str_200013"))
    reflow_message_line(doc, nav, [row[3] for row in rows], base)
    content = group("home", {**FULL, "transform": transform(), "opacity": number(1)}, [
        group("home-content", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}, [
            nav,
            base,
        ]),
    ])
    tall_title(doc, stage, content["children"][0], nav, base, [row[3] for row in rows])
    tall_dialog(doc, exit_modal)
    root = group("screen", {**FULL, "background-color": colour([0, 0, 0, 1]), "font-family": font("marine"),
                            "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [
        depth_layer(doc, "depth-backdrop", 1.0, [montage(doc)]),
        # The scene retains continuous parallax. Keep pixel-coverage vector
        # furniture still: fractional translation would retessellate the
        # grid and every broad band/glow on each pointer movement.
        group("depth-light", {**FULL, "pointer-events": keyword("none")}, lit_field("field", 0.7)),
        stage,
        group("depth-frame", {**FULL, "pointer-events": keyword("none")}, framing_bands("band")),
        content,
        prompt_bar(doc, [("#str_107019", "#str_231042"), ("#str_107020", "#str_200013")]),
        title_carry(doc, [("singlePlayer", "#str_42000", 212.2), ("loadGame", "#str_200001", 242.2),
                          ("multiplayer", "#str_200002", 272.2), ("settings", "#str_200009", 302.2)], continue_rows=True),
        exit_modal,
        mods_dialog,
        group("fade", {**FULL, "background-color": colour([0, 0, 0, 0]), "pointer-events": keyword("none")}),
    ])
    band_motion(doc, "band", "home", [("wordmark", "image-color", colour([1, 1, 1, 1]), colour([1, 1, 1, 0])),
                                      ("home-stage", "opacity", number(1), number(0))], quick=("plinth", "prompts"))
    hide_carry_at_home(doc)
    # Opening the menu focuses its primary action (section 13.4): CONTINUE
    # while there is a save to continue, else SINGLE PLAYER. Coming back from
    # a page (returnHome) keeps the focus on the item that led there.
    doc.events["open"] = [{"op": "playTimeline", "timeline": "open"},
                          {"op": "if", "condition": {"state": "menu_continue"},
                           "then": [{"op": "focus", "control": "nav_continue"}],
                           "else": [{"op": "focus", "control": "nav_singleplayer"}]}]
    return fit_narrow(doc.build(root))


SUBPAGE_STEP = 97.0                                   # one notch pitch (frame.step, section 8)
TOP_TO_SUBPAGE = ((323 + SUBPAGE_STEP) * U, -63 * U)
CRUMB_SLOT = (39.0, 12.0)                             # the parent title in the band's thin span
SUBPAGE_SLOT = (136.0, 19.0)                          # the sub-page title at the step's foot
CRUMB_SPAN = 83.0                                     # the thin span, 39 u to the step at 122 u
PAGE_TITLE_W, PAGE_TITLE_H = 300.0, 24.4 * 0.75       # the 18 dp screen title (text.title)
CRUMB_SCALE = 0.8
SUBPAGE_REVERSE_MS = (150, 200, 250, 300)            # a reversed frame.step (MOT-016)


def page_title(ident: str, key: str, left: float, top: float, alpha: float = 0.5) -> dict:
    """The screen title in the top band (section 9): Marine 18 dp at 0.50,
    where the home's title carry lands."""
    return label(ident, key, {**absolute(left=U * left, top=U * top, width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                              **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, alpha]), "white-space": keyword("nowrap"),
                              "transform": transform()})


def crumb_transform(scale: float = CRUMB_SCALE) -> dict:
    """The page title as the crumb: `scale` of its size (80 %, or less to fit
    the span) and 7 u up, its leading and top edges held while it scales
    about its center."""
    width, height = U * PAGE_TITLE_W, U * PAGE_TITLE_H
    return {"type": "transform", "unit": "dp", "value": [round(-(1 - scale) * width / 2, 3),
                                                       round(-(1 - scale) * height / 2 - U * (TITLE_SLOT[1] - CRUMB_SLOT[1]), 3),
                                                       round(scale, 4), round(scale, 4), 0]}


STRINGS = ROOT / "content" / "baseoq4" / "pak0" / "strings"
LANGUAGES = ("english", "french", "german", "italian", "polish", "russian", "spanish",
             "brazilian", "czech", "hungarian", "turkish", "ukrainian")


def localized(key: str) -> dict:
    """The shipped openQ4 string tables' text for `key`, by sys_lang value."""
    texts = {}
    for language in LANGUAGES:
        for line in (STRINGS / f"{language}_openq4.lang").read_text(encoding="utf-8").splitlines():
            match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
            if match and match.group(1) == key:
                texts[language] = match.group(2)
    return texts


@lru_cache(maxsize=8)
def font_metrics(face: str) -> tuple:
    """A shipped face's units per em, each mapped code point's advance in
    font units and the advance of '?', which stands in for a missing glyph;
    read once per face and shared by all localized rows."""
    data = (ROOT / "content" / "baseoq4" / "pak0" / "fonts" / f"{face}.ttf").read_bytes()
    tables = {}
    for index in range(struct.unpack(">H", data[4:6])[0]):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * index:28 + 16 * index])
        tables[tag.decode("latin-1")] = offset
    units = struct.unpack(">H", data[tables["head"] + 18:tables["head"] + 20])[0]
    metrics = struct.unpack(">H", data[tables["hhea"] + 34:tables["hhea"] + 36])[0]
    cmap, glyphs = tables["cmap"], {}
    for index in range(struct.unpack(">H", data[cmap + 2:cmap + 4])[0]):
        offset = cmap + struct.unpack(">I", data[cmap + 8 + 8 * index:cmap + 12 + 8 * index])[0]
        form = struct.unpack(">H", data[offset:offset + 2])[0]
        if form == 4:
            pairs = struct.unpack(">H", data[offset + 6:offset + 8])[0] // 2
            ends = struct.unpack(f">{pairs}H", data[offset + 14:offset + 14 + 2 * pairs])
            base = offset + 16 + 2 * pairs
            starts = struct.unpack(f">{pairs}H", data[base:base + 2 * pairs])
            deltas = struct.unpack(f">{pairs}h", data[base + 2 * pairs:base + 4 * pairs])
            ranges = base + 4 * pairs
            for segment in range(pairs):
                shift = struct.unpack(">H", data[ranges + 2 * segment:ranges + 2 * segment + 2])[0]
                for code in range(starts[segment], ends[segment] + 1):
                    if code == 0xFFFF:
                        continue
                    if shift == 0:
                        glyph = (code + deltas[segment]) & 0xFFFF
                    else:
                        at = ranges + 2 * segment + shift + 2 * (code - starts[segment])
                        glyph = struct.unpack(">H", data[at:at + 2])[0]
                        glyph = (glyph + deltas[segment]) & 0xFFFF if glyph else 0
                    glyphs.setdefault(code, glyph)
        elif form == 12:
            for group_index in range(struct.unpack(">I", data[offset + 12:offset + 16])[0]):
                first, last, glyph = struct.unpack(">III", data[offset + 16 + 12 * group_index:offset + 28 + 12 * group_index])
                for code in range(first, last + 1):
                    glyphs.setdefault(code, glyph + code - first)

    def advance(glyph: int) -> int:
        at = tables["hmtx"] + 4 * min(glyph, metrics - 1)
        return struct.unpack(">H", data[at:at + 2])[0]
    return units, {code: advance(glyph) for code, glyph in glyphs.items() if glyph}, advance(glyphs.get(ord("?"), 0))


def text_width(face: str, text: str, size: float) -> float:
    """The width in dp of `text` set in a shipped face at `size` dp, as the
    retained host lays it out: each glyph's advance at pixels / unitsPerEm,
    no kerning, '?' for a missing glyph."""
    units, advances, fallback = font_metrics(face)
    return sum(advances.get(ord(char), fallback) for char in text) * size / units


def crumb_fits(key: str) -> dict:
    """The crumb's scale for each shipped language: 80 % of the title, or
    less where that would not fit the band's thin span, but never below the
    13 dp type floor (section 9: the crumb shrinks to fit before it
    truncates)."""
    span, floor = U * CRUMB_SPAN - 2, 13 / 18
    return {language: max(floor, min(CRUMB_SCALE, span / text_width("marine", text, 18)))
            for language, text in merged_text(key).items()}


def by_language(doc: Document, prefix: str, fits: dict) -> list:
    """Steps playing `<prefix>-<language>` for the running language, or
    `<prefix>` where no fit was measured."""
    if "language" not in doc.state:
        doc.state["language"] = {"type": "string", "initial": "", "cvar": "sys_lang"}
    steps = [{"op": "playTimeline", "timeline": prefix}]
    for language in sorted(fits, reverse=True):
        steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "language"}, language]},
                  "then": [{"op": "playTimeline", "timeline": f"{prefix}-{language}"}], "else": steps}]
    return steps


def campaign_document(campaigns: bool) -> dict:
    """Single Player page and Campaign sub-page, specification sections 8/9
    and the sub-page level of 1.10.

    Use the shared Marine plates and docked vector bands. Content readiness
    comes from the filesystem probe, never a module or a folder's name alone.

    The page titles itself in the top band's slot (39,19 u). The Campaign
    sub-page holds the sub-page state: the top band one notch pitch (97 u) on
    toward the trailing edge, the bottom band docked, the page title as the
    crumb in the band's thin span (39,12 u, 80 %, 0.40, cut at the step) and
    the sub-page title at the step's foot (136,19 u). The session plays the
    change between the two documents (section 8, FLOW-050):
    subpageEnter  on the page, 350 ms: its plates fade over 150 ms while it
                  sweeps 640 u toward the trailing edge over 300 ms; its
                  title becomes the crumb (title.crumb, 150 ms, linear); from
                  50 ms the top band steps (frame.step, accel(150, 150) over
                  300 ms) and the CAMPAIGN label carries into the slot at the
                  step's foot (title.carry, 300 ms going deeper);
    onActivate    every arrival: a page's own plates at once at rest, its
                  backing fading in over 150 ms (content.in), so the sub-page
                  presented at 350 ms needs nothing more, and the focus on its
                  primary action or the row that led back to it;
    subpageLeave  on the sub-page, 300 ms: its title drops at once, it sweeps
                  640 u toward the leading edge over 300 ms with its plates
                  (screen.return, linear), its backing fades over 250 ms
                  (content.out), the top band steps back at once, and the
                  crumb returns to the title slot over the last 150 ms;
    subpageReturn on the page at 300 ms: its plates and content fade in over
                  150 ms with the bands docked and its title in the slot;
    subpageReverseN on the page, Back while it goes deeper: every part returns
                  from where it stands over N ms (150-300 ms, by the distance
                  the band has stepped).
    Leaving the screen (onDeactivate) places a document at rest while it is
    hidden, so it is never seen moved when it next presents, and a change
    that was cut short never leaves it moved. Under reduced motion the
    session hands over at once and the arrival's 80 ms fade is the change."""
    doc = Document("openq4.campaigns" if campaigns else "openq4.singleplayer")
    back = "campaignBack" if campaigns else "campaignHome"
    doc.session("back", back)
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    buttons = []
    if campaigns:
        # The campaign choice, then its two children (spec 1.10's menu levels):
        # QUAKE 4 and THE AWAKENING choose the campaign, the chosen one marked
        # with the orange rail, and move the focus to NEW GAME; NEW GAME and
        # CHAPTERS go deeper to the chosen campaign's difficulty or its levels.
        doc.state.update({"awakening_ready": {"type": "boolean", "initial": False},
                          "awakening_present": {"type": "boolean", "initial": False},
                          "awakening_chosen": {"type": "boolean", "initial": False},
                          "campaign.awakening": {"type": "boolean", "initial": False},
                          "from_chapters": {"type": "boolean", "initial": False}})
        for ident, key, event, awakening in (("quake4", "#str_230039", "chooseQuake4", False),
                                             ("awakening", "#str_230040", "chooseAwakening", True)):
            row = navigation_plate(doc, ident, key, event=event)
            mark = vector(f"chosen-{ident}", {**absolute(left=NAV_LEAD + U * 25, top=U * 10, width=U * 5, height=U * 10), "opacity": number(0)},
                          [path("rail", [(0, 0), (U * 3, 0), (U * 3, U * 10), (0, U * 10)], fill=solid(rgb(ORANGE)))])
            row["children"].append(mark)
            chosen = {"state": "campaign.awakening"} if awakening else {"op": "!", "args": [{"state": "campaign.awakening"}]}
            doc.bind(f"chosen-{ident}.opacity", f"chosen-{ident}", "opacity", {"op": "select", "args": [chosen, 1, 0]})
            doc.events[event] = [{"op": "setState", "values": {"campaign.awakening": awakening}}, {"op": "focus", "control": "newgame"}]
            buttons.append(row)
        doc.bind("awakening.enabled", "awakening", "enabled", {"state": "awakening_ready"})
        rows = [("newgame", "#str_200000", "campaignNew"), ("chapters", "#str_231280", "campaignChapters")]
    else:
        rows = [("campaign", "#str_230038", "campaigns"), ("arena", "#str_42002", "campaignArena")]
    # The sections that go deeper summarize themselves (section 13.7):
    # Single Player's CAMPAIGN over the newest save's levelshot and ARENA over
    # its chosen match's; Campaign's NEW GAME over the active campaign's first
    # level and CHAPTERS over the furthest it has reached. QUAKE 4 and THE
    # AWAKENING keep the column's word on the campaigns.
    summaries = ([("newgame", SUMMARY_NEWGAME, "summary.newgame.shot"), ("chapters", SUMMARY_CHAPTERS, "summary.chapters.shot")]
                 if campaigns else [("campaign", SUMMARY_CAMPAIGN, "summary.campaign.shot"), ("arena", SUMMARY_ARENA, "summary.arena.shot")])
    for ident, key, action in rows:
        doc.session(action, action)
        buttons.append(navigation_plate(doc, ident, key, action=action, subject=summary_subject(ident, summaries)))
    buttons.append(navigation_plate(doc, "back", "#str_230045", action="back"))
    wrap_menu(buttons)
    details = []
    if campaigns:
        for ident, key in [("available", "#str_230041"), ("partial", "#str_230042"), ("absent", "#str_230043")]:
            details.append(label(ident, key, {**absolute(left=0, top=0, width=U * 210, height=U * 140),
                                            "display": keyword("none"),
                                            **typeface("lowpixel", 18, 26, rgb(OLIVE))}))
        doc.bind("available.display", "available", "display", {"op": "select", "args": [{"state": "awakening_ready"}, "block", "none"]})
        for ident, partial in [("partial", True), ("absent", False)]:
            doc.bind(ident + ".display", ident, "display", {"op": "select", "args": [
                {"state": "awakening_ready"}, "none", {"op": "select", "args": [
                    {"state": "awakening_present"}, "block" if partial else "none", "none" if partial else "block"]}]})
    else:
        details.append(label("description", "#str_230044", {**absolute(left=0, top=0, width=U * 210, height=U * 140),
                                                           **typeface("lowpixel", 18, 26, rgb(OLIVE))}))
    bands = framing_bands("band")
    top_rest = transform(*(TOP_TO_SUBPAGE if campaigns else TOP_TO_PAGE))
    bands[0]["properties"]["transform"] = top_rest
    bands[1]["properties"]["transform"] = transform(*BOTTOM_TO_PAGE)
    nav_top = 190.0
    details_group = group("details", {**absolute(left=U * 395, top=U * 190, width=U * 210, height=U * 140), "opacity": number(0)}, details)
    keys = [node["properties"]["text"]["value"] for node in details]
    summary_keys, shots = section_summaries(doc, details_group, summaries)
    # The description, and each summary, grows with the text above 100 % (section 2).
    reflowed = reflow_details(doc, details_group, keys + summary_keys)
    navigation = group("navigation", {**absolute(left=0, top=U * nav_top, width=U * 357), "display": keyword("flex"),
                                      "flex-direction": keyword("column"), "opacity": number(1)}, buttons)
    content = group("content", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2),
                                "transform": transform(), "opacity": number(1)}, [navigation, details_group, *shots])
    # A tall view stands the plates and the description in one column.
    tall_page(doc, content, navigation, details_group, keys + summary_keys)
    fit_summary_shots(doc, shots, summaries, reflowed)
    if campaigns:
        # The session names the active campaign's levels: the other one's are
        # not mounted until choosing it reloads the engine, so its pictures
        # show only while the campaign chosen here is the active one.
        active = {"op": "==", "args": [{"state": "campaign.awakening"}, {"state": "awakening_chosen"}]}
        for shot in shots:
            binding = next(b for b in doc.bindings if b["node"] == shot["id"] and b["property"] == "display")
            binding["value"] = {"op": "select", "args": [active, binding["value"], "none"]}
    # The titles ride outside the sweeping content, in canvas coordinates.
    crumb_key = "#str_42000"
    if campaigns:
        # The crumb's clip ends at the step; while it returns it is let out
        # to the full title width.
        titles = [
            group("crumb-clip", {**absolute(left=U * CRUMB_SLOT[0], top=0, width=U * CRUMB_SPAN, height=U * 40), "overflow": keyword("hidden")}, [
                label("crumb", crumb_key, {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                           **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"),
                                           "transform": crumb_transform()})]),
            page_title("title", "#str_230038", *SUBPAGE_SLOT),
        ]
    else:
        titles = [
            group("title-clip", {**absolute(left=U * TITLE_SLOT[0], top=0, width=U * PAGE_TITLE_W, height=U * 40), "overflow": keyword("hidden")}, [
                label("title", crumb_key, {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                           **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.5]), "white-space": keyword("nowrap"),
                                           "transform": transform()})]),
            # Going deeper, the CAMPAIGN or ARENA label carries from its row
            # into the slot at the step's foot (title.carry, 300 ms).
            *[label(ident, key, {**absolute(left=U * SUBPAGE_SLOT[0], top=U * SUBPAGE_SLOT[1], width=U * 314, height=U * 24.4),
                    **typeface("marine", 24, U * 24.4, rgb(ORANGE)), "white-space": keyword("nowrap"), "opacity": number(0),
                    "transform": transform(), "pointer-events": keyword("none")})
              for ident, key in (("subpage-carry", "#str_230038"), ("arena-carry", "#str_42002"))],
        ]
    titles_group = group("titles", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2),
                                    "pointer-events": keyword("none")}, titles)
    # The titles keep their size in the top band's slots at any text size.
    fixed_text(doc, titles_group)

    page_top, sub_top = transform(*TOP_TO_PAGE), transform(*TOP_TO_SUBPAGE)
    identity = transform()
    swept_right, swept_left = transform(640 * U, 0), transform(-640 * U, 0)
    white = lambda alpha: colour([1, 1, 1, alpha])
    fits = crumb_fits(crumb_key)
    variants = [("", CRUMB_SCALE)] + [(f"-{language}", fit) for language, fit in fits.items()]
    # Every arrival: the plates at once, the backing fading in (content.in).
    doc.timelines.add("arrive", 150, [
        track("content", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
        track("details", "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
    ])
    if campaigns:
        # Hidden, the sub-page waits in the sub-page state with its backing out.
        doc.timelines.add("rest", 1, [
            track("band-top", "transform", [(0, sub_top), (1, sub_top)]),
            track("content", "transform", [(0, identity), (1, identity)]),
            track("content", "opacity", [(0, number(1)), (1, number(1))]),
            track("details", "opacity", [(0, number(0)), (1, number(0))]),
            track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
            track("crumb", "color", [(0, white(0.4)), (1, white(0.4))]),
            track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (1, length(U * CRUMB_SPAN))]),
        ])
        doc.timelines.add("subpageLeave", 300, [
            track("title", "color", [(0, white(0)), (1, white(0)), (300, white(0))]),
            track("content", "transform", [(0, identity), (300, swept_left)]),
            track("details", "opacity", [(0, number(1)), (250, number(0)), (300, number(0))]),
            track("band-top", "transform", [(0, sub_top, ACCEL), (300, page_top)]),
            track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (150, length(U * CRUMB_SPAN)), (151, length(U * PAGE_TITLE_W)),
                                          (300, length(U * PAGE_TITLE_W))]),
            track("crumb", "color", [(0, white(0.4)), (150, white(0.4)), (300, white(0.5))]),
        ])
        # The crumb's fitted scale for each language: at rest, and returning.
        for suffix, scale in variants:
            fitted = crumb_transform(scale)
            doc.timelines.add(f"crumbRest{suffix}", 1, [track("crumb", "transform", [(0, fitted), (1, fitted)])])
            doc.timelines.add(f"crumbReturn{suffix}", 300, [track("crumb", "transform", [(0, fitted), (150, fitted), (300, identity)])])
        doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}, *by_language(doc, "crumbRest", fits)]
        # Every arrival shows the sub-page at once, with the focus on NEW GAME
        # (section 13.4's primary action), or on CHAPTERS coming back from it.
        chosen = [{"op": "focus", "control": "newgame"},
                  {"op": "if", "condition": {"state": "from_chapters"}, "then": [{"op": "focus", "control": "chapters"}]}]
        doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"}, *by_language(doc, "crumbRest", fits), *chosen]
        doc.events["subpageLeave"] = [{"op": "playTimeline", "timeline": "subpageLeave"}, *by_language(doc, "crumbReturn", fits)]
    else:
        # Each carry starts on its row: CAMPAIGN the first, ARENA the second.
        start, arena_start = ({"type": "transform", "unit": "dp", "value": [round(U * (44 - SUBPAGE_SLOT[0]), 3),
                                                                          round(U * (nav_top + 30 * row + 2.8 - SUBPAGE_SLOT[1]), 3), 1, 1, 0]}
                              for row in (0, 1))
        width, height, scale = U * 314, U * 24.4, 0.75
        end = {"type": "transform", "unit": "dp", "value": [round(-(1 - scale) * width / 2, 3), round(-(1 - scale) * height / 2, 3),
                                                           scale, scale, 0]}
        # Hidden, the page waits docked with its title in the slot and its
        # content out, so a return fades it in from nothing.
        doc.timelines.add("rest", 1, [
            track("band-top", "transform", [(0, page_top), (1, page_top)]),
            track("content", "transform", [(0, identity), (1, identity)]),
            track("content", "opacity", [(0, number(0)), (1, number(0))]),
            track("details", "opacity", [(0, number(0)), (1, number(0))]),
            track("title", "transform", [(0, identity), (1, identity)]),
            track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
            track("title-clip", "width", [(0, length(U * PAGE_TITLE_W)), (1, length(U * PAGE_TITLE_W))]),
            *[track(carry, prop, [(0, value), (1, value)]) for carry, origin in (("subpage-carry", start), ("arena-carry", arena_start))
              for prop, value in (("opacity", number(0)), ("transform", origin), ("color", colour(rgb(ORANGE))))],
        ])
        # Going deeper to Campaign or to Arena: the same change, the chosen
        # row's label carrying.
        for timeline, carry, origin in (("subpageEnter", "subpage-carry", start), ("subpageEnterArena", "arena-carry", arena_start)):
            doc.timelines.add(timeline, 350, [
                track("content", "opacity", [(0, number(1)), (150, number(0)), (350, number(0))]),
                track("content", "transform", [(0, identity), (300, swept_right), (350, swept_right)]),
                track("title", "color", [(0, white(0.5)), (150, white(0.4)), (350, white(0.4))]),
                track("title-clip", "width", [(0, length(U * PAGE_TITLE_W)), (150, length(U * CRUMB_SPAN)), (350, length(U * CRUMB_SPAN))]),
                track("band-top", "transform", [(0, page_top), (50, page_top, ACCEL), (350, sub_top)]),
                track(carry, "opacity", [(0, number(1)), (1, number(1)), (350, number(1))]),
                track(carry, "transform", [(0, origin), (1, origin), (50, origin, ACCEL), (350, end)]),
                track(carry, "color", [(0, colour(rgb(ORANGE))), (1, colour(rgb(ORANGE))), (50, colour(rgb(ORANGE)), ACCEL),
                                       (350, white(0.5))]),
            ])
        for suffix, scale in variants:
            doc.timelines.add(f"crumbEnter{suffix}", 350, [
                track("title", "transform", [(0, identity), (150, crumb_transform(scale)), (350, crumb_transform(scale))])])
        doc.timelines.add("subpageReturn", 150, [
            track("content", "transform", [(0, identity), (1, identity), (150, identity)]),
            track("content", "opacity", [(0, number(0)), (150, number(1))]),
            track("details", "opacity", [(0, number(0)), (150, number(1))]),
        ])
        # Back while going deeper: every part returns from where it stands
        # (each play retargets its first key to the current value), over a
        # time proportional to the band's step (150-300 ms).
        for duration in SUBPAGE_REVERSE_MS:
            doc.timelines.add(f"subpageReverse{duration}", duration, [
                track("content", "opacity", [(0, number(0)), (duration, number(1))]),
                track("content", "transform", [(0, identity), (duration, identity)]),
                track("title", "transform", [(0, identity), (duration, identity)]),
                track("title", "color", [(0, white(0.4)), (duration, white(0.5))]),
                track("title-clip", "width", [(0, length(U * CRUMB_SPAN)), (duration, length(U * PAGE_TITLE_W))]),
                track("band-top", "transform", [(0, sub_top, ACCEL), (duration, page_top)]),
                *[track(carry, "opacity", [(0, number(1)), (duration // 2, number(0)), (duration, number(0))])
                  for carry in ("subpage-carry", "arena-carry")],
            ])
            doc.events[f"subpageReverse{duration}"] = [{"op": "playTimeline", "timeline": f"subpageReverse{duration}"}]
        doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}]
        # Back from Arena (the session sets from_arena first) returns onto ARENA;
        # every other arrival focuses CAMPAIGN, the primary action (section
        # 13.4), which Back from Campaign returns onto too.
        doc.state["from_arena"] = {"type": "boolean", "initial": False}
        doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"},
                                    {"op": "if", "condition": {"state": "from_arena"}, "then": [{"op": "focus", "control": "arena"}],
                                     "else": [{"op": "focus", "control": "campaign"}]}]
        for event in ("subpageEnter", "subpageEnterArena"):
            doc.events[event] = [{"op": "playTimeline", "timeline": event}, *by_language(doc, "crumbEnter", fits)]
        doc.events["subpageReturn"] = [{"op": "playTimeline", "timeline": "subpageReturn"}]
    import retained_menu_levels
    if campaigns:
        title_node = titles_group["children"][1]
        title_node["properties"]["left"] = length(0)
        titles_group["children"][1] = group("detail-title-clip", {**absolute(left=U * SUBPAGE_SLOT[0],top=0,width=U * PAGE_TITLE_W,height=U * 40),"overflow":keyword("hidden")}, [title_node])
        # NEW GAME carries the chosen campaign's label from its row, CHAPTERS its own.
        titles_group["children"].append(retained_menu_levels.child_transition(types.SimpleNamespace(**globals()),doc,3,
            [("Quake4","#str_230039",0),("Awakening","#str_230040",1),("Chapters","#str_231280",3)],"#str_230038"))
    result = doc.build(retained_menu_levels.fit_page(types.SimpleNamespace(**globals()),group("screen", {**FULL, "background-color": colour([0, 0, 0, 1])}, [
        *lit_field("field", 0.7), *bands, content, titles_group,
        # The pages' prompt bar, as legends: the page's own BACK is its button.
        page_prompts(doc, in_canvas=False),
    ])))
    result["canvas"]["minWidth"] = 960
    return result


# ------------------------------------------------------------------- the pause

OBJECTIVE_ROWS = 3   # the stock objective screen's three slots
LEVEL_SHOT = 110.0   # u: the level block's levelshot, short enough to leave its foot two time lines
LEVEL_FOOT = 29.0    # u: the time lines' rule above the card's foot


def level_time_lines(doc: Document, pw: float, h: float, tint: list[float]) -> list:
    """The level block's foot (section 13.7): the time in the mission and the
    run's total on one line, how long ago the newest save was written on the
    next, under a faint rule while either has text."""
    rule = vector("level-stats-rule", {**absolute(left=15, top=h - U * LEVEL_FOOT, width=pw, height=1), "display": keyword("none")},
                  [path("rule", [(0, 0.5), (pw, 0.5)], closed=False, stroke=stroke(solid([1, 1, 1, 0.1]), 1))])
    lines = []
    for index, (ident, state) in enumerate((("level-stats", "pause_stats"), ("level-saved", "pause_saved"))):
        lines.append(label(ident, "#str_230030", {**absolute(left=15, top=h - U * (27 - 10 * index), width=pw, height=U * 10),
            **typeface("lowpixel", 11, U * 10, tint), "white-space": keyword("nowrap")}))
        doc.bind(f"{ident}.text", ident, "text", {"state": state})
    empty = {"op": "&&", "args": [{"op": "==", "args": [{"state": "pause_stats"}, ""]}, {"op": "==", "args": [{"state": "pause_saved"}, ""]}]}
    doc.bind("level-stats-rule.display", "level-stats-rule", "display", {"op": "select", "args": [empty, "none", "block"]})
    return [rule, *lines]


def level_row_bindings(doc: Document, ident: str, index: int, text_nodes: tuple) -> dict:
    """A level block row shows the objectives with their state (section
    13.7): the open ones first, newest first, then the ones completed on
    this map, newest first, behind a check. Returns the expression that is
    true while the row holds a completed objective."""
    held = {"state": "pause_objective_count"}
    text = {"state": f"pause_objective_{index}"}
    tail = ""
    for count in range(0, index + 1):
        tail = {"op": "select", "args": [{"op": "==", "args": [held, count]}, {"state": f"pause_completed_{index - count}"}, tail]}
    text = {"op": "select", "args": [{"op": ">", "args": [held, index]}, text, tail]}
    for node in text_nodes:
        doc.bind(f"{node}.text", node, "text", text)
    rows = {"op": "+", "args": [held, {"state": "pause_completed_count"}]}
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"op": ">", "args": [rows, index]}, "block", "none"]})
    completed = {"op": "<=", "args": [held, index]}
    doc.bind(f"{ident}-mark.display", f"{ident}-mark", "display", {"op": "select", "args": [completed, "none", "block"]})
    doc.bind(f"{ident}-check.display", f"{ident}-check", "display", {"op": "select", "args": [completed, "block", "none"]})
    return completed


def level_check(ident: str, tint: list[float]) -> dict:
    """A completed objective's check (the atlas level block)."""
    return vector(f"{ident}-check", {**absolute(left=15, top=2, width=12, height=11), "display": keyword("none")}, [
        path("check", [(0, 6), (3.6, 9.9), (10.5, 1.8)], closed=False, stroke=stroke(solid(tint), 2.1, join="round", cap="round"))])


def level_block(doc: Document) -> dict:
    """The current level block in the emblem's place (section 13.7): a card
    (section 6: black 0.94 in a marine.rail.card rail, 12 and 3 dp cuts, a
    40 dp header) with the levelshot in a picture frame, the level, the
    difficulty, the objectives the player holds and, at its foot, the time in
    the mission with the run's total and the last save. The game publishes
    the objectives, newest first; a map's own objectives summary stands in
    while it publishes none."""
    x, y, w, h = U * 374, U * 118, U * 262, U * 270
    major, minor = 12.0, 3.0
    card = [(minor, 0), (w - major, 0), (w, major), (w, h - minor), (w - minor, h), (major, h), (0, h - major), (0, minor)]
    header = 40.0
    frame = vector("level-card", absolute(left=0, top=0, width=w, height=h), [
        path("body", card, fill=solid([0, 0, 0, 0.94])),
        path("wash", [(minor, 0.75), (w - major, 0.75), (w - major + header * 0.3, header), (0.75, header), (0.75, minor)],
             fill=linear((0, 0), (w, 0), [(0, [1, 1, 1, 0.08]), (0.5, [1, 1, 1, 0.08]), (1, [1, 1, 1, 0])])),
        path("rule", [(0, header), (w, header)], closed=False, stroke=stroke(solid(rgb(RAIL_CARD, 0.5)), 1)),
        path("rail", card, stroke=stroke(solid(rgb(RAIL_CARD)), 1.5)),
        marker_path("mark", 14, 16, 8, rgb(MARKER)),
    ])
    pw, ph = w - 30, U * LEVEL_SHOT
    shot = picture("level-shot", "", {**absolute(left=15, top=header + 12, width=pw, height=ph)})
    shot_frame = vector("level-shot-frame", absolute(left=15, top=header + 12, width=pw, height=ph), [
        path("frame", [(0.5, 0.5), ({"fraction": 1, "dp": -0.5}, 0.5), ({"fraction": 1, "dp": -0.5}, {"fraction": 1, "dp": -0.5}), (0.5, {"fraction": 1, "dp": -0.5})],
             stroke=stroke(solid(rgb(PICTURE)), 1.5))])
    heading = label("level-heading", "#str_230030", {**absolute(left=30, top=0, width=w - 60, height=header),
        **typeface("marine", 14, header, [1, 1, 1, 0.9]), "white-space": keyword("nowrap")})
    below = header + 12 + ph + 10
    name = label("level-name", "#str_230030", {**absolute(left=15, top=below, width=pw, height=24),
        **typeface("marine", 20, 24, [1, 1, 1, 0.9]), "white-space": keyword("nowrap")})
    detail = label("level-detail", "#str_230030", {**absolute(left=15, top=below + 26, width=pw, height=20),
        **typeface("lowpixel", 17, 20, [1, 1, 1, 0.6]), "white-space": keyword("nowrap")})
    objectives_head = label("level-objectives-head", "#str_200291", {**absolute(left=15, top=below + 54, width=pw, height=18),
        **typeface("marine", 14, 18, rgb(OBJECTIVE_HEAD)), "white-space": keyword("nowrap"), "display": keyword("block")})
    rows_top, row_h, stats_rule = below + 76, U * 12.5, h - U * LEVEL_FOOT
    objectives = label("level-objectives", "#str_230030", {**absolute(left=15, top=rows_top, width=pw, height=stats_rule - rows_top - 3),
        **typeface("lowpixel", 14, 18, [0.82, 0.87, 0.71, 1]), "overflow": keyword("hidden"), "display": keyword("block")})
    # Each objective with its state: an open one behind the atlas marker,
    # its title white 0.9; a completed one behind a check at 0.5.
    rows = []
    for index in range(OBJECTIVE_ROWS):
        ident = f"level-objective-{index}"
        mark = vector(f"{ident}-mark", {**absolute(left=15, top=4, width=9, height=9), "display": keyword("block")},
                      [path("mark", [(0, 0), (9, 0), (0, 9)], fill=solid(rgb(OBJECTIVE_MARK, 0.4)))])
        title = label(f"{ident}-text", "#str_230030", {**absolute(left=33, top=0, width=pw - 18, height=row_h),
            **typeface("lowpixel", 13, row_h, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        rows.append(group(ident, {**absolute(left=0, top=rows_top + index * row_h, width=w, height=row_h), "display": keyword("none")},
                          [mark, level_check(ident, rgb("#B5C784")), title]))
        completed = level_row_bindings(doc, ident, index, (f"{ident}-text",))
        doc.bind(f"{ident}-text.color", f"{ident}-text", "color", [1, 1, 1, {"op": "select", "args": [completed, 0.5, 0.9]}])
    foot = level_time_lines(doc, pw, h, rgb(LEVEL_STATS))
    doc.bind("level-shot.image", "level-shot", "image", {"state": "pause_shot"})
    doc.bind("level-name.text", "level-name", "text", {"state": "pause_level"})
    doc.bind("level-detail.text", "level-detail", "text", {"state": "pause_detail"})
    doc.bind("level-objectives.text", "level-objectives", "text", {"state": "pause_objectives"})
    published = {"op": ">", "args": [{"op": "+", "args": [{"state": "pause_objective_count"}, {"state": "pause_completed_count"}]}, 0]}
    doc.bind("level-objectives.display", "level-objectives", "display", {"op": "select", "args": [published, "none", "block"]})
    # A map without objectives shows no empty heading.
    doc.bind("level-objectives-head.display", "level-objectives-head", "display",
             {"op": "select", "args": [{"op": "||", "args": [published, {"op": "!=", "args": [{"state": "pause_objectives"}, ""]}]},
                                       "block", "none"]})
    return group("level-block", absolute(left=x, top=y, width=w, height=h),
                 [frame, shot, shot_frame, heading, name, detail, objectives_head, objectives, *rows, *foot])


LEVEL_PICTURE_GONE = 1.9   # text size from which the level block's picture has given all its room to the lines


def reflow_level_block(doc: Document, block: dict, strogg: bool = False) -> None:
    """Section 2's text scale in the level block (either family's): the
    header, the level and the objectives heading keep their size in the
    card's slots, while the difficulty, the objectives and the time lines
    grow with the text. Above 100 % the picture gives its room to them,
    shrinking to nothing at 200 % (hidden from 190 %), the lines stand in
    their place under the header, and the time in the mission wraps onto a
    second line over the newest save's age; the card keeps its size."""
    nodes = {node["id"]: node for node in walk_nodes(block)}
    card_h = block["properties"]["height"]["value"]

    def authored(ident: str, key: str) -> float:
        return nodes[ident]["properties"][key]["value"]

    def bind(ident: str, key: str, larger) -> None:
        doc.bind(f"text.{ident}.{key}", ident, key, {"op": "select", "args": [LARGER, larger, authored(ident, key)]})
    text_source(doc)
    for ident in ("level-heading", "level-name", "level-objectives-head"):
        for part in (ident, f"{ident}-rune"):
            if part in nodes:
                fixed_text(doc, nodes[part])
    shot_top = authored("level-shot", "top")
    picture = {"op": "max", "args": [0, text_sum(266, text_times(-133))]}   # 133 dp at 100 %, nothing at 200 %
    hidden = {"op": "select", "args": [{"op": ">", "args": [TEXT, LEVEL_PICTURE_GONE]}, "none", "block"]}
    for ident in ("level-shot", "level-shot-frame"):
        bind(ident, "height", picture)
        nodes[ident]["properties"]["display"] = keyword("block")
        doc.bind(f"text.{ident}.display", ident, "display", hidden)
    # Under the picture (shot_top + its height + 10 dp): the level (fixed),
    # 2 dp, the difficulty, 8 dp, the objectives heading (fixed), 4 dp, then
    # the objectives' rows.
    below = text_sum(shot_top + 10, picture)
    name_h, head_h = authored("level-name", "height"), authored("level-objectives-head", "height")
    detail_h, row_h = authored("level-detail", "height"), authored("level-objective-0", "height")
    detail_top = text_sum(below, name_h + 2)
    head_top = text_sum(below, name_h + 10, text_times(detail_h))
    rows_top = text_sum(below, name_h + 10 + head_h + 4, text_times(detail_h))
    rune_rises = {}
    if strogg:
        rune_rises = {ident: round(authored(ident, "top") - authored(f"{ident}-rune", "top"), 4)
                      for ident in ("level-detail", "level-objective-0-text")}
    bind("level-name", "top", below)
    bind("level-detail", "top", detail_top)
    bind("level-detail", "height", text_times(detail_h))
    bind("level-objectives-head", "top", head_top)
    if strogg:
        bind("level-name-rune", "top", text_sum(below, authored("level-name-rune", "top") - authored("level-name", "top")))
        bind("level-detail-rune", "top", text_sum(detail_top, text_times(-rune_rises["level-detail"])))
        bind("level-detail-rune", "height", text_times(detail_h))
        bind("level-objectives-head-rune", "top",
             text_sum(head_top, authored("level-objectives-head-rune", "top") - authored("level-objectives-head", "top")))
    # The foot: its rule, the time in the mission on two lines and the save's
    # age on one, 10.5 dp of the text size above the card's foot.
    line_h = authored("level-stats", "height")
    rule = text_sum(card_h, text_times(-(10.5 + 3 * line_h + 3)))
    bind("level-stats-rule", "top", rule)
    bind("level-stats", "top", text_sum(card_h, text_times(-(10.5 + 3 * line_h))))
    bind("level-stats", "height", text_times(2 * line_h))
    nodes["level-stats"]["properties"]["white-space"] = keyword("nowrap")
    doc.bind("text.level-stats.white-space", "level-stats", "white-space", {"op": "select", "args": [LARGER, "normal", "nowrap"]})
    bind("level-saved", "top", text_sum(card_h, text_times(-(10.5 + line_h))))
    bind("level-saved", "height", text_times(line_h))
    bind("level-objectives", "top", rows_top)
    bind("level-objectives", "height", {"op": "-", "args": [{"op": "-", "args": [rule, rows_top]}, 3]})
    for index in range(OBJECTIVE_ROWS):
        ident = f"level-objective-{index}"
        bind(ident, "top", text_sum(rows_top, text_times(index * row_h)))
        bind(ident, "height", text_times(row_h))
        bind(f"{ident}-text", "height", text_times(row_h))
        # The marker and the check stay centred on the row's line.
        for mark in (f"{ident}-mark", f"{ident}-check"):
            bind(mark, "top", text_sum(authored(mark, "top") - row_h / 2, text_times(row_h / 2)))
        if strogg:
            bind(f"{ident}-text-rune", "top", text_times(-rune_rises["level-objective-0-text"]))
            bind(f"{ident}-text-rune", "height", text_times(row_h))
    # Generation fails where a shipped language's time lines would not fit:
    # the mission's and the run's time (hours to two digits) in two lines,
    # the save's age in one.
    width = authored("level-stats", "width")
    joined, mission, total = merged_text("#str_231533"), merged_text("#str_231532"), merged_text("#str_231524")
    ages = [merged_text(key) for key in ("#str_230050", "#str_230048", "#str_230049")]
    face = "lowpixel"
    size = nodes["level-stats"]["properties"]["font-size"]["value"]
    for language in LANGUAGES:
        pick = lambda found: found.get(language) or found["english"]
        stats = pick(joined).replace("%s", pick(mission).replace("%s", "10:00:00"), 1).replace("%s", pick(total).replace("%s", "10:00:00"), 1)
        if text_lines(face, stats, 2 * size, width) > 2 or any(text_width(face, pick(age).replace("%d", "59"), 2 * size) > width
                                                               for age in ages):
            raise SystemExit(f"the level block's time lines do not fit at 200 % text in {language}")


# ----------------------------------------------------------- the Strogg pause

# After Kane's stroggification the pause menu takes the Strogg family
# (section 13.7, the hud_strogg row of section 3 and the atlas Strogg pause).
ST_READ = "#FCFFC8"      # R_Strogg labels and readouts
ST_SELECT = "#FFCC00"    # focus, and the rune copies
ST_LINE = "#F59512"      # rails, rim light, circuit traces and markers
ST_FILL = "#FF9000"      # the card header's wash
ST_BAR = "#FF8000"       # the credits scan bar (section 8)
ST_LIGHT = "#6B2A0E"     # the backdrop's additive light band
ST_GRID = "#FFB060"      # the reticle crosses
ST_PLINTH = "#4A2410"
ST_CARD = "#0A0503"
ST_STATS = "#C9A27A"

# The Marine frame with 30-degree shoulders and downward teeth in place of
# the 45-degree notches, at home, as TOP_HOME and BOTTOM_HOME.
STROGG_TOP = [(-300, 92), (-283, 102), (24, 102), (41, 112), (86, 112), (103, 102), (150, 102), (167, 112), (212, 112),
              (229, 102), (308, 102), (327, 91), (334, 91), (352, 66), (520, 66), (537, 76), (578, 76), (595, 66)]
STROGG_BOTTOM = [(-240, 424), (-223, 434), (-26, 434), (-8, 424), (0, 400), (334, 400), (351, 410), (372, 410),
                 (394, 444), (580, 444), (597, 434)]
# The circuit traces etched in the bands, with their junction dots.
STROGG_TRACES_TOP = ([[(-300, 40), (40, 40), (60, 58), (250, 58)], [(380, 24), (520, 24), (538, 40), (700, 40)]],
                     [(40, 40), (250, 58), (520, 24), (700, 40)])
STROGG_TRACES_BOTTOM = ([[(-200, 460), (120, 460), (140, 448), (300, 448)], [(420, 470), (560, 470), (578, 458), (760, 458)]],
                        [(120, 460), (300, 448), (560, 470)])

# Every Strogg label exists twice on one baseline, in runes and in R_Strogg,
# so it can translate (section 8; section 14.6 gives the runes 1.2 times the
# R_Strogg cap height). Both faces are scaled by their whole height, ascent
# plus descent: the shipped R_Strogg face puts its caps at 0.519 of the size
# and its baseline 0.800 down; the rune face 0.765 and 0.988.
RSTROGG_CAP, RSTROGG_ASCENT = 1329 / 2560, 2048 / 2560
RUNE_CAP, RUNE_ASCENT = 1427 / 1866, 1843 / 1866
RUNE_SCALE = 1.2 * RSTROGG_CAP / RUNE_CAP
SCAN_LENGTH = 150 * U     # the credits scan bar: 150 u, its head about 12 u past the line


def rune_rise(size: float) -> float:
    """How far the rune copy's box rises so the baselines meet: a line box
    puts each face's baseline half its leading plus its ascent down."""
    return size * RUNE_SCALE * (RUNE_ASCENT - 0.5) - size * (RSTROGG_ASCENT - 0.5)


def circle_commands(prefix: str, cx, cy, r: float) -> list:
    """A circle as four cubic quarter arcs, centered on canvas coordinates."""
    k = 0.5523 * r
    def at(dx, dy):
        return point({**cx, "dp": round(cx["dp"] + dx, 3)}, round(cy + dy, 3))
    return [{"id": f"{prefix}m", "op": "move", "points": [at(r, 0)]},
            {"id": f"{prefix}a", "op": "cubic", "points": [at(r, k), at(k, r), at(0, r)]},
            {"id": f"{prefix}b", "op": "cubic", "points": [at(-k, r), at(-r, k), at(-r, 0)]},
            {"id": f"{prefix}c", "op": "cubic", "points": [at(-r, -k), at(-k, -r), at(0, -r)]},
            {"id": f"{prefix}d", "op": "cubic", "points": [at(k, -r), at(r, -k), at(r, 0)]},
            {"id": f"{prefix}z", "op": "close"}]


def circuit_traces(traces: tuple) -> list:
    """The Strogg bands' etched circuit: #F59512 at 0.32, 0.8 u lines with
    1.6 u junction dots (the atlas)."""
    lines, dots = traces
    paint = rgb(ST_LINE, 0.32)
    result = [path(f"trace-{index}", [(vx(x), U * y) for x, y in points], closed=False,
                   stroke=stroke(solid(paint), U * 0.8, minimum=1)) for index, points in enumerate(lines)]
    commands = []
    for index, (x, y) in enumerate(dots):
        commands += circle_commands(f"d{index}", vx(x), U * y, U * 1.6)
    result.append({"id": "trace-dots", "commands": commands, "fill": solid(paint)})
    return result


STROGG_BANDS = {
    "top": STROGG_TOP, "top_ends": (92, 66), "bottom": STROGG_BOTTOM, "bottom_ends": (424, 434),
    # The atlas draws the Strogg rim as a thinner, brighter line than the
    # Marine one: #F59512, held close to the edge.
    "tint": ST_LINE, "peak": 0.34, "reach": 11.0,
    "etched_top": circuit_traces(STROGG_TRACES_TOP), "etched_bottom": circuit_traces(STROGG_TRACES_BOTTOM),
}


def scan_bar(ident: str, height: float, props: dict) -> dict:
    """The credits scan bar (section 8): an additive #FF8000 ramp that
    brightens linearly to a rounded head, with soft top and bottom edges.
    Its head is the node's trailing edge; it reaches SCAN_LENGTH back. The
    bar slides by its right inset, a percentage of the label's wrapper: from
    100% (the head at the wrapper's leading edge, where the bar starts in the
    atlas) to 0% (12 u past the label), so it sweeps any label whole."""
    def capsule(name: str, half: float, k: float, alphas: list) -> dict:
        a, c = half * k, 0.5523
        head, tail = {"fraction": 1}, {"fraction": 1, "dp": -SCAN_LENGTH}
        def x(dp):
            return {"fraction": 1, "dp": round(dp, 3)}
        def y(dp):
            return {"fraction": 0.5, "dp": round(dp, 3)}
        commands = [{"id": "m", "op": "move", "points": [[tail, y(-half)]]},
                    {"id": "top", "op": "line", "points": [[x(-a), y(-half)]]},
                    {"id": "h1", "op": "cubic", "points": [[x(-a + a * c), y(-half)], [head, y(-half * c)], [head, y(0)]]},
                    {"id": "h2", "op": "cubic", "points": [[head, y(half * c)], [x(-a + a * c), y(half)], [x(-a), y(half)]]},
                    {"id": "bottom", "op": "line", "points": [[tail, y(half)]]},
                    {"id": "close", "op": "close"}]
        stops = [(at, rgb(ST_BAR, round(alpha, 4))) for at, alpha in alphas]
        return {"id": name, "commands": commands, "fill": linear((tail, 0), (head, 0), stops), "blend": "additive"}
    ramp = [(0, 0), (0.25, 0.24), (0.5, 0.55), (0.75, 0.84), (0.9, 0.96), (0.955, 1), (1, 0.7)]
    soft = capsule("edge", height / 2, 0.6, [(at, alpha * 0.55) for at, alpha in ramp])
    core = capsule("core", height * 0.29, 0.9, ramp)
    return vector(ident, {**props, "right": length(0, "%"), "opacity": number(0)}, [soft, core])


class Translation:
    """The short credits translation of the Strogg pause (sections 8 and
    13.7): every line arrives in runes at 0.80, then cross-fades into
    R_Strogg while both copies pop 6% larger and settle. Headings and names
    arrive white and settle to their color 60 ms later over 150 ms. A line
    whose base is a control's label fades through its wrapper's opacity, so
    the control's own feedback keeps the label's color."""
    POP = 1.06

    def __init__(self):
        self.lines: list[tuple] = []
        self.bars: list[tuple] = []

    def add(self, prefix: str, delay: float, duration: float, *, rest: list[float] | None = None,
            white: bool = False, width: float | None = None, runes: bool = True) -> None:
        """`rest` is the label's color for a color fade; without it the
        `<prefix>-latin` wrapper fades. `width` keeps a fixed box's leading
        edge in place as it pops. A line without `runes` has no rune copy
        and only fades in."""
        self.lines.append((prefix, delay, duration, rest, white, width, runes))

    def bar(self, ident: str, delay: float, slide: float) -> None:
        self.bars.append((ident, delay, slide))

    def reset_tracks(self) -> list:
        """Tracks that put every line back in runes at once: the rune copy at
        0.80, the R_Strogg label or its wrapper hidden."""
        runes = [*rgb(ST_SELECT)[:3], 0.8]
        result = []
        for prefix, _, _, rest, white, _, has_runes in self.lines:
            if has_runes:
                result.append(track(f"{prefix}-rune", "color", [(0, colour(runes)), (1, colour(runes))]))
            if rest is None:
                result.append(track(f"{prefix}-latin", "opacity", [(0, number(0)), (1, number(0))]))
            else:
                hidden = [1, 1, 1, 0] if white else [*rest[:3], 0]
                result.append(track(prefix, "color", [(0, colour(hidden)), (1, colour(hidden))]))
        return result

    @staticmethod
    def pop(scale: float, width: float | None) -> dict:
        return transform(tx=(scale - 1) * width / 2 if width else 0, sx=scale, sy=scale)

    def tracks(self, offset: float) -> tuple:
        """The tracks with every line offset by `offset` ms, and their end."""
        result, end = [], 0.0

        def keys(start, value_from, value_to, length):
            # A play retargets the first key to the current value; the 1 ms
            # key puts the line back in runes before its own start.
            steps = [(0, value_from), (1, value_from)]
            if start > 1:
                steps.append((start, value_from))
            steps.append((max(start, 1) + length, value_to))
            return steps

        for prefix, delay, duration, rest, white, width, has_runes in self.lines:
            start = offset + delay
            hidden = [*rgb(ST_SELECT)[:3], 0]
            runes = [*rgb(ST_SELECT)[:3], 0.8]
            if has_runes:
                result.append(track(f"{prefix}-rune", "color", [(at, colour(value)) for at, value in
                                                                 keys(start, runes, hidden, duration)]))
                result.append(track(f"{prefix}-rune", "transform", [(at, value) for at, value in
                                                                     keys(start, self.pop(self.POP, width), transform(), duration)]))
            latin = f"{prefix}-latin" if rest is None else prefix
            if has_runes:
                pop = self.pop(1 + (self.POP - 1) * 0.9, width)
                result.append(track(latin, "transform", [(at, value) for at, value in keys(start, pop, transform(), duration)]))
            finish = max(start, 1) + duration
            if rest is None:
                result.append(track(latin, "opacity", [(at, number(value)) for at, value in keys(start, 0, 1, duration)]))
            elif white:
                steps = [(at, colour(value)) for at, value in keys(start, [1, 1, 1, 0], [1, 1, 1, 1], duration)]
                steps += [(finish + 60, colour([1, 1, 1, 1])), (finish + 210, colour(rest))]
                result.append(track(latin, "color", steps))
                finish += 210
            else:
                result.append(track(latin, "color", [(at, colour(value)) for at, value in
                                                     keys(start, [*rest[:3], 0], rest, duration)]))
            end = max(end, finish)
        for ident, delay, slide in self.bars:
            start = offset + delay
            # The bar slides in from the leading edge over `slide` ms,
            # accel(0.4, 0.6), and dims to nothing over 3.33 times as long
            # (section 8).
            dim = round(slide * 10 / 3)
            parked, rested = length(100, "%"), length(0, "%")
            result.append(track(ident, "right", [(0, parked), (1, parked), *([(start, parked, ACCEL_LATE)] if start > 1 else []),
                                                 (max(start, 1) + slide, rested)]))
            # It lights only as it starts to slide, never while parked.
            assert start > 1
            result.append(track(ident, "opacity", [(0, number(0)), (1, number(0)), (start, number(0)), (start + 1, number(1)),
                                                   (start + dim, number(0))]))
            end = max(end, max(start, 1) + dim)
        return result, end

    def finish(self, doc: Document) -> None:
        """`translate` runs with `open`; `translateReturn` waits the 500 ms a
        page's return takes before the home content shows (section 8). The
        events replace the timelines of the same names, so the session's
        open and returnHome play both. Neither is essential: reduced motion
        shows the translated labels at once."""
        for ident, offset in (("translate", 0), ("translateReturn", 500)):
            tracks, end = self.tracks(offset)
            doc.timelines.add(ident, end, tracks)
        doc.events["open"] = [{"op": "playTimeline", "timeline": "open"}, {"op": "playTimeline", "timeline": "translate"}]
        doc.events["returnHome"] = [{"op": "playTimeline", "timeline": "returnHome"},
                                    {"op": "playTimeline", "timeline": "translateReturn"}]


# accel(120, 180) of the credits scan bar: accelerating over 40% of the slide.
ACCEL_LATE = [0.4, 0.0, 0.45, 1.0]


def strogg_text(ident: str, key: str, box: dict, size: float, line: float, tint: list[float], **extra) -> list:
    """An R_Strogg label and its rune copy on the same baseline. Both rest
    translated: the rune copy transparent, the label in `tint`; the
    translation's 1 ms keys put them back in runes when it plays."""
    latin = label(ident, key, {**box, **typeface("r_strogg", size, line, tint), "white-space": keyword("nowrap"),
                               "transform": transform(), **extra})
    rune_box = dict(box)
    rune_box["top"] = length(box["top"]["value"] - rune_rise(size))
    rune = label(f"{ident}-rune", key, {**rune_box, **typeface("strogg", round(size * RUNE_SCALE, 3), line, [*rgb(ST_SELECT)[:3], 0]),
                                        "white-space": keyword("nowrap"), "transform": transform(), "pointer-events": keyword("none"), **extra})
    return [rune, latin]


def strogg_navigation_plate(doc: Document, ident: str, key: str, translation: Translation, index: int, *,
                            action: str | None = None, event: str | None = None) -> dict:
    """A Strogg navigation plate (section 13.7 and the atlas): the Marine
    plate's bleed off the leading edge, ending in a 30-degree shoulder at
    357 u, the end of its target. The #F59512 rail runs along its foot, up
    its trailing edge and back over the shoulder; the slanted marker turns
    #FFCC00 with the label on focus, and the focused plate runs the credits
    scan bar under its label. The label arrives in runes and translates
    120 + 45 ms per row after the menu opens, over 200 ms."""
    lead = NAV_LEAD
    width = lead + 357 * U
    x0 = lead
    top, bottom = U * 2.8, U * 27.2
    end, shoulder, foot = x0 + 357 * U, x0 + 341 * U, U * 12
    fade = [(0, [0, 0, 0, 1]), (0.44, [0, 0, 0, 1]), (0.62, [0, 0, 0, 0.5]), (0.88, [0, 0, 0, 0]), (1, [0, 0, 0, 0])]
    rail = [(0, rgb(ST_LINE, 0.9)), (0.55, rgb(ST_LINE, 0.9)), (1, rgb(ST_LINE, 0.45))]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0.4)}, [
        path("fill", [(0, top), (shoulder, top), (end, foot), (end, bottom), (0, bottom)],
             fill=linear((x0, 0), (end, 0), fade)),
        path("rail", [(0, bottom - 0.75), (end - 0.75, bottom - 0.75), (end - 0.75, foot), (shoulder, top + 0.75),
                      (x0 + 285 * U, top + 0.75)], closed=False, stroke=stroke(linear((x0, 0), (end, 0), rail), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)}, [
        path("inset", [(0, bottom - 3.2), (x0 + 348, bottom - 3.2)], closed=False, stroke=stroke(solid(rgb(ST_SELECT)), 1.2))])

    def slanted(tint: list[float]) -> dict:
        # A parallelogram 6 u wide, slanted as the atlas draws it.
        mx, my = x0 + U * 36.4, U * 11.8
        return path("mark", [(mx, my), (mx + U * 6, my), (mx + U * 4.4, my + U * 6), (mx - U * 1.6, my + U * 6)], fill=solid(tint))
    marker_rest = vector(f"{ident}-marker", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0.4)},
                         [slanted(rgb(ST_LINE))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=0, top=0, width=width, height=45), "opacity": number(0)},
                        [slanted(rgb(ST_SELECT))])
    # The label's wrapper fits the label, so the scan bar's head can rest
    # 12 u past it whatever the language; it starts at 30 u, as the bar does.
    size = 24.0
    line_height = bottom - top
    text_node = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                                              **typeface("r_strogg", size, line_height, rgb(ST_READ, 0.8)),
                                              "white-space": keyword("nowrap"), "transform": transform()})
    latin = group(f"{ident}-latin", {"position": keyword("relative"), "display": keyword("block"), "margin-left": length(U * 14),
                                     "margin-right": length(U * 12), "opacity": number(1), "transform": transform()}, [text_node])
    rune = label(f"{ident}-rune", key, {**absolute(left=U * 14, top=-rune_rise(size)),
                                        **typeface("strogg", round(size * RUNE_SCALE, 3), line_height, [*rgb(ST_SELECT)[:3], 0]),
                                        "white-space": keyword("nowrap"), "transform": transform(), "pointer-events": keyword("none")})
    bar = scan_bar(f"{ident}-scan", U * 15, {**absolute(left=0, top=0, height=line_height), "right": length(0),
                                             "pointer-events": keyword("none")})
    fit = group(f"{ident}-fit", {**absolute(left=x0 + U * 30, top=top, height=line_height)}, [bar, rune, latin])
    fixed_text(doc, fit)   # the label and its runes keep their size on the plate
    translation.add(ident, 120 + 45 * index, 200)
    slide = 300
    bar_rest, bar_start = length(0, "%"), length(100, "%")
    # Focus (and hover) slide the bar in from the leading edge over 300 ms,
    # accel(0.4, 0.6), and dim it over 1000 ms; any other state lets it go
    # over its own duration.
    run_bar = [(f"{ident}-scan", "right", [(0, bar_start), (1, bar_start, ACCEL_LATE), (1 + slide, bar_rest), (1000, bar_rest)]),
               (f"{ident}-scan", "opacity", [(0, number(1)), (1, number(1)), (1000, number(0))])]
    def settle(duration: float, alpha: float) -> list:
        return [(f"{ident}-scan", "right", [(0, bar_rest), (duration, bar_rest)]),
                (f"{ident}-scan", "opacity", [(0, number(alpha)), (duration, number(alpha))])]
    ids = doc.states(ident, {
        "default": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                    (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-focus", "opacity", number(0)),
                    (f"{ident}-label", "color", colour(rgb(ST_READ, 0.8))), (f"{ident}-label", "transform", transform())],
        "hover": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)),
                  (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(0)),
                  (f"{ident}-label", "color", colour(rgb(ST_SELECT))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "focus": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", number(0)),
                  (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                  (f"{ident}-label", "color", colour(rgb(ST_SELECT))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "pressed": [(f"{ident}-plate", "opacity", number(1)), (f"{ident}-marker", "opacity", number(0)),
                    (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-focus", "opacity", number(1)),
                    (f"{ident}-label", "color", colour(rgb(ST_SELECT))), (f"{ident}-label", "transform", transform(tx=1, ty=1, sx=1.03, sy=1.03))],
        "disabled": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                     (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-focus", "opacity", number(0)),
                     (f"{ident}-label", "color", colour(rgb(ST_READ, 0.4))), (f"{ident}-label", "transform", transform())],
    })
    extra = {"default": settle(300, 0), "hover": run_bar, "focus": run_bar, "pressed": settle(60, 1), "disabled": settle(150, 0)}
    for timeline in doc.timelines.items:
        state = timeline["id"][len(ident) + 1:]
        if timeline["id"].startswith(ident + ".") and state in extra:
            for node, prop, keys in extra[state]:
                timeline["tracks"].append(track(node, prop, keys))
            timeline["durationMs"] = max(timeline["durationMs"], max(key[0] for _, _, keys in extra[state] for key in keys))
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, {"position": keyword("relative"), "display": keyword("block"),
                         "margin-left": length(-lead), "width": length(width), "height": length(30 * U)},
                 [plate, focus, marker_rest, marker_hot, fit], control=control)


def strogg_link(doc: Document, ident: str, key: str, translation: Translation, delay: float, *, event: str) -> dict:
    """EXIT on the Strogg plinth: R_Strogg 16 dp at 0.50 behind the slanted
    marker; hover and focus turn both #FFCC00. It arrives in runes and
    translates at `delay` over 200 ms, after the navigation."""
    def slanted(name: str, tint: list[float], alpha: float) -> dict:
        return vector(name, {**absolute(left=0, top=0, width=U * 7, height=U * 17), "opacity": number(alpha)}, [
            path("mark", [(U * 1.2, U * 7.25), (U * 5.7, U * 7.25), (U * 4.5, U * 11.75), (0, U * 11.75)], fill=solid(tint))])
    marker, hot = slanted(f"{ident}-marker", rgb(ST_LINE), 0.4), slanted(f"{ident}-marker-hot", rgb(ST_SELECT), 0)
    text_node = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
        **typeface("r_strogg", 16, U * 17, rgb(ST_READ, 0.5)), "white-space": keyword("nowrap")})
    latin = group(f"{ident}-latin", {"position": keyword("relative"), "display": keyword("block"), "margin-left": length(U * 7),
                                     "opacity": number(1), "transform": transform()}, [text_node])
    rune = label(f"{ident}-rune", key, {**absolute(left=U * 7, top=-rune_rise(16)),
                                        **typeface("strogg", round(16 * RUNE_SCALE, 3), U * 17, [*rgb(ST_SELECT)[:3], 0]),
                                        "white-space": keyword("nowrap"), "transform": transform(), "pointer-events": keyword("none")})
    translation.add(ident, delay, 200)
    fixed_text(doc, rune)        # EXIT and its runes keep their size on the plinth
    fixed_text(doc, text_node)
    rest, lit = colour(rgb(ST_READ, 0.5)), colour(rgb(ST_SELECT))
    ids = doc.states(ident, {
        "default": [(f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)), (f"{ident}-label", "color", rest)],
        "hover": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-label", "color", lit)],
        "focus": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)), (f"{ident}-label", "color", lit)],
        "pressed": [(f"{ident}-marker", "opacity", number(0)), (f"{ident}-marker-hot", "opacity", number(1)),
                    (f"{ident}-label", "color", colour(rgb(ST_READ)))],
        "disabled": [(f"{ident}-marker", "opacity", number(0.4)), (f"{ident}-marker-hot", "opacity", number(0)),
                     (f"{ident}-label", "color", colour(rgb(ST_READ, 0.25)))],
    })
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "height": length(U * 17),
                         "margin-right": length(U * 14)}, [marker, hot, rune, latin],
                 control={"role": "button", "label": key, "states": ids, "event": event})


def strogg_plinth(links: list) -> dict:
    """The plinth in #4A2410 at 0.30 with the Strogg shoulder at its
    trailing end and a steeper leading cut (the atlas)."""
    left, right, top, bottom = 24.0, 361.2, 403.3, 422.1
    width, height = U * (right - left), U * (bottom - top)
    shape = path("plinth", [(0, 0), (width - U * 10.7, 0), (width, U * 12.5), (width, height), (U * 7.2, height), (0, height - U * 6.3)],
                 fill=solid(rgb(ST_PLINTH, 0.3)))
    row = group("plinth-links", {**absolute(left=U * (32 - left), top=U * -0.3, height=U * 17),
                                  "display": keyword("flex"), "flex-direction": keyword("row"),
                                  "align-items": keyword("flex-start")}, links)
    return vector("plinth", {**absolute(left=U * left, top=U * top, width=width, height=height), "opacity": number(1)}, [shape], [row])


STROGG_SHOULDER = 1.732   # a 30-degree shoulder runs this far along per unit it falls


def strogg_modal_silhouette(width: float, height: float) -> list:
    """modal_silhouette in the Strogg family (section 13.7): the slot keeps
    its 17 dp depth but its flanks fall at 30 degrees, and the lower-leading
    chamfer becomes a 30-degree shoulder over the same 38 dp run. The
    silhouette takes the Strogg level card's fill and its #F59512 rail."""
    inset, raised, bottom = width * 7 / 512, 3.5625, height - 5.34375
    slot_lead, slot_trail = width * 14 / 512, width * 388 / 512
    depth = 17.0
    flank = round(depth * STROGG_SHOULDER, 4)
    run, rise = 38.0, round(38.0 / STROGG_SHOULDER, 4)
    right = width - inset
    outline = [(inset, raised), (slot_lead, raised), (slot_lead + flank, raised + depth), (slot_trail - flank, raised + depth),
               (slot_trail, raised), (right, raised), (right, bottom), (inset + run, bottom), (inset, bottom - rise)]
    return [path("frame", outline, fill=solid(rgb(ST_CARD, 0.85))),
            path("rail", outline, stroke=stroke(solid(rgb(ST_LINE, 0.75)), 1.5))]


def strogg_action_plate(doc: Document, ident: str, key: str, left: float, top: float, *, action: str | None = None,
                        event: str | None = None, width: float = 180) -> dict:
    """action_plate in the Strogg family (section 13.7): the 35 dp band in its
    45 dp target keeps its 20 dp run, but the lower-leading cut becomes a
    30-degree shoulder; the fill is the Strogg plates' black fade and the rail
    #F59512. The slanted marker and the R_Strogg label turn #FFCC00 on focus.
    The label rests translated over its rune copy; the confirmation's
    translation plays them, so the control's states keep the label's color."""
    band_top, band_bottom = 5.0, 40.0
    run, rise = 20.0, round(20.0 / STROGG_SHOULDER, 4)
    fade = [(0, [0, 0, 0, 1]), (0.55, [0, 0, 0, 1]), (0.8, [0, 0, 0, 0.5]), (1, [0, 0, 0, 0])]
    rail = [(0, rgb(ST_LINE, 0.9)), (0.55, rgb(ST_LINE, 0.9)), (1, rgb(ST_LINE, 0.2))]
    whole = absolute(left=0, top=0, width=width, height=45)
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.4)}, [
        path("fill", [(0, band_top), (width, band_top), (width, band_bottom), (run, band_bottom), (0, band_bottom - rise)],
             fill=linear((0, 0), (width, 0), fade)),
        path("rail", [(0.75, band_top), (0.75, band_bottom - rise), (run, band_bottom - 0.75), (width, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (width, 0), rail), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**whole, "opacity": number(0)}, [
        path("inset", [(run + 2, band_bottom - 3.5), (width - 20, band_bottom - 3.5)], closed=False,
             stroke=stroke(solid(rgb(ST_SELECT)), 1.2))])

    def slanted(tint: list[float]) -> dict:
        lean = round(8 * 0.267, 4)
        return path("mark", [(lean, 0), (8, 0), (8 - lean, 8), (0, 8)], fill=solid(tint))
    marker_rest = vector(f"{ident}-marker", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(0.4)}, [slanted(rgb(ST_LINE))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=14, top=14, width=8, height=8), "opacity": number(0)},
                        [slanted(rgb(ST_SELECT))])
    size = 20.0
    read, hot = rgb(ST_READ, 0.8), rgb(ST_SELECT)
    text_node = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                                              **typeface("r_strogg", size, 45, read), "white-space": keyword("nowrap"),
                                              "transform": transform()})
    latin = group(f"{ident}-latin", {"position": keyword("relative"), "display": keyword("block"), "opacity": number(1),
                                     "transform": transform()}, [text_node])
    rune = label(f"{ident}-rune", key, {**absolute(left=0, top=round(-rune_rise(size), 3)),
                                        **typeface("strogg", round(size * RUNE_SCALE, 3), 45, [*rgb(ST_SELECT)[:3], 0]),
                                        "white-space": keyword("nowrap"), "transform": transform(), "pointer-events": keyword("none")})
    fit = group(f"{ident}-fit", absolute(left=26, top=0, width=width - 30, height=45), [rune, latin])
    def state(plate_alpha, marker, marker_lit, focus_alpha, tint, moved):
        return [(f"{ident}-plate", "opacity", number(plate_alpha)), (f"{ident}-marker", "opacity", number(marker)),
                (f"{ident}-marker-hot", "opacity", number(marker_lit)), (f"{ident}-focus", "opacity", number(focus_alpha)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved)]
    ids = doc.states(ident, {
        "default": state(0.4, 0.4, 0, 0, read, transform()),
        "hover": state(0.8, 0, 1, 0, hot, transform(sx=1.03, sy=1.03)),
        "focus": state(0.8, 0, 1, 1, hot, transform(sx=1.03, sy=1.03)),
        "pressed": state(1, 0, 1, 1, hot, transform(tx=1, ty=1, sx=1.03, sy=1.03)),
        "disabled": state(0.4, 0.4, 0, 0, rgb(ST_READ, 0.4), transform()),
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, absolute(left=left, top=top, width=width, height=45), [plate, focus, marker_rest, marker_hot, fit], control=control)


def strogg_level_block(doc: Document, translation: Translation, start: float) -> dict:
    """The Strogg level block (section 13.7): the Marine block's content in a
    chamfered card (10 u at every corner, #0A0503 at 0.94 in a #F59512 rail
    at 0.75) under the stock HUD grain, the header washed #FF9000, the
    levelshot chamfered in an orange frame, and every line in R_Strogg,
    translating from runes after the actions from `start` ms."""
    x, y, w, h = U * 374, U * 118, U * 262, U * 270
    cut, header = U * 10, 40.0
    card = [(cut, 0), (w - cut, 0), (w, cut), (w, h - cut), (w - cut, h), (cut, h), (0, h - cut), (0, cut)]
    frame = vector("level-card", absolute(left=0, top=0, width=w, height=h), [
        path("body", card, fill=solid(rgb(ST_CARD, 0.94))),
        path("wash", [(cut + 0.75, 0.75), (w - cut - 0.75, 0.75), (w - 0.75, cut + 0.75), (w - 0.75, header), (0.75, header), (0.75, cut + 0.75)],
             fill=linear((0, 0), (w, 0), [(0, rgb(ST_FILL, 0.16)), (0.5, rgb(ST_FILL, 0.1)), (1, [1, 1, 1, 0])])),
        path("rule", [(0, header), (w, header)], closed=False, stroke=stroke(solid(rgb(ST_LINE, 0.5)), 1)),
        path("rail", card, stroke=stroke(solid(rgb(ST_LINE, 0.75)), 1.5)),
        path("mark", [(U * 12, U * 9.8), (U * 18, U * 9.8), (U * 16.4, U * 16.2), (U * 10.4, U * 16.2)], fill=solid(rgb(ST_LINE))),
    ])
    # The masked grain: the stock Strogg HUD static (s_static), added warm and
    # faint in tiles of its own 128 texels, inside the chamfers.
    inset, tile = cut / 2, 128.0
    tiles = []
    columns, rows_needed = math.ceil((w - 2 * inset) / tile), math.ceil((h - 2 * inset) / tile)
    for row in range(rows_needed):
        for column in range(columns):
            tiles.append(picture(f"level-grain-{row}-{column}", "gfx/guis/hud/s_static",
                                 {**absolute(left=column * tile, top=row * tile, width=tile, height=tile),
                                  "image-blend": keyword("additive"), "image-color": colour([1, 0.75, 0.4, 0.14])}, fit="fill"))
    grain = group("level-grain", {**absolute(left=inset, top=inset, width=w - 2 * inset, height=h - 2 * inset),
                                  "overflow": keyword("hidden"), "pointer-events": keyword("none")}, tiles)
    pw, ph = w - 30, U * LEVEL_SHOT
    px, py, chamfer = 15, header + 12, U * 6
    shot = picture("level-shot", "", {**absolute(left=px, top=py, width=pw, height=ph)})
    corners = vector("level-shot-frame", absolute(left=px, top=py, width=pw, height=ph), [
        path("lead", [(0, 0), (chamfer, 0), (0, chamfer)], fill=solid(rgb(ST_CARD))),
        path("trail", [({"fraction": 1}, {"fraction": 1, "dp": -chamfer}), ({"fraction": 1}, {"fraction": 1}),
                       ({"fraction": 1, "dp": -chamfer}, {"fraction": 1})], fill=solid(rgb(ST_CARD))),
        path("frame", [(chamfer, 0.5), ({"fraction": 1, "dp": -0.5}, 0.5), ({"fraction": 1, "dp": -0.5}, {"fraction": 1, "dp": -chamfer}),
                       ({"fraction": 1, "dp": -chamfer}, {"fraction": 1, "dp": -0.5}), (0.5, {"fraction": 1, "dp": -0.5}), (0.5, chamfer)],
             stroke=stroke(solid(rgb(ST_LINE, 0.6)), 1.5)),
    ])
    lines = []
    def line(ident: str, key: str, box: dict, size: float, height_dp: float, tint: list[float], delay: float, duration: float,
             white: bool = False, **extra) -> None:
        lines.extend(strogg_text(ident, key, {**box, "height": length(height_dp)}, size, height_dp, tint, **extra))
        translation.add(ident, start + delay, duration, rest=tint, white=white, width=box["width"]["value"])
    line("level-heading", "#str_230030", absolute(left=U * 24, top=0, width=w - U * 34), 15, header, rgb(ST_READ), 0, 300, white=True)
    below = header + 12 + ph + 10
    line("level-name", "#str_230030", absolute(left=15, top=below, width=pw), 21, 24, rgb(ST_READ), 80, 400, white=True)
    line("level-detail", "#str_230030", absolute(left=15, top=below + 26, width=pw), 17, 20, rgb(ST_SELECT), 140, 200)
    line("level-objectives-head", "#str_200291", absolute(left=15, top=below + 54, width=pw), 14.5, 18, rgb(ST_SELECT), 180, 200,
         display=keyword("block"))
    rows_top, row_h, stats_rule = below + 76, U * 12.5, h - U * LEVEL_FOOT
    # A map's own objectives summary wraps, so it fades in with its heading
    # rather than translating.
    objectives = label("level-objectives", "#str_230030", {**absolute(left=15, top=rows_top, width=pw, height=stats_rule - rows_top - 3),
        **typeface("r_strogg", 14, 18, rgb(ST_READ, 0.85)), "overflow": keyword("hidden"), "display": keyword("block")})
    translation.add("level-objectives", start + 180, 200, rest=rgb(ST_READ, 0.85), runes=False)
    rows = []
    for index in range(OBJECTIVE_ROWS):
        ident = f"level-objective-{index}"
        mark = vector(f"{ident}-mark", {**absolute(left=15, top=4, width=9, height=9), "display": keyword("block")}, [
            path("mark", [(1.95, 0), (9, 0), (7.05, 7.5), (0, 7.5)], fill=solid(rgb(ST_LINE, 0.6)))])
        text_parts = strogg_text(f"{ident}-text", "#str_230030", absolute(left=33, top=0, width=pw - 18, height=row_h),
                                 16, row_h, rgb(ST_READ), overflow=keyword("hidden"))
        translation.add(f"{ident}-text", start + 220 + 60 * index, 200, rest=rgb(ST_READ), width=pw - 18)
        # The translation owns the text's color, so a completed row dims as
        # a whole, to the atlas's 0.55.
        rows.append(group(ident, {**absolute(left=0, top=rows_top + index * row_h, width=w, height=row_h), "display": keyword("none"),
                                  "opacity": number(1)}, [mark, level_check(ident, rgb(ST_SELECT)), *text_parts]))
        completed = level_row_bindings(doc, ident, index, (f"{ident}-text", f"{ident}-text-rune"))
        doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [completed, 0.55, 1]})
    foot = level_time_lines(doc, pw, h, rgb(ST_STATS))
    doc.bind("level-shot.image", "level-shot", "image", {"state": "pause_shot"})
    for ident, value in (("level-name", "pause_level"), ("level-detail", "pause_detail")):
        doc.bind(f"{ident}.text", ident, "text", {"state": value})
        doc.bind(f"{ident}-rune.text", f"{ident}-rune", "text", {"state": value})
    doc.bind("level-objectives.text", "level-objectives", "text", {"state": "pause_objectives"})
    published = {"op": ">", "args": [{"op": "+", "args": [{"state": "pause_objective_count"}, {"state": "pause_completed_count"}]}, 0]}
    doc.bind("level-objectives.display", "level-objectives", "display", {"op": "select", "args": [published, "none", "block"]})
    shown = {"op": "select", "args": [{"op": "||", "args": [published, {"op": "!=", "args": [{"state": "pause_objectives"}, ""]}]},
                                      "block", "none"]}
    doc.bind("level-objectives-head.display", "level-objectives-head", "display", shown)
    doc.bind("level-objectives-head-rune.display", "level-objectives-head-rune", "display", shown)
    return group("level-block", absolute(left=x, top=y, width=w, height=h),
                 [frame, grain, shot, corners, *lines, objectives, *rows, *foot])


def strogg_title(translation: Translation) -> dict:
    """GAME PAUSED in R_Strogg #FCFFC8, translating from runes at 60 ms over
    500 ms under the scan bar, arriving white (section 13.7)."""
    size, height = 18.5, U * 24
    rest = rgb(ST_READ)
    text_node = label("paused-title", "#str_230031", {"position": keyword("relative"), "display": keyword("block"),
        **typeface("r_strogg", size, height, rest), "white-space": keyword("nowrap"), "transform": transform()})
    latin = group("paused-title-latin", {"position": keyword("relative"), "display": keyword("block"), "margin-left": length(U * 14),
                                         "margin-right": length(U * 12)}, [text_node])
    rune = label("paused-title-rune", "#str_230031", {**absolute(left=U * 14, top=-rune_rise(size)),
        **typeface("strogg", round(size * RUNE_SCALE, 3), height, [*rgb(ST_SELECT)[:3], 0]), "white-space": keyword("nowrap"),
        "transform": transform(), "pointer-events": keyword("none")})
    bar = scan_bar("paused-title-scan", U * 16, {**absolute(left=0, top=0, height=height), "right": length(0),
                                                  "pointer-events": keyword("none")})
    translation.add("paused-title", 60, 500, rest=rest, white=True)
    translation.bar("paused-title-scan", 60, 150)
    return group("paused-title-fit", {**absolute(left=U * 30, top=U * 134, height=height), "pointer-events": keyword("none")},
                 [bar, rune, latin])


# ------------------------------------------------------- the objectives page

# The pause menu's OBJECTIVES (section 13.7) opens the objectives display of
# section 14.11 in its Remastered form as a page of the pause: every open
# objective, newest first, and the ones completed on this map below them.
OBJECTIVE_PAGE_ROWS = 8      # open objectives the page lists (Session.cpp pageRows)
COMPLETED_ROWS = 8           # completed ones, newest first
OBJ_FRAME = "#B0CD6B"        # frames and markers (section 3, wristcomm)
OBJ_TEXT = "#D0DEB6"         # descriptions
OBJ_SERIAL = "#D9E7BF"
ST_OBJ_FRAME, ST_OBJ_MARK = "#FF9000", "#FF9900"   # the Strogg display (section 14.11)
OBJ_TOP, OBJ_BOTTOM = 62.0, 428.0    # u: the list, under the docked top band, over the docked bottom band
OBJ_LEFT, OBJ_WIDTH = 14.0, 600.0    # u: the stock plates are 600 u wide


def objective_frame(ident: str, tint: str) -> dict:
    """The objective plate (section 14.11): an open plate whose foot strip is
    cut at the lower leading corner through the whole foot, its fill
    dissolving toward the trailing side (full to 29%, half at 40%, 0.05 at
    51%, gone at 56%) and its rail along the leading edge, the cut and the
    bottom. Frames rest at 0.40, baked into the paint, so no layer is held
    for them. It spans its entry, so it grows with the entry's text."""
    cut = U * 20 * 0.9
    outline = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (cut, {"fraction": 1}), (0, {"fraction": 1, "dp": -cut})]
    def ramp(peak: float) -> list:
        return [(0, rgb(tint, round(peak * 0.4, 4))), (0.29, rgb(tint, round(peak * 0.4, 4))), (0.40, rgb(tint, round(peak * 0.2, 4))),
                (0.51, rgb(tint, round(peak * 0.04, 4))), (0.56, rgb(tint, 0)), (1, rgb(tint, 0))]
    return vector(ident, {**absolute(left=0, top=0), "right": length(0), "bottom": length(0), "pointer-events": keyword("none")}, [
        path("fill", outline, fill=linear((0, 0), ({"fraction": 1}, 0), ramp(0.49))),
        path("rail", [(0.9, 0), (0.9, {"fraction": 1, "dp": -cut}), (cut, {"fraction": 1, "dp": -0.9}), ({"fraction": 1}, {"fraction": 1, "dp": -0.9})],
             closed=False, stroke=stroke(linear((0, 0), ({"fraction": 1}, 0), ramp(1.0)), 1.8)),
    ])


def objectives_scrollbar(doc: Document, ident: str, viewport: str, tint: str, hot: str, left: float, top: float, height: float,
                         line_step: float = U * 48, key: str = "#str_200380") -> dict:
    """The list's scrollbar at the plates' trailing end (the atlas): a thin
    trough at 0.15 and a thumb at 0.55 in the frame color, the family's
    focus color on hover, focus and press. Scroll position stays view state.
    `line_step` is one entry of the list it scrolls, `key` the list's name."""
    trough = vector(f"{ident}-trough", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                        "pointer-events": keyword("none")}, [
        path("line", [(15.75, 0), (20.25, 0), (20.25, {"fraction": 1}), (15.75, {"fraction": 1})], fill=solid(rgb(tint, 0.15)))])
    thumb_shape = [(14.25, 0), (21.75, 0), (21.75, {"fraction": 1, "dp": -3}), (18.75, {"fraction": 1}), (14.25, {"fraction": 1})]
    base = vector(f"{ident}-thumb-base", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                          "pointer-events": keyword("none"), "opacity": number(1)}, [path("thumb", thumb_shape, fill=solid(rgb(tint, 0.55)))])
    active = vector(f"{ident}-thumb-active", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                              "pointer-events": keyword("none"), "opacity": number(0)}, [path("thumb", thumb_shape, fill=solid(rgb(hot)))])
    thumb = group(f"{ident}-thumb", {**absolute(left=0, top=0, width=36, height=36)}, [base, active])
    track_node = group(f"{ident}-track", {"position": keyword("relative"), "display": keyword("block"), "width": length(36),
                                          "height": length(100, "%")}, [trough, thumb])
    ids = doc.states(ident, {
        "default": [(f"{ident}-thumb-base", "opacity", number(1)), (f"{ident}-thumb-active", "opacity", number(0))],
        "hover": [(f"{ident}-thumb-base", "opacity", number(0)), (f"{ident}-thumb-active", "opacity", number(1))],
        "focus": [(f"{ident}-thumb-base", "opacity", number(0)), (f"{ident}-thumb-active", "opacity", number(1))],
        "pressed": [(f"{ident}-thumb-base", "opacity", number(0)), (f"{ident}-thumb-active", "opacity", number(1))],
        "disabled": [(f"{ident}-thumb-base", "opacity", number(0.4)), (f"{ident}-thumb-active", "opacity", number(0))],
    })
    return group(ident, {**absolute(left=left, top=top, width=36, height=height)}, [track_node],
                 control={"role": "scrollbar", "label": key, "viewport": viewport, "orientation": "vertical",
                          "lineStep": round(line_step, 3), "minimumThumb": 36,
                          "parts": {"track": f"{ident}-track", "thumb": f"{ident}-thumb"}, "states": ids})


def grow_objectives(doc: Document, heading: dict, entries: list, empty: dict, completed_rows: list, serial: dict) -> None:
    """Section 2's reflow on the OBJECTIVES page: above 100 % text an open
    objective's plate already grows with its wrapping description, and its
    title keeps its two lines; the empty state's line takes the lines the
    running language needs and its plate grows with it; each completed
    objective's row grows with its title. The heading, COMPLETED with its
    count and the serial are furniture and keep their size. Every binding
    gives the authored value at 100 % (check_text_bindings)."""
    text_source(doc)
    for entry in entries:
        # The title and description keep their columns over the back bar,
        # which darkens no further; a title takes the lines it needs to hold
        # what its two held at 100 % (its glyphs and lines both grow).
        title = next(child for child in entry["children"] if child["id"].endswith("-title"))
        doc.bind(f"text.{title['id']}.max-height", title["id"], "max-height",
                 text_times(text_times(title["properties"]["max-height"]["value"])))
    nodes = {node["id"]: node for node in walk_nodes(empty)}
    sentence = nodes["objectives-empty-text"]["properties"]
    counts = wrapped_lines(("#str_200935",), "lowpixel", sentence["font-size"]["value"], sentence["width"]["value"])
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    authored = sentence["height"]["value"]
    needed = {"op": "max", "args": [authored, text_times({"op": "*", "args": [lines, sentence["line-height"]["value"]]})]}
    doc.bind("text.objectives-empty-text.height", "objectives-empty-text", "height", {"op": "select", "args": [LARGER, needed, authored]})
    plate = empty["properties"]["height"]["value"]
    doc.bind("text.objectives-empty.height", "objectives-empty", "height",
             {"op": "select", "args": [LARGER, text_sum(plate - authored, needed), plate]})
    for row in completed_rows[1:]:
        doc.bind(f"text.{row['id']}.height", row["id"], "height", text_times(row["properties"]["height"]["value"]))
        title = next(child for child in row["children"] if child["id"].endswith("-title"))
        doc.bind(f"text.{title['id']}.height", title["id"], "height", text_times(title["properties"]["height"]["value"]))
        title["properties"]["text-overflow"] = keyword("clip")
        doc.bind(f"text.{title['id']}.text-overflow", title["id"], "text-overflow", {"op": "select", "args": [LARGER, "ellipsis", "clip"]})
    for node in (heading, completed_rows[0], serial):
        fixed_text(doc, node)


def objectives_page(doc: Document, strogg: bool) -> tuple:
    """The OBJECTIVES page (sections 13.7 and 14.11). Choosing OBJECTIVES
    docks the bands as a page hand-off does and carries the label into the
    title slot; at 550 ms, when a stock page would appear, the stock entry
    motion plays: the back bar slides 220 u in and fades to 0.80 over
    150 ms, then the heading, plates, text and screenshots fade in over
    100 ms (the frames rest at 0.40 in their paint, so they fade in with
    the text rather than leading at 0.50). Every open objective is
    reachable, newest first, as plates of
    the stock construction whose titles wrap to two lines and descriptions
    wrap without a limit; the completed ones follow under COMPLETED, title
    only at 0.40 with a check. The list scrolls with the wheel, the stick or
    the bar, and Back (the prompt bar's BACK where the stock plate stood at
    532,441 u, or the back input) returns to the pause with the bands. The
    Strogg family keeps the construction with #FF9000 frames, #FF9900
    markers, an R_Strogg heading and the STROGG NET serial. Returns the back
    bar, which lies under the framing bands, and the page."""
    frame_tint = ST_OBJ_FRAME if strogg else OBJ_FRAME
    mark_tint = ST_OBJ_MARK if strogg else OBJ_FRAME
    hot = ST_SELECT if strogg else ORANGE
    visible, leaving = "objectives.visible", "objectives.leaving"
    doc.state[visible] = {"type": "boolean", "initial": False}
    doc.state[leaving] = {"type": "boolean", "initial": False}
    doc.state["pause_completed_count"] = {"type": "number", "initial": 0}
    for index in range(OBJECTIVE_PAGE_ROWS):
        doc.state.setdefault(f"pause_objective_{index}", {"type": "string", "initial": ""})
        doc.state[f"pause_objective_text_{index}"] = {"type": "string", "initial": ""}
        doc.state[f"pause_objective_shot_{index}"] = {"type": "string", "initial": ""}
    for index in range(COMPLETED_ROWS):
        doc.state[f"pause_completed_{index}"] = {"type": "string", "initial": ""}
    # The back bar: black 0.80 over the leading 45% of the view, opaque to
    # 190 u, half at 285 u, clear by 380 u. It lies under the framing bands,
    # so their rim light keeps its strength.
    bar = vector("objectives-bar", {**FULL, "transform": transform(), "opacity": number(0), "pointer-events": keyword("none"),
                                    "display": keyword("none")}, [
        path("bar", [(0, 0), (vx(380), 0), (vx(380), {"fraction": 1}), (0, {"fraction": 1})],
             fill=linear((vx(190), 0), (vx(380), 0), [(0, [0, 0, 0, 0.8]), (0.5, [0, 0, 0, 0.4]), (1, [0, 0, 0, 0])]))])
    heading_face = ("r_strogg", 17) if strogg else ("marine", 18)
    heading = label("objectives-heading", "#str_200291", {**absolute(left=U * 20, top=U * 44, width=U * 560, height=24),
        **typeface(heading_face[0], heading_face[1], 24, rgb(OBJECTIVE_HEAD)), "letter-spacing": length(-1.5),
        "white-space": keyword("nowrap")})
    entries = []
    for index in range(OBJECTIVE_PAGE_ROWS):
        ident = f"objectives-entry-{index}"
        marker = vector(f"{ident}-mark", absolute(left=U * 10, top=U * 5, width=U * 8, height=U * 8.5), [
            path("mark", [(0, 0), (U * 8, 0), (U * 8, U * 8.5)], fill=solid(rgb(mark_tint, 0.4)))])
        # The title wraps to two lines at the stock's 308 u; a longer one is
        # cut at the second line's foot.
        title = label(f"{ident}-title", "#str_230030", {"position": keyword("relative"), "display": keyword("block"),
            "margin-left": length(U * 20), "padding-top": length(U * 4), "width": length(U * 308), "max-height": length(U * 24),
            "overflow": keyword("hidden"), **typeface("lowpixel", 14.4, U * 12, [1, 1, 1, 1]), "white-space": keyword("normal")})
        shot = picture(f"{ident}-shot", "", {**absolute(left=U * 1, top=U * 1, width=U * 132, height=U * 98)})
        shot_frame = vector(f"{ident}-shot-frame", {**absolute(left=0, top=0, width=U * 134, height=U * 100)}, [
            path("back", [(0, 0), (U * 134, 0), (U * 134, U * 100), (0, U * 100)], fill=solid([0, 0, 0, 0.5]),
                 stroke=stroke(solid([0, 0, 0, 1]), U * 1))])
        shot_box = group(f"{ident}-shotbox", {"position": keyword("relative"), "display": keyword("block"), "margin-left": length(U * 22),
            "width": length(U * 134), "height": length(U * 100), "flex-shrink": number(0)}, [shot_frame, shot])
        text_node = label(f"{ident}-text", "#str_230030", {"position": keyword("relative"), "display": keyword("block"),
            "margin-left": length(U * 8), "width": length(U * 178), **typeface("lowpixel", 14.4, U * 13.75, rgb(OBJ_TEXT)),
            "white-space": keyword("normal")})
        body = group(f"{ident}-body", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("row"),
            "align-items": keyword("flex-start"), "margin-top": length(U * 2)}, [shot_box, text_node])
        # 112 + 20 u at least. As in the stock, the screenshot (18-118 u)
        # reaches into the foot strip, and 14 u stay clear below the content.
        entries.append(group(ident, {"position": keyword("relative"), "display": keyword("none"), "margin-left": length(U * OBJ_LEFT),
            "box-sizing": keyword("border-box"), "width": length(U * OBJ_WIDTH), "min-height": length(U * 132), "padding-bottom": length(U * 14),
            "margin-bottom": length(U * 11)}, [objective_frame(f"{ident}-frame", frame_tint), marker, title, body]))
        doc.bind(f"{ident}.display", ident, "display",
                 {"op": "select", "args": [{"op": ">", "args": [{"state": "pause_objective_count"}, index]}, "block", "none"]})
        doc.bind(f"{ident}-title.text", f"{ident}-title", "text", {"state": f"pause_objective_{index}"})
        doc.bind(f"{ident}-text.text", f"{ident}-text", "text", {"state": f"pause_objective_text_{index}"})
        doc.bind(f"{ident}-shot.image", f"{ident}-shot", "image", {"state": f"pause_objective_shot_{index}"})
    # No open objective: one plate with the stock static screenshot and the
    # stock line beside it, Lowpixel 0.31 in #D0DEB6 wrapping at 164 u.
    empty = group("objectives-empty", {"position": keyword("relative"), "display": keyword("none"), "margin-left": length(U * OBJ_LEFT),
        "width": length(U * OBJ_WIDTH), "height": length(U * 132), "margin-bottom": length(U * 11)}, [
        objective_frame("objectives-empty-frame", frame_tint),
        vector("objectives-empty-shot-frame", absolute(left=U * 22, top=U * 17, width=U * 134, height=U * 100), [
            path("back", [(0, 0), (U * 134, 0), (U * 134, U * 100), (0, U * 100)], fill=solid([0, 0, 0, 0.5]),
                 stroke=stroke(solid([0, 0, 0, 1]), U * 1))]),
        picture("objectives-empty-shot", "gfx/objectives/none", {**absolute(left=U * 23, top=U * 18, width=U * 132, height=U * 98)}),
        label("objectives-empty-text", "#str_200935", {**absolute(left=U * 164, top=U * 43, width=U * 164, height=U * 73),
              **typeface("lowpixel", 22.3, U * 16, rgb(OBJ_TEXT)), "white-space": keyword("normal")})])
    doc.bind("objectives-empty.display", "objectives-empty", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "pause_objective_count"}, 0]}, "block", "none"]})
    # The completed ones: title only at 0.40 behind a check, newest first.
    completed_rows = [group("objectives-completed-head", {"position": keyword("relative"), "display": keyword("flex"),
        "flex-direction": keyword("row"), "margin-bottom": length(U * 4)}, [
        label("objectives-completed-label", "#str_230047", {"position": keyword("relative"), "display": keyword("block"),
              **typeface("lowpixel", 13, U * 12, [1, 1, 1, 0.4]), "white-space": keyword("nowrap")}),
        label("objectives-completed-count", "#str_230030", {"position": keyword("relative"), "display": keyword("block"),
              "margin-left": length(U * 6), **typeface("lowpixel", 13, U * 12, [1, 1, 1, 0.4]), "white-space": keyword("nowrap")})])]
    doc.bind("objectives-completed-count.text", "objectives-completed-count", "text",
             {"op": "numberText", "args": [{"state": "pause_completed_count"}], "decimals": 0})
    for index in range(COMPLETED_ROWS):
        ident = f"objectives-done-{index}"
        check = vector(f"{ident}-check", absolute(left=0, top=U * 2, width=U * 9, height=U * 8), [
            path("check", [(U * 0.5, U * 4), (U * 3, U * 6.6), (U * 7.6, U * 1.2)], closed=False,
                 stroke=stroke(solid(rgb(hot if strogg else "#B5C784", 0.4)), U * 1.4, join="round", cap="round"))])
        title = label(f"{ident}-title", "#str_230030", {**absolute(left=U * 12, top=0, width=U * 560, height=U * 12),
            **typeface("lowpixel", 14.4, U * 12, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        completed_rows.append(group(ident, {"position": keyword("relative"), "display": keyword("none"), "height": length(U * 13)},
                                    [check, title]))
        doc.bind(f"{ident}.display", ident, "display",
                 {"op": "select", "args": [{"op": ">", "args": [{"state": "pause_completed_count"}, index]}, "block", "none"]})
        doc.bind(f"{ident}-title.text", f"{ident}-title", "text", {"state": f"pause_completed_{index}"})
    completed = group("objectives-completed", {"position": keyword("relative"), "display": keyword("none"),
        "margin-left": length(U * (OBJ_LEFT + 12)), "margin-top": length(U * 2)}, completed_rows)
    doc.bind("objectives-completed.display", "objectives-completed", "display",
             {"op": "select", "args": [{"op": ">", "args": [{"state": "pause_completed_count"}, 0]}, "block", "none"]})
    serial = label("objectives-serial", "#str_200280" if strogg else "#str_200275", {"position": keyword("relative"),
        "display": keyword("block"), "margin-left": length(U * (OBJ_LEFT + 12)), "margin-top": length(U * 12),
        "margin-bottom": length(U * 8), **typeface("r_strogg" if strogg else "marine", 13, 18, rgb(OBJ_SERIAL, 0.25)),
        "white-space": keyword("nowrap")})
    grow_objectives(doc, heading, entries, empty, completed_rows, serial)
    column = group("objectives-column", {"position": keyword("relative"), "display": keyword("block")},
                   [*entries, empty, completed, serial])
    viewport = group("objectives-list", {**absolute(left=0, top=U * OBJ_TOP, width=U * (OBJ_LEFT + OBJ_WIDTH + 2),
        height=U * (OBJ_BOTTOM - OBJ_TOP)), "overflow": keyword("auto")}, [column])
    scrollbar = objectives_scrollbar(doc, "objectives_scroll", "objectives-list", frame_tint, hot,
                                     U * (OBJ_LEFT + OBJ_WIDTH) + 2, U * OBJ_TOP, U * (OBJ_BOTTOM - OBJ_TOP))
    # The prompt bar holds BACK alone: Accept has nothing to do on the page.
    family = {"cap_tint": rgb(ST_SELECT, 0.85), "verb_face": ("r_strogg", 14, rgb(ST_READ, 0.8)), "hot": ST_SELECT} if strogg else {}
    back = page_prompts(doc, "objectives_back", event="objectivesHide", select=False, ident="objectives-prompts", **family)
    canvas = group("objectives-canvas", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"),
                                         "margin-left": length(-CANVAS_W / 2)}, [heading, viewport, scrollbar, back])
    content = group("objectives-content", {**FULL, "opacity": number(0)}, [canvas])
    node = group("objectives", {**FULL, "display": keyword("none")}, [content],
                 modal={"initialFocus": "objectives_scroll", "back": "objectivesHide"})
    for part in ("objectives", "objectives-bar"):
        doc.bind(f"{part}.display", part, "display", {"op": "select", "args": [{"state": visible}, "block", "none"]})
    # Choosing OBJECTIVES docks the bands and carries the label as a page
    # hand-off does; the stock entry motion follows at 550 ms. Back fades the
    # page and brings the bands home; the page closes when they arrive, and
    # a second Back during that leave changes nothing.
    home_return = [{"op": "call", "event": "returnHome"}] if strogg else [{"op": "playTimeline", "timeline": "returnHome"}]
    doc.events["objectivesShow"] = [{"op": "setState", "values": {visible: True, leaving: False}}, {"op": "call", "event": "carry_objectives"},
                                    {"op": "playTimeline", "timeline": "depart"}, {"op": "playTimeline", "timeline": "objectivesEnter"}]
    doc.events["objectivesHide"] = [{"op": "if", "condition": {"op": "!", "args": [{"state": leaving}]},
                                     "then": [{"op": "setState", "values": {leaving: True}},
                                              {"op": "playTimeline", "timeline": "objectivesLeave"}, *home_return]}]
    doc.events["objectivesHidden"] = [{"op": "setState", "values": {visible: False, leaving: False}}]
    shown, hidden_bar = transform(), transform(-220 * U)
    doc.timelines.add("objectivesEnter", 800, [
        track("objectives-bar", "transform", [(0, hidden_bar), (1, hidden_bar), (550, hidden_bar, EASE_OUT), (700, shown)]),
        track("objectives-bar", "opacity", [(0, number(0)), (1, number(0)), (550, number(0)), (700, number(1))]),
        track("objectives-content", "opacity", [(0, number(0)), (1, number(0)), (700, number(0)), (800, number(1))]),
        track("prompts", "opacity", [(0, number(1)), (150, number(0)), (800, number(0))]),
    ])
    doc.timelines.add("objectivesLeave", 650, [
        track("objectives-content", "opacity", [(0, number(1)), (100, number(0)), (650, number(0))]),
        track("objectives-bar", "opacity", [(0, number(1)), (150, number(0)), (650, number(0))]),
        track("prompts", "opacity", [(0, number(0)), (500, number(0)), (650, number(1))]),
    ], complete="objectivesHidden")
    return bar, node


def pause_document(strogg: bool = False) -> dict:
    """The single-player pause (section 13.7). `strogg` builds the Strogg
    pause that the session presents after Kane's stroggification."""
    doc = Document("openq4.pause_strogg" if strogg else "openq4.pause")
    doc.state.update({
        "pause_level": {"type": "string", "initial": ""},
        "pause_detail": {"type": "string", "initial": ""},
        "pause_objectives": {"type": "string", "initial": ""},
        "pause_shot": {"type": "string", "initial": ""},
        "pause_objective_count": {"type": "number", "initial": 0},
        **{f"pause_objective_{index}": {"type": "string", "initial": ""} for index in range(OBJECTIVE_ROWS)},
        "pause_stats": {"type": "string", "initial": ""},
        "pause_saved": {"type": "string", "initial": ""},
    })
    doc.session("resume", "resume")
    doc.session("saveGame", "saveGame")
    doc.session("loadGame", "loadGame")
    doc.session("restartLevel", "restartLevel")
    doc.session("settings", "settings")
    doc.session("quitToMenu", "quitToMenu")
    doc.session("quit", "quit")
    # QUIT CURRENT GAME outruns the lit slot in French: it runs on over the
    # raised trailing section, as the longer dialog titles do. The Strogg
    # pause asks in its own family.
    quit_modal = confirmation(doc, "quitModal", "#str_200004", "#str_200174", "quitToMenu", wide_title=True, strogg=strogg)
    exit_modal = confirmation(doc, "exitModal", "#str_200169", "#str_200170", "quit", frame_leave_ms=200, strogg=strogg)
    doc.events["onBack"] = [{"op": "action", "action": "resume"}]
    items = [("nav_resume", "#str_200381", "resume", None), ("nav_savegame", "#str_200003", "saveGame", None),
             ("nav_loadgame", "#str_200001", "loadGame", None), ("nav_restart", "#str_229983", "restartLevel", None),
             ("nav_objectives", "#str_200380", None, "objectivesShow"), ("nav_settings", "#str_200009", "settings", None),
             ("nav_quit", "#str_230025", None, "quitModalShow")]
    translation = Translation() if strogg else None
    if strogg:
        plates = [strogg_navigation_plate(doc, ident, key, translation, index, action=action, event=event)
                  for index, (ident, key, action, event) in enumerate(items)]
    else:
        plates = [navigation_plate(doc, ident, key, action=action, event=event) for ident, key, action, event in items]
    nav = group("nav", {**absolute(left=0, top=U * 182.2, width=U * 413), "display": keyword("flex"),
                        "flex-direction": keyword("column")}, plates)
    if strogg:
        # The actions translate 45 ms apart, EXIT after them, and the level
        # block's lines follow.
        exit_link = strogg_link(doc, "link_exit", "#str_200013", translation, 120 + 45 * len(items), event="exitModalShow")
        wrap_menu(plates, [exit_link])
        home_parts = [strogg_title(translation), nav, strogg_level_block(doc, translation, 120 + 45 * (len(items) + 1)),
                      strogg_plinth([exit_link])]
    else:
        exit_link = link(doc, "link_exit", "#str_200013", event="exitModalShow")
        wrap_menu(plates, [exit_link])
        home_parts = [label("paused-title", "#str_230031", {**absolute(left=U * 44, top=U * 134, width=U * 330, height=U * 24),
                            **typeface("marine", 18, U * 24, [1, 1, 1, 0.5]), "white-space": keyword("nowrap")}),
                      nav, level_block(doc), plinth(doc, [exit_link])]
    # GAME PAUSED is the screen's title; the level block's details grow.
    fixed_text(doc, home_parts[0])
    reflow_level_block(doc, home_parts[2], strogg)
    content = group("home", {**FULL, "transform": transform(), "opacity": number(1)}, [
        group("home-content", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)},
              home_parts),
    ])
    # A tall view stands the title, actions, EXIT and level block in one column.
    tall_pause(doc, content["children"][0], *home_parts)
    for modal in (quit_modal, exit_modal):
        tall_dialog(doc, modal)
    # The paused view, softened and never dimmed (sections 9 and 13.7). The
    # specification gives scene softening no values of its own, so the pause
    # takes modal.softfocus's; it ramps in with menu.fade. With the
    # opaque-backing option, or where the renderer cannot soften the view, a
    # darkening scrim and vignette stand in (section 13.6).
    doc.state["soft_focus"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedSoftFocus"}
    softened = group("scene-softfocus", {**FULL, "display": keyword("none"), "backdrop-blur": length(SOFT_FOCUS_BLUR),
                                         "backdrop-saturate": number(SOFT_FOCUS_SATURATION)})
    doc.bind("scene-softfocus.display", "scene-softfocus", "display", {"op": "select", "args": [{"state": "soft_focus"}, "block", "none"]})
    doc.bind("scrim.display", "scrim", "display", {"op": "select", "args": [{"state": "soft_focus"}, "none", "block"]})
    scrim = vector("scrim", {**FULL, "display": keyword("block"), "opacity": number(1)}, [
        path("dim", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid([0, 0, 0, 0.45])),
        path("lead", [(0, 0), (vx(413), 0), (vx(413), {"fraction": 1}), (0, {"fraction": 1})],
             fill=linear((vx(-107), 0), (vx(413), 0), [(0, [0, 0, 0, 0.7]), (0.55, [0, 0, 0, 0.35]), (1, [0, 0, 0, 0])])),
    ])
    prompts = [("#str_107019", "#str_231042"), ("#str_107020", "#str_200381")]
    carries = [("saveGame", "#str_200003", 212.2), ("loadGame", "#str_200001", 242.2),
               ("restartLevel", "#str_229983", 272.2), ("objectives", "#str_200380", 302.2),
               ("settings", "#str_200009", 332.2)]
    objectives_bar, objectives = objectives_page(doc, strogg)
    if strogg:
        # The Strogg light, crosses and bands.
        field = lit_field("field", 0.55, glow=ST_LIGHT, grid={"tint": rgb(ST_GRID), "arm_u": 5.5})
        bands = framing_bands("band", STROGG_BANDS)
        prompt = prompt_bar(doc, prompts, cap_tint=rgb(ST_SELECT, 0.85), verb_face=("r_strogg", 14, rgb(ST_READ, 0.8)))
        carry = title_carry(doc, carries, face="r_strogg", tint=ST_SELECT)
    else:
        field = lit_field("field", 0.45)
        bands = framing_bands("band")
        prompt = prompt_bar(doc, prompts)
        carry = title_carry(doc, carries)
    # Everything but the softened view and its stand-in: returning to the
    # game hides it at once (section 8), while the backdrop releases.
    doc.state["pause.released"] = {"type": "boolean", "initial": False}
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [
        *field,
        objectives_bar,
        *bands,
        content,
        objectives,
        prompt,
        carry,
        quit_modal,
        exit_modal,
        group("fade", {**FULL, "background-color": colour([0, 0, 0, 0]), "pointer-events": keyword("none")}),
    ])
    doc.bind("chrome.display", "chrome", "display", {"op": "select", "args": [{"state": "pause.released"}, "none", "block"]})
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [
        softened,
        scrim,
        chrome,
    ])
    band_motion(doc, "band", "home", [], quick=("plinth", "prompts"))
    # The softened view rests at modal.softfocus, so a page's return keeps it.
    # A play retargets its first key from the current value, and the pause
    # document outlives each pause; the second key restarts the ramp from
    # nothing every time the pause opens, behind menu.fade's black.
    opening = next(timeline for timeline in doc.timelines.items if timeline["id"] == "open")
    opening["tracks"] += [
        track("scene-softfocus", "backdrop-blur", [(0, length(0)), (1, length(0)), (250, length(SOFT_FOCUS_BLUR))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(1)), (1, number(1)), (250, number(SOFT_FOCUS_SATURATION))]),
    ]
    hide_carry_at_home(doc)
    if strogg:
        translation.finish(doc)
    # A pause can close without Back (a console load or disconnect); every
    # opening starts at home without the Objectives page, prompts showing.
    opening["tracks"] += [track("scrim", "opacity", [(0, number(1)), (1, number(1)), (250, number(1))])]
    reset = {"op": "setState", "values": {"objectives.visible": False, "objectives.leaving": False, "pause.released": False}}
    # Each opening focuses RESUME, the primary action (section 13.4); a page's
    # Back (returnHome) keeps the focus on the item that led there.
    resume = {"op": "focus", "control": "nav_resume"}
    doc.events["open"] = [reset, *doc.events.get("open", [{"op": "playTimeline", "timeline": "open"}]), resume]
    # Returning to the game closes the screen at once (section 8), but the
    # softened view, or the scrim standing in for it, releases over 250 ms as
    # the modal backdrop does (section 4); the session keeps drawing the
    # closed screen over the running game until then.
    doc.timelines.add("release", 250, [
        track("scene-softfocus", "backdrop-blur", [(0, length(SOFT_FOCUS_BLUR)), (250, length(0))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(SOFT_FOCUS_SATURATION)), (250, number(1))]),
        track("scrim", "opacity", [(0, number(1)), (250, number(0))]),
    ])
    doc.events["release"] = [{"op": "setState", "values": {"pause.released": True}}, {"op": "playTimeline", "timeline": "release"}]
    # The pause can come back through returnHome as well as open: a page left
    # with Back before the legacy menu reached home. Every activation after a
    # release shows the screen again and ramps the softened view back in;
    # when open follows, its own play of the same properties takes over.
    doc.timelines.add("unrelease", 250, [
        track("scene-softfocus", "backdrop-blur", [(0, length(0)), (1, length(0)), (250, length(SOFT_FOCUS_BLUR))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(1)), (1, number(1)), (250, number(SOFT_FOCUS_SATURATION))]),
        track("scrim", "opacity", [(0, number(1)), (1, number(1)), (250, number(1))]),
    ])
    # The release hid every action, and its focus with it: RESUME takes it.
    doc.events["onActivate"] = [{"op": "if", "condition": {"state": "pause.released"},
                                 "then": [{"op": "setState", "values": {"pause.released": False}},
                                          {"op": "playTimeline", "timeline": "unrelease"}, resume]}]
    return fit_narrow(doc.build(root))


# ----------------------------------------------------------------- the loading

def corner_bracket(ident: str, left_u: float, top_u: float, flip_x: bool, flip_y: bool) -> dict:
    """load_corner (Appendix B.1): an L bracket with 24 u legs in a 52 u
    window, a 0.8 u white core and a warm 7 u halo, additive #3A3A3A."""
    size = U * 52
    inset, leg = U * 14, U * 24
    ox = size - inset if flip_x else inset
    oy = size - inset if flip_y else inset
    sx = -1 if flip_x else 1
    sy = -1 if flip_y else 1
    points = [(ox, oy + sy * leg), (ox, oy), (ox + sx * leg, oy)]
    halo = path("halo", points, closed=False, stroke=stroke(solid(rgb("#502D14", 0.55)), U * 7, minimum=1, join="miter"), blend="additive")
    core = path("core", points, closed=False, stroke=stroke(solid(rgb("#3A3A3A")), U * 1.2, minimum=1, join="miter"), blend="additive")
    white = path("white", points, closed=False, stroke=stroke(solid([1, 1, 1, 0.55]), U * 0.8, minimum=1, join="miter"), blend="additive")
    return vector(ident, absolute(left=U * left_u, top=U * top_u, width=size, height=size), [halo, core, white])


# The multiplayer arsenal's kinds of item (RETAINED_ARSENAL_ICONS in Session.cpp).
ARSENAL_ICONS = 20
# The loading server card's roster rows (RETAINED_ROSTER_ROWS in Session.cpp):
# six names per list and a "+N more" line.
SERVER_ROSTER_ROWS = 7
SERVER_ROSTER_HEAD, SERVER_ROSTER_PITCH = 20.0, 16.0
# The multiplayer palette (section 4): teams, spectators, notices and headers.
MP_MARINE, MP_STROGG, MP_SPECTATOR, MP_NOTICE, MP_HEADING = "#6AA42B", "#FF7B04", "#999999", "#FFFF8D", "#8B964B"
# Single player tips: Lowpixel 14 dp, two lines at most, from the band's riser
# to the bar (216-384 u); ui_retained_gate.py checks every tip in every
# language fits.
TIP_LEFT, TIP_WIDTH, TIP_SIZE, TIP_LINE, TIP_ALPHA = 216.0, 168.0, 14.0, 10.5, 0.7


def tip_lines(text: str, language_width: float | None = None) -> list:
    """`text` wrapped as the host wraps it at the tip's width: greedy at
    spaces, each glyph's advance from lowpixel.ttf."""
    width = (language_width or U * TIP_WIDTH)
    lines, current = [], ""
    for word in text.split(" "):
        candidate = word if not current else current + " " + word
        if current and text_width("lowpixel", candidate, TIP_SIZE) > width:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return lines


def loading_document() -> dict:
    """Section 14.17: the stock loading screens in their Remastered form: the
    drifting levelshot, the detail line, the progress with the loader's phase,
    and in multiplayer the band kept low under the server card."""
    doc = Document("openq4.loading")
    doc.state.update({
        "map_loading": {"type": "number", "initial": 0},
        "loading_levelshot": {"type": "string", "initial": ""},
        "loading_levelname": {"type": "string", "initial": ""},
        "loading_detail": {"type": "string", "initial": ""},
        "loading_objectives": {"type": "string", "initial": ""},
        "loading_author": {"type": "string", "initial": ""},
        "server_name": {"type": "string", "initial": ""},
        "server_ip": {"type": "string", "initial": ""},
        "server_gametype": {"type": "string", "initial": ""},
        "server_limit": {"type": "string", "initial": ""},
        "loading_mp": {"type": "boolean", "initial": False},
        "loading_intro": {"type": "boolean", "initial": False},
        "loading_ready": {"type": "boolean", "initial": False},
        "motion_reduced": {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"},
        "loading_phase": {"type": "string", "initial": ""},
        "loading_count": {"type": "string", "initial": ""},
        "loading_tip_a": {"type": "string", "initial": ""},
        "loading_tip_b": {"type": "string", "initial": ""},
        "server_message": {"type": "string", "initial": ""},
        "server_team_mode": {"type": "boolean", "initial": False},
        "server_roster_known": {"type": "boolean", "initial": False},
        "server_roster_rows": {"type": "number", "initial": 0},
        "server_team_a": {"type": "string", "initial": ""},
        "server_team_b": {"type": "string", "initial": ""},
        "server_players": {"type": "string", "initial": ""},
        "server_roster_extra": {"type": "string", "initial": ""},
        **{f"load_icon_{index}": {"type": "number", "initial": 0} for index in range(1, ARSENAL_ICONS + 1)},
        **{f"load_icon_src_{index}": {"type": "string", "initial": ""} for index in range(1, ARSENAL_ICONS + 1)},
        **{f"load_icon_{channel}_{index}": {"type": "number", "initial": 1}
           for index in range(1, ARSENAL_ICONS + 1) for channel in ("r", "g", "b")},
    })
    doc.events["FinishedLoading"] = [{"op": "setState", "values": {"loading_ready": True}},
                                     {"op": "playTimeline", "timeline": "continuePulse"}]
    # Loading bands (Appendix B.2): black 0.80 with one 45-degree riser each.
    # Edge furniture belongs to the left/right virtual screens. A narrow
    # view fits the 960 dp center screen to its width and gains top/bottom
    # space; every bottom anchor must therefore follow the actual height.
    foot = lambda u: {"fraction": 1, "dp": round(U * (u - 480), 3)}
    top_edge = [(edge(0), U * 28.8), (U * 268.8, U * 28.8), (U * 290, U * 9.3), (edge(1), U * 9.3)]
    bottom_edge = [(edge(0), foot(468.5)), (U * 165, foot(468.5)), (U * 207.5, foot(426)), (edge(1), foot(426))]

    def spill(edge_points, down: bool) -> list:
        # The *_edgeadd companions add #181D0A from the inner edge into the
        # picture: 0.98 at the edge to 0.55 at 35 u, gone by 80 u.
        paths = []
        steps = 8
        for index in range(steps, 0, -1):
            width = 2 * U * 80 * index / steps
            paths.append(path(f"spill-{index}", edge_points, closed=False, blend="additive",
                              stroke=stroke(solid(rgb(LOAD_SPILL, round(0.98 / steps * 1.4, 4))), width, minimum=0, join="round")))
        return paths

    top_band = vector("load-top", dict(FULL), spill(top_edge, True) + [
        path("band", [(edge(1), -400), (edge(0), -400)] + top_edge, fill=solid([0, 0, 0, 0.8]))])
    bottom_band = vector("load-bottom", dict(FULL), spill(bottom_edge, False) + [
        path("band", bottom_edge + [(edge(1), foot(480 + 400 / U)), (edge(0), foot(480 + 400 / U))], fill=solid([0, 0, 0, 0.8]))])
    shot = picture("load-shot", "", {**FULL, "transform": transform()})
    doc.bind("load-shot.image", "load-shot", "image", {"state": "loading_levelshot"})
    # Remastered: the levelshot drifts in by 3% over the load; reduced motion
    # holds it still (section 14.17).
    drift = {"op": "select", "args": [{"state": "motion_reduced"}, 1, {"op": "+", "args": [
        1, {"op": "*", "args": [0.03, {"op": "clamp", "args": [{"state": "map_loading"}, 0, 1]}]}]}]}
    doc.bind("load-shot.transform", "load-shot", "transform", [0, 0, drift, drift, 0])

    # The top-trailing bracket is left out: the level name and its detail
    # line stand where it would be, and it showed through beneath them.
    corners = []
    for suffix, right, bottom in (("tl", False, False), ("bl", False, True), ("br", True, True)):
        node = corner_bracket(f"bracket-{suffix}", 0, 0, right, bottom)
        node["properties"] = absolute(**{"right" if right else "left": U * 8,
                                         "bottom" if bottom else "top": U * (54 if bottom else 28),
                                         "width": U * 52, "height": U * 52})
        corners.append(node)
    brackets = group("brackets", {"display": keyword("block"), **FULL}, corners)
    doc.bind("brackets.display", "brackets", "display", {"op": "select", "args": [{"state": "loading_intro"}, "none", "block"]})
    # The dot matrix: ten by two 4 u squares on a 9 u pitch, top-leading.
    dots = []
    for column in range(10):
        for row in range(2):
            x0, y0 = U * (12 + 9 * column), U * (4 + 9 * row)
            dots.append(path(f"dot-{column}-{row}", [(x0, y0), (x0 + U * 4, y0), (x0 + U * 4, y0 + U * 4), (x0, y0 + U * 4)],
                             fill=solid(rgb(DOTS, 1))))
    dot_matrix = vector("dots", absolute(left=0, top=0, width=CANVAS_W, height=U * 40), dots)

    # Identity: the level name trails at the top over a name strip (section 9).
    identity = group("identity", {"display": keyword("block"), **absolute(right=0, top=U * 34, width=CANVAS_W, height=U * 60)}, [
        # The strip also darkens the message line, which must read over any levelshot.
        vector("name-strip", absolute(left=U * 300, top=0, width=U * 340, height=U * 44), [
            path("strip", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
                 fill=linear((0, 0), ({"fraction": 1}, 0), [(0, [0, 0, 0, 0]), (0.5, [0, 0, 0, 0.55]), (1, [0, 0, 0, 0.8])]))]),
        label("level-name", "#str_230030", {**absolute(left=U * 100, top=0, width=U * 532, height=U * 26), **typeface("marine", 26, U * 26, [1, 1, 1, 0.8]),
              "text-align": keyword("right"), "letter-spacing": length(-0.1, "em"), "white-space": keyword("nowrap")}),
        label("level-detail", "#str_230030", {**absolute(left=U * 100, top=U * 27, width=U * 532, height=U * 14), **typeface("lowpixel", 17, U * 14, [1, 1, 1, 0.62]),
              "text-align": keyword("right"), "white-space": keyword("nowrap")}),
    ])
    doc.bind("level-name.text", "level-name", "text", {"state": "loading_levelname"})
    doc.bind("level-detail.text", "level-detail", "text", {"state": "loading_detail"})
    doc.bind("identity.display", "identity", "display", {"op": "select", "args": [{"state": "loading_intro"}, "none", "block"]})

    # Objectives on the leading side, in the objectives display's open plate (section 14.11).
    objectives = group("objectives", {"display": keyword("block"), **absolute(left=U * 18, top=U * 120, width=U * 250, height=U * 180)}, [
        vector("objectives-plate", absolute(left=0, top=0, width=U * 250, height=U * 180), [
            path("fill", [(0, 0), (U * 250, 0), (U * 250, U * 180), (U * 20, U * 180), (0, U * 160)],
                 fill=linear((0, 0), (U * 250, 0), [(0, rgb("#B0CD6B", 0.2)), (0.3, rgb("#B0CD6B", 0.2)), (0.56, rgb("#B0CD6B", 0)), (1, rgb("#B0CD6B", 0))])),
            path("rail", [(0.75, 0), (0.75, U * 160), (U * 20, U * 180 - 0.75), (U * 250, U * 180 - 0.75)], closed=False,
                 stroke=stroke(linear((0, 0), (U * 250, 0), [(0, rgb("#B0CD6B", 0.4)), (0.56, rgb("#B0CD6B", 0.4)), (1, rgb("#B0CD6B", 0))]), 1.5)),
            path("backing", [(0, 0), (U * 250, 0), (U * 250, U * 180), (U * 20, U * 180), (0, U * 160)],
                 fill=linear((0, 0), (U * 250, 0), [(0, [0, 0, 0, 0.5]), (0.66, [0, 0, 0, 0.5]), (1, [0, 0, 0, 0])])),
        ]),
        label("objectives-head", "#str_200291", {**absolute(left=U * 10, top=U * 8, width=U * 230, height=U * 16),
              **typeface("marine", 18, U * 16, rgb(OBJECTIVE_HEAD)), "letter-spacing": length(-0.05, "em"), "white-space": keyword("nowrap")}),
        label("objectives-text", "#str_230030", {**absolute(left=U * 10, top=U * 30, width=U * 225, height=U * 140),
              **typeface("lowpixel", 14, 18, [0.82, 0.87, 0.71, 1]), "overflow": keyword("hidden")}),
    ])
    doc.bind("objectives-text.text", "objectives-text", "text", {"state": "loading_objectives"})
    doc.bind("objectives.display", "objectives", "display", {"op": "select", "args": [
        {"op": "||", "args": [{"state": "loading_mp"}, {"op": "||", "args": [{"state": "loading_intro"},
                                                                              {"op": "==", "args": [{"state": "loading_objectives"}, ""]}]}]}, "none", "block"]})

    # Remastered multiplayer (section 14.17): the bottom band stays low and a
    # section 6 card on the leading side, above the band's thin part, holds
    # the server's name in its header, then its address in Profont, the mode,
    # the limits, the players by team (or the players, outside team modes)
    # over a line of spectators and players still connecting, and the
    # server's message (si_motd) in the notices color. The card stands on
    # 384 u, clear of the lower corner bracket (388-412 u): its parts flow,
    # so it grows upward with what the session publishes and the message
    # wraps to at most three lines.
    cw, major, minor, header = U * 250, 12.0, 3.0, 40.0
    foot = lambda dp: {"fraction": 1, "dp": round(-dp, 3)}
    outline = [(minor, 0), (cw - major, 0), (cw, major), (cw, foot(minor)), (cw - minor, foot(0)), (major, foot(0)), (0, foot(major)),
               (0, minor)]

    def flow(props: dict) -> dict:
        return {"position": keyword("relative"), "display": keyword("block"), **props}

    def card_line(ident: str, tint: list, face: str = "lowpixel", size: float = 17, top: float = 0) -> dict:
        return label(ident, "#str_230030", flow({"margin-left": length(15), "margin-top": length(top), "width": length(cw - 30),
                                                 "height": length(20), **typeface(face, size, 20, tint), "white-space": keyword("nowrap")}))

    half = (cw - 30 - 12) / 2
    rows = {"op": "clamp", "args": [{"state": "server_roster_rows"}, 0, SERVER_ROSTER_ROWS]}
    named = {"op": ">", "args": [rows, 0]}
    extra_shown = {"op": "!=", "args": [{"state": "server_roster_extra"}, ""]}
    roster_shown = {"op": "&&", "args": [{"state": "server_roster_known"}, {"op": "||", "args": [named, extra_shown]}]}
    # Inside the roster: the list heads, the names from 22 dp on a 16 dp pitch,
    # then the spectators and connecting line.
    names_top = {"op": "select", "args": [named, SERVER_ROSTER_HEAD + 2, 4]}
    extra_top = {"op": "+", "args": [names_top, {"op": "*", "args": [rows, SERVER_ROSTER_PITCH]}]}
    roster_h = {"op": "+", "args": [extra_top, {"op": "select", "args": [extra_shown, SERVER_ROSTER_PITCH, 0]}]}

    def roster_column(ident: str, key: str, left: float, width: float, tint: list) -> list:
        return [label(f"{ident}-head", key, {**absolute(left=left, top=4, width=width, height=SERVER_ROSTER_HEAD - 4),
                                            **typeface("lowpixel", 13, SERVER_ROSTER_HEAD - 4, tint), "white-space": keyword("nowrap")}),
                label(ident, "#str_230030", {**absolute(left=left, top=SERVER_ROSTER_HEAD + 2, width=width,
                                                        height=SERVER_ROSTER_PITCH * SERVER_ROSTER_ROWS),
                                             **typeface("lowpixel", 14, SERVER_ROSTER_PITCH, [1, 1, 1, 0.8]), "white-space": keyword("pre"),
                                             "overflow": keyword("hidden")})]
    server = group("server", {"display": keyword("block"), **FULL, "pointer-events": keyword("none")}, [
        group("server-card", {**absolute(left=U * 18, bottom=720 - U * 384, width=cw), "height": keyword("auto"),
                              "padding-bottom": length(12)}, [
            vector("server-frame", {**absolute(left=0, top=0, width=cw), "height": length(100, "%")}, [
                path("body", outline, fill=solid([0, 0, 0, 0.94])),
                path("wash", [(minor, 0.75), (cw - major, 0.75), (cw - major + header * 0.3, header), (0.75, header), (0.75, minor)],
                     fill=linear((0, 0), (cw, 0), [(0, [1, 1, 1, 0.08]), (0.5, [1, 1, 1, 0.08]), (1, [1, 1, 1, 0])])),
                path("rule", [(0, header), (cw, header)], closed=False, stroke=stroke(solid(rgb(RAIL_CARD, 0.5)), 1)),
                path("rail", outline, stroke=stroke(solid(rgb(RAIL_CARD)), 1.5)),
                marker_path("mark", 14, 16, 8, rgb(MARKER)),
            ]),
            label("server-name", "#str_230030", flow({"margin-left": length(30), "width": length(cw - 45), "height": length(header),
                  **typeface("marine", 14, header, [1, 1, 1, 0.9]), "white-space": keyword("nowrap")})),
            # Tabular detail (section 5): Profont 13 dp.
            card_line("server-address", [1, 1, 1, 0.62], face="profont", size=13, top=8),
            card_line("server-mode", [1, 1, 1, 0.8]),
            card_line("server-rules", [1, 1, 1, 0.62]),
            group("server-roster", flow({"width": length(cw), "height": length(0), "display": keyword("none")}), [
                group("server-teams", {**absolute(left=0, top=0, width=cw), "height": length(100, "%"), "display": keyword("none")}, [
                    *roster_column("server-team-a", "#str_200197", 15, half, rgb(MP_MARINE)),
                    *roster_column("server-team-b", "#str_200199", 15 + half + 12, half, rgb(MP_STROGG)),
                ]),
                group("server-all", {**absolute(left=0, top=0, width=cw), "height": length(100, "%"), "display": keyword("none")},
                      roster_column("server-players", "#str_200038", 15, cw - 30, rgb(MP_HEADING))),
                label("server-roster-extra", "#str_230030", {**absolute(left=15, top=4, width=cw - 30, height=SERVER_ROSTER_PITCH),
                      **typeface("lowpixel", 13, SERVER_ROSTER_PITCH, rgb(MP_SPECTATOR)), "white-space": keyword("nowrap")}),
            ]),
            label("server-message", "#str_230030", flow({"margin-left": length(15), "margin-top": length(6), "width": length(cw - 30),
                  "max-height": length(60), "overflow": keyword("hidden"), **typeface("lowpixel", 16, 20, rgb(MP_NOTICE)),
                  "white-space": keyword("pre-line"), "display": keyword("none")})),
        ]),
    ])
    doc.bind("server-name.text", "server-name", "text", {"state": "server_name"})
    doc.bind("server-address.text", "server-address", "text", {"state": "server_ip"})
    doc.bind("server-mode.text", "server-mode", "text", {"state": "server_gametype"})
    doc.bind("server-rules.text", "server-rules", "text", {"state": "server_limit"})
    doc.bind("server-roster.display", "server-roster", "display", {"op": "select", "args": [roster_shown, "block", "none"]})
    doc.bind("server-roster.height", "server-roster", "height", roster_h)
    doc.bind("server-teams.display", "server-teams", "display", {"op": "select", "args": [
        {"op": "&&", "args": [named, {"state": "server_team_mode"}]}, "block", "none"]})
    doc.bind("server-all.display", "server-all", "display", {"op": "select", "args": [
        {"op": "&&", "args": [named, {"op": "!", "args": [{"state": "server_team_mode"}]}]}, "block", "none"]})
    for column, key in (("server-team-a", "server_team_a"), ("server-team-b", "server_team_b"), ("server-players", "server_players")):
        doc.bind(f"{column}.text", column, "text", {"state": key})
    doc.bind("server-roster-extra.text", "server-roster-extra", "text", {"state": "server_roster_extra"})
    doc.bind("server-roster-extra.top", "server-roster-extra", "top", extra_top)
    doc.bind("server-message.text", "server-message", "text", {"state": "server_message"})
    doc.bind("server-message.display", "server-message", "display", {"op": "select", "args": [
        {"op": "!=", "args": [{"state": "server_message"}, ""]}, "block", "none"]})
    doc.bind("server.display", "server", "display", {"op": "select", "args": [{"state": "loading_mp"}, "block", "none"]})

    # Remastered progress (section 14.17): the thick part of the bottom band
    # holds LOADING above a 240 u bar ending at 632 u; under the bar run the
    # loader's phase with its count and, trailing, the percentage in the value
    # color. When the level is ready LOADING becomes the continue prompt for
    # the active device: the south button and CONTINUE on a controller.
    bar_x, bar_w, tail = U * 392, U * 240, U * 8
    # Multiplayer never waits: when its load ends the prompt reads JOINING.
    joining = {"op": "&&", "args": [{"state": "loading_mp"}, {"op": ">=", "args": [{"state": "map_loading"}, 1]}]}
    controller = {"op": "!", "args": [{"op": "==", "args": [{"state": PROMPT_GLYPHS}, 0]}]}
    prompt = {"op": "select", "args": [{"state": "loading_ready"},
              {"op": "select", "args": [controller, "#str_200984", "#str_200937"]},
              {"op": "select", "args": [joining, "#str_230037", "#str_200938"]}]}
    glyph_shown = {"op": "select", "args": [{"op": "&&", "args": [{"state": "loading_ready"}, controller]},
                                            "block", "none"]}

    def south_button(ident: str, tint: list) -> dict:
        """The confirm glyph: the active controller family's south button
        (section 13.5); a generic controller's is the four positions as a
        diamond with the south one filled."""
        return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(18),
                             "height": length(18), "margin-right": length(8)}, device_glyphs(doc, ident, "south", tint, 18))

    def prompt_row(ident: str, offset: float, tint: list) -> dict:
        return group(ident, {**absolute(top=U * 2 + offset, height=U * 22, right=tail - offset), "display": keyword("flex"),
                             "flex-direction": keyword("row"), "align-items": keyword("center")}, [
            south_button(f"{ident}-glyph", tint),
            label(f"{ident}-text", "#str_200938", {"position": keyword("relative"), "display": keyword("block"),
                  **typeface("marine", 29, U * 22, tint), "white-space": keyword("nowrap")}),
        ])

    def stage_label(ident: str, key: str, tint: list, **more) -> dict:
        return label(ident, key, {"position": keyword("relative"), "display": keyword("block"),
                                  **typeface("lowpixel", 14, U * 10, tint), "white-space": keyword("nowrap"), **more})

    progress = group("progress", absolute(right=0, bottom=0, width=CANVAS_W, height=U * 54), [
        prompt_row("progress-prompt-shadow", 1.5, [0, 0, 0, 0.8]),
        prompt_row("progress-prompt", 0, [1, 1, 1, 1]),
        vector("progress-track", absolute(left=bar_x, top=U * 26, width=bar_w, height=U * 6), [
            path("track", [(U * 6, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1}), (0, U * 6)],
                 fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(PROGRESS, 0.2)), (0.5, rgb(PROGRESS, 0.3)), (1, rgb(PROGRESS, 0.2))]))]),
        group("progress-fill-clip", {**absolute(left=bar_x, top=U * 26, width=0, height=U * 6), "overflow": keyword("hidden")}, [
            vector("progress-fill", absolute(left=0, top=0, width=bar_w, height=U * 6), [
                path("fill", [(U * 6, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1}), (0, U * 6)],
                     fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(PROGRESS, 0.35)), (0.5, rgb(PROGRESS, 0.6)), (1, rgb(PROGRESS, 0.35))])),
                path("core", [(U * 3, U * 3), ({"fraction": 1}, U * 3)], closed=False, stroke=stroke(solid(rgb("#FFC070", 0.5)), 1, minimum=1)),
            ])]),
        group("progress-stage", {**absolute(left=bar_x, top=U * 34.5, height=U * 10), "display": keyword("flex"),
                                 "flex-direction": keyword("row")}, [
            stage_label("progress-phase", "#str_230030", [1, 1, 1, 0.62]),
            stage_label("progress-count", "#str_230030", [1, 1, 1, 0.42], **{"margin-left": length(8)}),
        ]),
        group("progress-percent", {**absolute(top=U * 34.5, height=U * 10, right=tail), "display": keyword("flex"),
                                   "flex-direction": keyword("row")}, [
            stage_label("progress-percent-value", "#str_230030", rgb(VALUE)),
            stage_label("progress-percent-sign", "#str_230036", rgb(VALUE)),
        ]),
    ])
    doc.bind("progress-fill-clip.width", "progress-fill-clip", "width",
             {"op": "*", "args": [{"op": "clamp", "args": [{"state": "map_loading"}, 0, 1]}, bar_w]})
    for row in ("progress-prompt", "progress-prompt-shadow"):
        doc.bind(f"{row}-text.text", f"{row}-text", "text", prompt)
        doc.bind(f"{row}-glyph.display", f"{row}-glyph", "display", glyph_shown)
    doc.bind("progress-phase.text", "progress-phase", "text", {"state": "loading_phase"})
    doc.bind("progress-count.text", "progress-count", "text", {"state": "loading_count"})
    doc.bind("progress-percent-value.text", "progress-percent-value", "text", {"op": "numberText", "decimals": 0, "args": [
        {"op": "round", "args": [{"op": "*", "args": [{"op": "clamp", "args": [{"state": "map_loading"}, 0, 1]}, 100]}]}]})
    # The prompt pulses from 100 % to 50 % over 500 ms and back over 200 ms (section 8).
    doc.timelines.add("continuePulse", 700, [
        track("progress-prompt-text", "color", [(0, colour([1, 1, 1, 1])), (500, colour([1, 1, 1, 0.5])), (700, colour([1, 1, 1, 1]))])],
        iterations=0)
    # The multiplayer arsenal (section 14.17): the thick band's leading part,
    # from its riser to the bar, holds the map's items, two rows of ten 16 u
    # icons on a 17 u pitch. Each kind fades in over 150 ms in its color code
    # as it spawns; the session publishes its image and tint and plays its
    # timeline. The 1 ms key restarts a fade a previous load completed.
    icons = []
    for index in range(1, ARSENAL_ICONS + 1):
        column, row = (index - 1) % 10, (index - 1) // 10
        ident = f"arsenal-{index}"
        icons.append(picture(ident, "", {**absolute(left=U * (216 + 17 * column), top=U * (10 + 18 * row), width=U * 16, height=U * 16),
                                         "display": keyword("none"), "opacity": number(0), "image-color": colour([1, 1, 1, 1])},
                             fit="contain"))
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [
            {"op": "&&", "args": [{"op": ">=", "args": [{"state": f"load_icon_{index}"}, 1]},
                                  {"op": "!=", "args": [{"state": f"load_icon_src_{index}"}, ""]}]}, "block", "none"]})
        doc.bind(f"{ident}.image", ident, "image", {"state": f"load_icon_src_{index}"})
        doc.bind(f"{ident}.image-color", ident, "image-color",
                 [{"state": f"load_icon_r_{index}"}, {"state": f"load_icon_g_{index}"}, {"state": f"load_icon_b_{index}"}, 1])
        doc.timelines.add(f"arsenal{index}", 150, [track(ident, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))])])
    arsenal = group("arsenal", {"display": keyword("block"), **absolute(left=0, bottom=0, width=CANVAS_W, height=U * 54)}, icons)
    doc.bind("arsenal.display", "arsenal", "display", {"op": "select", "args": [{"state": "loading_mp"}, "block", "none"]})
    # Single player tips (section 14.17): the thick band's leading part, where
    # multiplayer has its arsenal, holds a localized tip of at most two lines
    # of Lowpixel 14 dp under a TIP tag while the level loads. The session
    # publishes each tip's key into one of two slots every 6 s and plays tipA
    # or tipB, which cross-fade the slots over 250 ms by text alpha (no
    # composition layer). When the level is ready the continue prompt, which
    # runs across this part in most languages, takes the band.
    def tip_slot(ident: str) -> dict:
        return label(ident, "#str_230030", {**absolute(left=U * TIP_LEFT, top=U * 12, width=U * TIP_WIDTH, height=U * 2 * TIP_LINE),
                                            **typeface("lowpixel", TIP_SIZE, U * TIP_LINE, [1, 1, 1, 0]), "white-space": keyword("normal"),
                                            "overflow": keyword("hidden")})
    tips = group("tips", {"display": keyword("none"), **absolute(left=0, bottom=0, width=CANVAS_W, height=U * 54)}, [
        label("tip-tag", "#str_230051", {**absolute(left=U * TIP_LEFT, top=U * 2.5, width=U * TIP_WIDTH, height=U * 9),
                                         **typeface("lowpixel", TIP_SIZE, U * 9, rgb(OBJECTIVE_HEAD)), "white-space": keyword("nowrap")}),
        tip_slot("tip-a"), tip_slot("tip-b"),
    ])
    for slot in ("a", "b"):
        doc.bind(f"tip-{slot}.text", f"tip-{slot}", "text", {"state": f"loading_tip_{slot}"})
    doc.bind("tips.display", "tips", "display", {"op": "select", "args": [
        {"op": "||", "args": [{"op": "||", "args": [{"state": "loading_mp"}, {"state": "loading_ready"}]}, {"op": "&&", "args": [
            {"op": "==", "args": [{"state": "loading_tip_a"}, ""]}, {"op": "==", "args": [{"state": "loading_tip_b"}, ""]}]}]},
        "none", "block"]})
    shown, hidden = colour([1, 1, 1, TIP_ALPHA]), colour([1, 1, 1, 0])
    doc.timelines.add("tipA", 250, [track("tip-a", "color", [(0, hidden), (250, shown)]),
                                    track("tip-b", "color", [(0, shown), (250, hidden)])])
    doc.timelines.add("tipB", 250, [track("tip-b", "color", [(0, hidden), (250, shown)]),
                                    track("tip-a", "color", [(0, shown), (250, hidden)])])
    # The intro art (e3_load) has the + grid painted in, so the stock intro
    # screen draws none over it; a second grid at another pitch read as mess.
    grid = vector("load-grid", {**FULL, "display": keyword("block")}, [grid_path()])
    doc.bind("load-grid.display", "load-grid", "display", {"op": "select", "args": [{"state": "loading_intro"}, "none", "block"]})
    # A multiplayer join that ends at the Welcome card hands the screen over
    # (section 14.17): the session keeps drawing it, reading JOINING, until
    # the card presents, then plays handoff, the whole screen fading over
    # 250 ms (the runtime's 80 ms under reduced motion) while the card's
    # softening ramps in below. Each load presents it whole again.
    doc.timelines.add("handoff", 250, [track("screen", "opacity", [(0, number(1)), (250, number(0))])])
    doc.timelines.add("present", 1, [track("screen", "opacity", [(0, number(1)), (1, number(1))])])
    doc.events["handoff"] = [{"op": "playTimeline", "timeline": "handoff"}]
    doc.events["present"] = [{"op": "playTimeline", "timeline": "present"}]
    root = group("screen", {**FULL, "background-color": colour([0, 0, 0, 1]), "font-family": font("marine"),
                            "font-size": length(16), "color": colour([1, 1, 1, 0.8]), "opacity": number(1)}, [
        shot,
        grid,
        top_band, bottom_band, brackets, dot_matrix, identity, objectives, server, arsenal, tips, progress,
    ])
    result = doc.build(root)
    result["canvas"]["minWidth"] = CANVAS_W
    return result


HEADER = """// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Generated by tools/ui/build_retained_screens.py from docs/dev/ui-visual-design.md;
// regenerate instead of editing. Presented only while ui_retained is 1.
"""


def encode(value, depth: int = 0) -> str:
    """Structured JSON with every small object or array on one line, so the
    generated sources stay reviewable without tens of thousands of lines."""
    flat = json.dumps(value, ensure_ascii=False)
    if len(flat) <= 110 or not isinstance(value, (dict, list)) or not value:
        return flat
    pad, inner = " " * depth, " " * (depth + 1)
    if isinstance(value, dict):
        items = [f"{inner}{json.dumps(key, ensure_ascii=False)}: {encode(item, depth + 1)}" for key, item in value.items()]
        return "{\n" + ",\n".join(items) + "\n" + pad + "}"
    items = [f"{inner}{encode(item, depth + 1)}" for item in value]
    return "[\n" + ",\n".join(items) + "\n" + pad + "]"


def render(document: dict) -> str:
    text_value = HEADER + encode(document) + "\n"
    return text_value.replace("\r\n", "\n").replace("\n", "\r\n")


NOTICE_DOCUMENTS = ("title", "pause", "singleplayer", "campaigns", "skill", "loadgame", "savegame", "restartlevel", "demos",
                    "restartmenu", "credits", "arena", "multiplayer", "createserver", "joingame", "arena_pause", "settings")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when a generated document differs from its source")
    args = parser.parse_args()
    # The multiplayer menus and the retained pages live in their own
    # modules, which build on this one.
    from retained_mp_menus import escape_document, welcome_document
    from retained_mp_summary import summary_document
    from retained_saves import STROGG, load_game_document, save_game_document
    from retained_difficulty import restart_level_document
    from retained_demos import demos_document
    from retained_death import restart_menu_document
    from retained_credits import credits_document
    from retained_arena import arena_document
    from retained_mp_page import multiplayer_document
    from retained_create_server import create_server_document
    from retained_join_game import join_game_document
    from retained_arena_pause import arena_pause_document
    from retained_notices import with_notices
    from retained_audio import audio_document
    from retained_game import game_document
    from retained_controls import controls_document
    from retained_settings_page import settings_page_document
    from retained_system import system_document
    documents = {"title": title_document(), "pause": pause_document(), "pause_strogg": pause_document(strogg=True),
                 "loading": loading_document(),
                 "singleplayer": campaign_document(False), "campaigns": campaign_document(True),
                 "skill": retained_menu_levels.skill_document(types.SimpleNamespace(**globals())),
                 "chapters": retained_menu_levels.chapters_document(types.SimpleNamespace(**globals())),
                 "loadgame": load_game_document(), "savegame": save_game_document(), "restartlevel": restart_level_document(),
                 "loadgame_strogg": load_game_document(STROGG), "savegame_strogg": save_game_document(STROGG),
                 "restartlevel_strogg": restart_level_document(STROGG),
                 "demos": demos_document(),
                 "restartmenu": restart_menu_document(),
                 "credits": credits_document(),
                 "arena": arena_document(),
                 "multiplayer": multiplayer_document(),
                 "createserver": create_server_document(),
                 "joingame": join_game_document(),
                 "mp_escape": escape_document(), "mp_welcome": welcome_document(), "mp_summary": summary_document(),
                 "arena_pause": arena_pause_document(), "audio": audio_document(),
                 "game": game_document(), "controls": controls_document(), "system": system_document(),
                 "settings": settings_page_document()}
    # The session's notices (section 13.13) over every retained menu but the
    # loading screen, the Strogg family's pages and the multiplayer cards.
    for name in NOTICE_DOCUMENTS:
        documents[name] = with_notices(documents[name])
    for document in documents.values():
        safe_frame(document)
        check_text_bindings(document)
    stale = []
    for name, document in documents.items():
        output = OUTPUTS[name]
        rendered = render(document)
        if args.check:
            # Git normalizes these text files, so a checkout may hold either
            # line ending; compare the content, not the convention.
            current = output.read_bytes().decode("utf-8").replace("\r\n", "\n") if output.exists() else None
            if current != rendered.replace("\r\n", "\n"):
                stale.append(str(output.relative_to(ROOT)))
            continue
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(rendered.encode("utf-8"))
        print(f"wrote {output.relative_to(ROOT)} ({len(rendered)} bytes)")
    if stale:
        print("stale generated documents: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
