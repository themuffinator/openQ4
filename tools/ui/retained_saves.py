"""The retained Load Game page (docs/dev/ui-visual-design.md sections 9 and 13.7).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The title's and the Marine pause's LOAD GAME hand
off to it as to a stock page: the home docks its bands and carries the label
into the title slot, and at 550 ms, when the stock page would appear, the
session presents this document in the page state (idSessionLocal::
BeginRetainedPage). The page keeps the stock composition of section 9: the
selected save's preview, 183x137 u at 25,78 u in a 1 u #414624 border, with
its date and time below it on the leading side, then the difficulty and the
run's play time the save records (none for a save from an earlier build, in
the Demos page's three detail lines); LOAD GAME and DELETE GAME as
section navigation plates under them; the saves on the trailing side under
Lowpixel SAVE NAME and LOCATION headings and an olive rule; the prompt bar in
the bottom band, its BACK where the stock BACK plate stood at 532,441 u.

The session publishes the saves newest first. The list holds ROWS rows and
pages through more saves, so the document stays the same size however many
saves there are. A row's first Accept selects its save (the preview and the
date follow); Accept on the selected save, or LOAD GAME, loads it. DELETE GAME
asks with the stock confirmation, and the save that takes the deleted one's
place is selected. With no saves the list is the empty state of section 13.13:
a heading, one sentence and an action that fixes it: NEW GAME at the title,
or CLEAR FILTERS while a filter hides every save. A save that cannot be
loaded or deleted says why over the foot of its preview, until another save
is chosen.

The page is the stock page's content alone. A stock page lives in the main
menu over its home's backdrop and docked bands, so the session keeps drawing
the home that handed off (the title's montage, or the pause over the softened
game) behind the page: its departed state is the page's frame, and the label
it carried into the title slot is the page's title. Back returns the home with
the bands, as from a stock page.

The Strogg pause hands off to the same pages in the Strogg family
(loadgame_strogg.q4ui, savegame_strogg.q4ui and, from retained_difficulty.py,
restartlevel_strogg.q4ui): the construction stays, the plates end in the
Strogg pause's 30-degree shoulders with slanted markers, and the rails,
rules, frames and row light take its #F59512 and #FF9000. Labels and headings
are R_Strogg in #FCFFC8, turning #FFCC00, and the saves' names stay Lowpixel.
The confirmations take the Strogg family too, as the Strogg pause's own do
(confirmation's `strogg`).
"""
from __future__ import annotations

import re
from dataclasses import dataclass

from build_retained_screens import fit_narrow
from build_retained_screens import (CANVAS_W, CLOSE_W, FULL, LANGUAGES, MARKER, OLIVE, ORANGE, PICTURE, RAIL_DARK, ST_FILL, ST_LINE,
                                    ST_OBJ_FRAME, PAGE_BAR_LEFT, ST_READ, ST_SELECT, STRINGS, U, VERB_DELETE, VERB_LOAD, VERB_SAVE,
                                    Document, absolute, action_plate, colour, confirmation, fixed_text, font, group, keyword, label,
                                    length,
                                    linear, localized, marker_path, number, objectives_scrollbar, page_prompts, path, picture, rgb,
                                    row_prompt, solid, stroke, text_width, track, transform, typeface, vector)

ROWS = 64                      # RETAINED_LOAD_ROWS in the session: the saves the list holds at a time
PLACEHOLDER = "#str_230030"    # bound labels' text before their first binding
ERROR = "#E46D56"              # a refusal's reason (section 14.18)
HEADING = [0.72, 0.76, 0.62, 1]  # text.heading
ROW_LIGHT = "#FF9C00"          # bg_line, bg_hover and bg_focus (Appendix B.1)

# Section 9 "Save and load", measured from the stock page (source units).
PREVIEW = (25.0, 78.0, 183.0, 137.0)       # the picture, its 1 u border included
ERROR_STRIP = (26.0, 177.0, 181.0, 37.0)   # load_mapshotbtm: the foot of the picture
ERROR_LINE = 17.0                          # dp: three lines of the reason fit the strip
DATE_TOP, SKILL_TOP, PLAYED_TOP, DETAIL_LEFT, DETAIL_W = 215.0, 230.0, 245.0, 37.0, 165.0
TIME_LEFT = 91.0                           # u into the detail: the date's tabular digits, then a space
DETAIL_TEXT = 18.0                         # dp: the detail lines' Lowpixel
STAMPS = ("00/00/0000", "00:00pm")         # Sys_TimeStampToStr's widest date and time (digits are tabular)
LONGEST_PLAY = "999:59:59"                 # the play time a detail line holds
SKILLS = ("#str_200014", "#str_200015", "#str_200016", "#str_200017", "#str_41117")  # Session_SkillName's difficulties
PLAYED = "#str_231525"                     # "%s played": a save's play time (Session_RetainedSaveDetails)
PLATES = (("load_go", "#str_200001", 21.0, 256.0, "save"), ("load_delete", "#str_200021", 20.0, 293.0, None))
LIST_LEFT, LIST_TOP, LIST_W, LIST_H = 217.0, 78.0, 410.0, 325.0
ROW_W, ROW_H = 394.0, 20.0                 # tabstops 0, 180 and 394: the scrollbar takes the last 16 u
LOCATION_X = 180.0
HEAD_TOP = 63.0
RULE_Y = 77.6                              # horiz_line2's two centre texels, drawn at 75 u by 6 u
PLATE_PITCH = 30.0                         # a section plate's target: the navigation pitch (section 3)
PLATE_FADE = 208 * 0.94                    # u: where a section plate's fill and rail are gone


@dataclass(frozen=True)
class Family:
    """The pages' family: the Marine pages, or the Strogg pages the Strogg
    pause hands off to (section 13.7). `face` sets the plates, headings and
    pager labels, `read` their text at rest, `hot` focus and the pointer,
    `rail` the plates' rails, `marker` the markers at rest, `frame` the
    picture's border, `rule` the list's rule and scrollbar, `light` the rows'
    light; `strogg` gives the plates their shoulders and slanted markers."""
    suffix: str
    face: str
    read: tuple
    hot: str
    rail: str
    marker: str
    frame: str
    rule: str
    light: str
    strogg: bool

    def tint(self, alpha: float) -> list[float]:
        """The family's text colour at `alpha`: white, or #FCFFC8."""
        return rgb(ST_READ, alpha) if self.strogg else [1, 1, 1, alpha]

    def mark(self, ident: str, x: float, y: float, size: float, tint: list[float]) -> dict:
        """A marker in the `size` square at x,y: the Marine upper-trailing
        triangle, or the Strogg parallelogram slanted as the atlas draws it."""
        if not self.strogg:
            return marker_path(ident, x, y, size, tint)
        lean = round(size * 0.267, 4)
        return path(ident, [(x + lean, y), (x + size, y), (x + size - lean, y + size), (x, y + size)], fill=solid(tint))

    def prompts(self) -> dict:
        """page_prompts' arguments for the family's prompt bar."""
        if not self.strogg:
            return {}
        return {"cap_tint": rgb(ST_SELECT, 0.85), "verb_face": ("r_strogg", 14, rgb(ST_READ, 0.8)), "hot": ST_SELECT}


MARINE = Family("", "marine", (1, 1, 1, 0.8), ORANGE, RAIL_DARK, MARKER, PICTURE, OLIVE, "#FF9C00", False)
STROGG = Family("_strogg", "r_strogg", tuple(rgb(ST_READ, 0.8)), ST_SELECT, ST_LINE, ST_LINE, ST_LINE, ST_OBJ_FRAME, ST_FILL, True)
STROGG_SHOULDER = 1.732                    # a 30-degree shoulder runs this far along per unit it falls


def section_plate(doc: Document, ident: str, key: str, left: float, top: float, *, action: str | None = None,
                  event: str | None = None, label_w: float = 186, selected: dict | None = None,
                  family: Family = MARINE) -> dict:
    """A section navigation plate (b2_dark drawn at 208x52 u, Appendix B.1):
    its band rows 16-45 of 64 (13-37.4 u), a lower-leading cut from row 32,
    the #CCCC51 rail along the leading edge, the cut and the bottom, a black
    fill opaque to 57 % of the width, half at 72 % and gone by 94 %, drawn at
    0.40; the corner marker 8,19 u into the rectangle and the Marine label
    (textscale 0.31, 22.3 dp) from 18 u. Hover and focus light the plate to
    0.80 and turn the marker and label orange, as on the home plates. The
    label is `label_w` u wide, past the plate's faded end where it may run.
    With `selected`, an expression, the plate lights while it holds the
    choice, as the stock New Game page lights its difficulty: the plate at
    full strength and its label orange, whatever its feedback.
    The control is the plate's target (section 3): its 30 u pitch centred on
    the band, from the leading rail to where the plate fades out or the label
    ends in the widest language. The art around it never takes the pointer,
    so a plate takes no hits over the next plate on its pitch or the list
    beside it. The Strogg `family` trades the cut for a 30-degree shoulder
    7 u tall, the corner marker for the slanted one and the label for
    R_Strogg in #FCFFC8, turning #FFCC00."""
    width, height = U * 208, U * 52
    band_top, band_bottom, cut = U * 13.0, U * 37.4, U * 11.4
    cut_x, cut_y = (round(U * 7 * STROGG_SHOULDER, 4), U * 7) if family.strogg else (cut, cut)
    hot, read = family.hot, list(family.read)
    fade = [(0, [0, 0, 0, 1]), (0.57, [0, 0, 0, 1]), (0.72, [0, 0, 0, 0.5]), (0.94, [0, 0, 0, 0]), (1, [0, 0, 0, 0])]
    tint = family.rail
    rail = [(0, rgb(tint)), (0.57, rgb(tint)), (0.72, rgb(tint, 0.5)), (0.94, rgb(tint, 0)), (1, rgb(tint, 0))]
    def plate_paths() -> list:
        return [
            path("fill", [(0, band_top), (width, band_top), (width, band_bottom), (cut_x, band_bottom), (0, band_bottom - cut_y)],
                 fill=linear((0, 0), (width, 0), fade)),
            path("rail", [(0.75, band_top), (0.75, band_bottom - cut_y), (cut_x, band_bottom - 0.75), (width, band_bottom - 0.75)],
                 closed=False, stroke=stroke(linear((0, 0), (width, 0), rail), 1.5)),
        ]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(0.4)}, plate_paths())
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(0)}, [
        path("inset", [(cut_x + 2, band_bottom - 3.2), (width * 0.72, band_bottom - 3.2)], closed=False, stroke=stroke(solid(rgb(hot)), 1.2))])
    marker_rest = vector(f"{ident}-marker", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(0.4)}, [
        family.mark("mark", U * 10.75, U * 21.75, U * 5.5, rgb(family.marker))])
    marker_hot = vector(f"{ident}-marker-hot", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(0)}, [
        family.mark("mark", U * 10.75, U * 21.75, U * 5.5, rgb(hot))])
    text_box = absolute(left=U * 18, top=band_top, width=U * label_w, height=band_bottom - band_top)
    text_node = label(f"{ident}-label", key, {**text_box, **typeface(family.face, 22.3, band_bottom - band_top, read),
                                              "white-space": keyword("nowrap"), "transform": transform()})
    parts = [plate, focus, marker_rest, marker_hot, text_node]
    lit, rest = number(1), number(0)
    states = {
        "default": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                    (f"{ident}-marker-hot", "opacity", rest), (f"{ident}-focus", "opacity", rest),
                    (f"{ident}-label", "color", colour(read)), (f"{ident}-label", "transform", transform())],
        "hover": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", rest),
                  (f"{ident}-marker-hot", "opacity", lit), (f"{ident}-focus", "opacity", rest),
                  (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "focus": [(f"{ident}-plate", "opacity", number(0.8)), (f"{ident}-marker", "opacity", rest),
                  (f"{ident}-marker-hot", "opacity", lit), (f"{ident}-focus", "opacity", lit),
                  (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(sx=1.03, sy=1.03))],
        "pressed": [(f"{ident}-plate", "opacity", number(1)), (f"{ident}-marker", "opacity", rest),
                    (f"{ident}-marker-hot", "opacity", lit), (f"{ident}-focus", "opacity", lit),
                    (f"{ident}-label", "color", colour(rgb(hot))), (f"{ident}-label", "transform", transform(tx=1, ty=1, sx=1.03, sy=1.03))],
        "disabled": [(f"{ident}-plate", "opacity", number(0.4)), (f"{ident}-marker", "opacity", number(0.4)),
                     (f"{ident}-marker-hot", "opacity", rest), (f"{ident}-focus", "opacity", rest),
                     (f"{ident}-label", "color", colour(family.tint(0.3))), (f"{ident}-label", "transform", transform())],
    }
    if selected is not None:
        # The lit plate and label stand in while the plate holds the choice;
        # the lit label follows the label's feedback motion.
        plate_lit = vector(f"{ident}-plate-lit", {**absolute(left=0, top=0, width=width, height=height), "opacity": number(1),
                                                  "display": keyword("none"), "pointer-events": keyword("none")}, plate_paths())
        text_lit = label(f"{ident}-label-lit", key, {**text_box, **typeface(family.face, 22.3, band_bottom - band_top, rgb(hot)),
                                                     "white-space": keyword("nowrap"), "transform": transform(), "display": keyword("none")})
        text_node["properties"]["display"] = keyword("block")
        doc.bind(f"{ident}-plate-lit.display", f"{ident}-plate-lit", "display", {"op": "select", "args": [selected, "block", "none"]})
        doc.bind(f"{ident}-label-lit.display", f"{ident}-label-lit", "display", {"op": "select", "args": [selected, "block", "none"]})
        doc.bind(f"{ident}-label.display", f"{ident}-label", "display", {"op": "select", "args": [selected, "none", "block"]})
        parts = [plate, plate_lit, focus, marker_rest, marker_hot, text_node, text_lit]
        for state, tracks in states.items():
            moved = next(value for node, prop, value in tracks if node == f"{ident}-label" and prop == "transform")
            tracks.append((f"{ident}-label-lit", "transform", moved))
    ids = doc.states(ident, states)
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    inset = (band_top + band_bottom - U * PLATE_PITCH) / 2
    reach = max(U * PLATE_FADE, U * 18 + max(text_width(family.face, text, 22.3) for text in table_text(key).values()))
    art = group(f"{ident}-art", {**absolute(left=0, top=-inset, width=width, height=height), "pointer-events": keyword("none")}, parts)
    return group(ident, absolute(left=U * left, top=U * top + inset, width=reach, height=U * PLATE_PITCH), [art], control=control)


def row_light(alpha: float, light: str = ROW_LIGHT) -> list:
    """bg_line's fill across a row: faded over its leading 5 % and its
    trailing 13 % (Appendix B.1)."""
    return [(0, rgb(light, 0)), (0.05, rgb(light, alpha)), (0.87, rgb(light, alpha)), (1, rgb(light, 0))]


def row_plates(ident: str, width: float, height: float, family: Family = MARINE, selectable: bool = False) -> list:
    """A list row's backgrounds (bg_line, bg_hover and bg_focus, Appendix B.1):
    the light over rows 3-60 of 64, #FF9C00 at 0.01 at rest, 0.16 under the
    pointer or focus and 0.41 while the row's save is selected, with the
    white 0.50 rule at row 62 and its ends faded the same way. The focused row
    adds the orange inset of the home plates along its leading edge and foot.
    The Strogg family lights its rows #FF9000, ruled in #FCFFC8. A
    `selectable` row's hover band gives way to its selection (bind_selected)."""
    top, bottom, rule = height * 3 / 64, height * 61 / 64, height * 62.5 / 64
    band = [(0, top), ({"fraction": 1}, top), ({"fraction": 1}, bottom), (0, bottom)]
    whole = {**absolute(left=0, top=0, width=width, height=height)}
    ruled = family.tint
    rule_paint = linear((0, 0), ({"fraction": 1}, 0), [(0, ruled(0)), (0.05, ruled(0.5)), (0.87, ruled(0.5)), (1, ruled(0))])
    return [
        vector(f"{ident}-line", {**whole, "pointer-events": keyword("none")}, [
            path("fill", band, fill=linear((0, 0), ({"fraction": 1}, 0), row_light(0.01, family.light))),
            path("rule", [(0, rule), ({"fraction": 1}, rule)], closed=False, stroke=stroke(rule_paint, 1))]),
        vector(f"{ident}-hover", {**whole, **({"display": keyword("block")} if selectable else {}), "opacity": number(0),
                                  "pointer-events": keyword("none")}, [
            path("fill", band, fill=linear((0, 0), ({"fraction": 1}, 0), row_light(0.16, family.light)))]),
        vector(f"{ident}-selected", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
            path("fill", band, fill=linear((0, 0), ({"fraction": 1}, 0), row_light(0.41, family.light)))]),
        vector(f"{ident}-focus", {**whole, "opacity": number(0), "pointer-events": keyword("none")}, [
            path("inset", [(0.75, top + 1), (0.75, bottom - 1.5), (width * 0.87, bottom - 1.5)], closed=False,
                 stroke=stroke(solid(rgb(family.hot)), 1.2))]),
    ]


def grow_rows(doc: Document, column: dict) -> None:
    """A list's rows follow the text size (section 2's reflow): above 100 %
    each row is its 100 % height times the text size, its cells, lights and
    rule with it, so the enlarged text keeps its line in its row and the list
    scrolls through fewer rows at a time. A pager row's marker keeps its
    size, centred in the grown row. Every binding gives the authored value at
    100 % (check_text_bindings)."""
    from build_retained_screens import TEXT, in_fractions, text_source, text_sum, text_times
    text_source(doc)
    for row in column["children"]:
        height = row["properties"]["height"]["value"]
        doc.bind(f"text.{row['id']}.height", row["id"], "height", text_times(height))
        for child in row.get("children", []):
            props = child.get("properties", {})
            found = props.get("height") or {}
            if found.get("unit") != "dp" or found.get("value") != height:
                continue
            if child["id"].endswith("-marker"):
                doc.bind(f"text.{child['id']}.top", child["id"], "top",
                         text_sum(props["top"]["value"], {"op": "*", "args": [height / 2, {"op": "-", "args": [TEXT, 1]}]}))
                continue
            if child.get("type") == "vector":
                # The lights and the rule are drawn in fractions of the row.
                in_fractions(child, props["width"]["value"], height)
            elif child.get("type") == "text" and "row-" in child["id"]:
                # A name or map past its column ends in an ellipsis.
                props["text-overflow"] = keyword("clip")
                doc.bind(f"text.{child['id']}.text-overflow", child["id"], "text-overflow",
                         {"op": "select", "args": [{"op": ">", "args": [TEXT, 1]}, "ellipsis", "clip"]})
            doc.bind(f"text.{child['id']}.height", child["id"], "height", text_times(height))


def grow_details(doc: Document, parts: list, shot: str, error: str, lines: tuple) -> None:
    """The selected item's detail lines under its picture follow the text
    size, as the pause's level block's do. `lines` are the lines at 100 %,
    each a tuple of the ids that share it (Load's date and time); above it
    every id stands on a line of its own at the lines' pitch times the text
    size, from the leading edge across the detail column, ending in an
    ellipsis where a value runs past it, and the lines' foot stays over the
    section plates. The picture gives them its room, shrinking about its top
    leading corner, its aspect kept, to end where the first line starts; the
    error strip stays on its foot and grows upward with its lines. Every
    binding gives the authored value at 100 % (check_text_bindings). `parts`
    holds those nodes."""
    from build_retained_screens import LARGER, text_source, text_sum, text_times
    text_source(doc)
    nodes = {node["id"]: node for part in parts for node in walk(part)}

    def prop(ident: str, key: str) -> float:
        return nodes[ident]["properties"][key]["value"]

    def larger(grown, authored):
        return {"op": "select", "args": [LARGER, grown, authored]}
    stacked = [ident for line in lines for ident in line]
    pitch = prop(lines[1][0], "top") - prop(lines[0][0], "top")
    foot = max(prop(ident, "top") + prop(ident, "height") for ident in stacked)
    left = min(prop(ident, "left") for ident in stacked)
    width = max(prop(ident, "left") + prop(ident, "width") for ident in stacked) - left
    count = len(stacked)
    for index, ident in enumerate(stacked):
        height = prop(ident, "height")
        # Line `index` of `count` above the foot, at the pitch the text sets.
        doc.bind(f"text.{ident}.top", ident, "top", larger(text_sum(foot, text_times(-(height + (count - 1 - index) * pitch))),
                                                           prop(ident, "top")))
        doc.bind(f"text.{ident}.height", ident, "height", text_times(height))
        if prop(ident, "left") != left:
            doc.bind(f"text.{ident}.left", ident, "left", larger(left, prop(ident, "left")))
        if prop(ident, "width") != width:
            doc.bind(f"text.{ident}.width", ident, "width", larger(width, prop(ident, "width")))
        doc.bind(f"text.{ident}.text-overflow", ident, "text-overflow", larger("ellipsis", "clip"))
        nodes[ident]["properties"]["text-overflow"] = keyword("clip")
    # The first line's top above 100 %, where the picture ends.
    first = text_sum(foot, text_times(-(prop(stacked[0], "height") + (count - 1) * pitch)))
    shot_top, shot_h = prop(f"{shot}-back", "top"), prop(f"{shot}-back", "height")
    height = {"op": "max", "args": [0, text_sum(-shot_top, first)]}
    scale = {"op": "/", "args": [height, shot_h]}
    for ident in (f"{shot}-back", shot, f"{shot}-border"):
        inset = shot_h - prop(ident, "height")          # the picture inside the frame's 1 u border
        doc.bind(f"text.{ident}.height", ident, "height",
                 larger({"op": "max", "args": [0, text_sum(-inset, height)]}, prop(ident, "height")))
        doc.bind(f"text.{ident}.width", ident, "width",
                 larger({"op": "max", "args": [0, text_sum(-inset, {"op": "*", "args": [prop(f"{shot}-back", "width"), scale]})]},
                        prop(ident, "width")))
    strip, reason = nodes[error]["properties"], nodes[f"{error}-text"]["properties"]
    strip_h = strip["height"]["value"]
    doc.bind(f"text.{error}.top", error, "top", larger(text_sum(shot_top, height, text_times(-strip_h)), strip["top"]["value"]))
    doc.bind(f"text.{error}.height", error, "height", text_times(strip_h))
    doc.bind(f"text.{error}-text.height", f"{error}-text", "height", text_times(reason["height"]["value"]))


def grow_empty(doc: Document, empty: dict, keys: tuple, actions: tuple) -> None:
    """An empty state's sentence (`{empty}-text`, one of `keys`) follows the
    text size: its box takes the lines the running language's longest
    sentence needs at the running size, never less than its authored box, and
    its action plates (`actions`) move down by what it grew; its heading and
    the plates' labels are furniture and keep their size."""
    from build_retained_screens import (TEXT, fixed_text, per_language, text_buckets, text_source, text_sum, text_times,
                                        wrapped_lines)
    text_source(doc)
    nodes = {node["id"]: node for node in walk(empty)}
    sentence = nodes[f"{empty['id']}-text"]["properties"]
    face, size, line = sentence["font-family"]["value"], sentence["font-size"]["value"], sentence["line-height"]["value"]
    authored = sentence["height"]["value"]
    counts = wrapped_lines(keys, face, size, sentence["width"]["value"])
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    larger = {"op": ">", "args": [TEXT, 1]}
    needed = {"op": "max", "args": [authored, text_times({"op": "*", "args": [lines, line]})]}
    grown = {"op": "select", "args": [larger, needed, authored]}
    doc.bind(f"text.{empty['id']}-text.height", f"{empty['id']}-text", "height", grown)
    for ident in actions:
        top = nodes[ident]["properties"]["top"]["value"]
        doc.bind(f"text.{ident}.top", ident, "top", {"op": "select", "args": [larger, text_sum(top - authored, needed), top]})
    fixed_text(doc, nodes[f"{empty['id']}-heading"])
    for ident in actions:
        fixed_text(doc, nodes[ident])
    height = empty["properties"]["height"]["value"]
    doc.bind(f"text.{empty['id']}.height", empty["id"], "height", {"op": "select", "args": [larger, text_sum(height - authored, needed),
                                                                                               height]})


def bind_selected(doc: Document, ident: str, chosen: dict) -> None:
    """The row's selected band while `chosen` holds, in place of its hover
    band: a row draws one band, the selection's first (Appendix B.1), so that
    the focused row's orange text stays on the selection's light rather than
    on the two bands together."""
    doc.bind(f"{ident}-selected.display", f"{ident}-selected", "display", {"op": "select", "args": [chosen, "block", "none"]})
    doc.bind(f"{ident}-hover.display", f"{ident}-hover", "display", {"op": "select", "args": [chosen, "none", "block"]})


def row_states(doc: Document, ident: str, text_nodes: tuple, rest_colours: tuple, family: Family = MARINE) -> dict:
    """A row's feedback: the pointer lights it, focus lights it and turns its
    first column orange (#FFCC00 for the Strogg), and a disabled row dims."""
    lit, rest = number(1), number(0)
    def text(colour_value, first_hot=False):
        return [(node, "color", colour(rgb(family.hot)) if first_hot and index == 0 else colour(colour_value[index]))
                for index, node in enumerate(text_nodes)]
    hot = [family.tint(1) for _ in text_nodes]
    return doc.states(ident, {
        "default": [(f"{ident}-hover", "opacity", rest), (f"{ident}-focus", "opacity", rest)] + text(rest_colours),
        "hover": [(f"{ident}-hover", "opacity", lit), (f"{ident}-focus", "opacity", rest)] + text(hot),
        "focus": [(f"{ident}-hover", "opacity", lit), (f"{ident}-focus", "opacity", lit)] + text(rest_colours, True),
        "pressed": [(f"{ident}-hover", "opacity", lit), (f"{ident}-focus", "opacity", lit)] + text(rest_colours, True),
        "disabled": [(f"{ident}-hover", "opacity", rest), (f"{ident}-focus", "opacity", rest)] + text([family.tint(0.3)] * len(text_nodes)),
    })


NAME_TINT, LOCATION_TINT = [1, 1, 1, 0.9], [1, 1, 1, 0.65]


def save_row(doc: Document, index: int, prefix: str = "load", row_w: float = ROW_W,
             chosen: list | None = None, location_x: float = LOCATION_X, family: Family = MARINE, guarded: bool = False,
             verb: str = VERB_LOAD) -> dict:
    """One of the list's rows: the save's name in the SAVE NAME column and its
    level in the LOCATION column (tabstops 0 and 180 u), Lowpixel at the stock
    list's textscale 0.22 (15.8 dp) on its 20 u pitch. Its first Accept selects
    the save; Accept on the selected save runs `chosen` (by default it loads
    it), so the prompt bar names Accept SELECT, then `verb`. `prefix` names
    the page's list state, `row_w` its row width and `location_x` where its
    second column starts."""
    ident, key = f"row-{index}", f"{prefix}.row{index}"
    if guarded:
        doc.state[f"{key}.context"] = {"type": "string", "initial": ""}
    doc.state[f"{key}.name"] = {"type": "string", "initial": ""}
    doc.state[f"{key}.location"] = {"type": "string", "initial": ""}
    width, height = U * row_w, U * ROW_H
    name_tint, location_tint = family.tint(0.9), family.tint(0.65)
    cell = {"white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")}
    name = label(f"{ident}-name", PLACEHOLDER, {**absolute(left=U * 4, top=0, width=U * (location_x - 8), height=height),
                 **typeface("lowpixel", 15.8, height, name_tint), **cell})
    location = label(f"{ident}-location", PLACEHOLDER, {**absolute(left=U * location_x, top=0, width=U * (row_w - location_x - 4), height=height),
                     **typeface("lowpixel", 15.8, height, location_tint), **cell})
    doc.bind(f"{ident}-name.text", f"{ident}-name", "text", {"state": f"{key}.name"})
    doc.bind(f"{ident}-location.text", f"{ident}-location", "text", {"state": f"{key}.location"})
    save = {"op": "+", "args": [{"state": f"{prefix}.first"}, index]}
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [save, {"state": f"{prefix}.count"}]}, "block", "none"]})
    bind_selected(doc, ident, {"op": "==", "args": [{"state": f"{prefix}.selected"}, save]})
    event = f"{prefix}Row{index}"
    doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": f"{prefix}.selected"}, save]},
                          "then": chosen if chosen is not None else [{"op": "action", "action": "save"}],
                          "else": [{"op": "setState", "values": {f"{prefix}.pick": index}}, {"op": "action", "action": "select"}]}]
    ids = row_states(doc, ident, (f"{ident}-name", f"{ident}-location"), (name_tint, location_tint), family)
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                         "flex-shrink": number(0)},
                 [*row_plates(ident, width, height, family, selectable=True), name, location],
                 control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids,
                          **({"context": {"state": f"{key}.context"}} if guarded else {}),
                          "prompt": row_prompt(verb, {"op": "==", "args": [{"state": f"{prefix}.selected"}, save]})})


def pager_row(doc: Document, ident: str, key: str, action: str, shown: dict, row_w: float = ROW_W,
              family: Family = MARINE) -> dict:
    """A row that pages the list to the newer or older saves (more saves than
    the list holds): a row with the home plates' marker and a Marine label."""
    width, height = U * row_w, U * ROW_H
    marker = vector(f"{ident}-marker", {**absolute(left=U * 4, top=0, width=U * 8, height=height), "pointer-events": keyword("none")}, [
        family.mark("mark", 0, height / 2 - U * 3, U * 5, rgb(family.marker))])
    read = list(family.read)
    text_node = label(f"{ident}-label", key, {**absolute(left=U * 14, top=0, width=U * (row_w - 18), height=height),
                      **typeface(family.face, 16, height, read), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                      "pointer-events": keyword("none")})
    ids = row_states(doc, ident, (f"{ident}-label",), (read,), family)
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [shown, "block", "none"]})
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                         "flex-shrink": number(0)},
                 [*row_plates(ident, width, height, family), marker, text_node],
                 control={"role": "button", "label": key, "action": action, "states": ids})


def picture_frame(ident: str, left: float, top: float, width: float, height: float, frame: str = PICTURE) -> list:
    """The save's picture in the stock preview (load_mapshot): black 0.60
    behind it and a 1 u #414624 border (`frame`), the picture fitted inside."""
    box = absolute(left=U * left, top=U * top, width=U * width, height=U * height)
    inner = [(0.75, 0.75), ({"fraction": 1, "dp": -0.75}, 0.75), ({"fraction": 1, "dp": -0.75}, {"fraction": 1, "dp": -0.75}),
             (0.75, {"fraction": 1, "dp": -0.75})]
    return [
        vector(f"{ident}-back", box, [path("back", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
                                           fill=solid([0, 0, 0, 0.6]))]),
        picture(ident, "", {**absolute(left=U * (left + 1), top=U * (top + 1), width=U * (width - 2), height=U * (height - 2))}, fit="contain"),
        vector(f"{ident}-border", {**box, "pointer-events": keyword("none")}, [path("border", inner, stroke=stroke(solid(rgb(frame)), U * 1))]),
    ]


# The stock page's backing (grad2): bg_darkgrad2 drawn black at 0.60 across the
# whole view, from 11 u before the canvas's leading edge to 32 u past its
# trailing one, stretched with the view. Across its width the texture is clear
# to 18.4 %, opaque from 34 % to 91.8 % and clear again by 97.3 %.
BACKING_ALPHA = 0.6
BACKING_STOPS = ((0.184, 0.0), (0.34, 1.0), (0.918, 1.0), (0.973, 0.0))


def backing_x(f: float) -> dict:
    """A fraction of the stretched backing as a view position: the texture
    spans x = -11 - E to 672 + E u, E the view's expansion each side, so a
    fraction f lies at f of the view's width plus U (683 f - 11) - 960 f dp."""
    return {"fraction": f, "dp": round(U * (683 * f - 11) - CANVAS_W * f, 4)}


def backing(ident: str = "load-backing") -> dict:
    first, last = BACKING_STOPS[0][0], BACKING_STOPS[-1][0]
    stops = [(round((f - first) / (last - first), 4), [0, 0, 0, round(BACKING_ALPHA * alpha, 4)]) for f, alpha in BACKING_STOPS]
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    return vector(ident, {**FULL, "opacity": number(1), "pointer-events": keyword("none")}, [
        path("backing", whole, fill=linear((backing_x(first), 0), (backing_x(last), 0), stops))])


def table_text(key: str) -> dict:
    """`key` in every shipped language, as the engine resolves it: a
    language's openQ4 table, else the stock GUI table that openQ4 ships for
    every language (a few stock strings are corrected in some languages'
    openQ4 tables only)."""
    found = localized(key)
    for language in LANGUAGES:
        if language in found:
            continue
        for line in (STRINGS / f"{language}_guis.lang").read_text(encoding="utf-8").splitlines():
            match = re.match(r'\s*"(#str_\d+)"\s+"(.*)"\s*$', line)
            if match and match.group(1) == key:
                found[language] = match.group(2)
    return found


def lines_needed(face: str, text: str, size: float, width: float) -> int:
    """Lines `text` takes at `width` dp, broken at spaces as the host breaks it."""
    lines, current = 1, ""
    for word in text.split(" "):
        candidate = f"{current} {word}" if current else word
        if current and text_width(face, candidate, size) > width:
            lines, current = lines + 1, word
        else:
            current = candidate
    return lines


def newgame_width() -> float:
    """NEW GAME's plate: its label in the widest shipped language, the
    marker before it and a 20 dp margin after it, never under 180 dp."""
    widest = max(text_width("marine", text, 20) for text in table_text("#str_200000").values())
    return max(180.0, round(widest + 30 + 20))


def empty_clear(doc: Document, prefix: str, family: Family) -> dict:
    """The empty state's action row while a filter hides every save (section
    13.13): CLEAR FILTERS, the bottom band's plate, under the sentence."""
    width = 24 + max(text_width("lowpixel", text, 14) for text in table_text("#str_231422").values()) / U
    node = browser_button(doc, f"{prefix}-empty-clear", "#str_231422", "clearFilters", 0, 74, width, family)
    node["properties"]["display"] = keyword("none")
    doc.bind(f"{prefix}-empty-clear.display", f"{prefix}-empty-clear", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "browser.filter"}, 0]}, "block", "none"]})
    return node


def heading_face(family: Family) -> tuple:
    """The list headings' face and tint: Lowpixel white at 0.40, or R_Strogg
    in #FCFFC8 at 0.45, as the Strogg objectives page sets its own."""
    return ("r_strogg", rgb(ST_READ, 0.45)) if family.strogg else ("lowpixel", [1, 1, 1, 0.4])


def empty_heading_tint(family: Family) -> list[float]:
    return rgb(ST_READ) if family.strogg else HEADING


def check_text_fits(family: Family = MARINE) -> None:
    """Every shipped language's text fits its box: the plates, headings and
    pager labels on one line, the empty state's sentence on two and a reason
    on the three lines of the strip over the picture, and under the picture
    the date beside the time, a difficulty and a play time. Generation fails
    otherwise."""
    face, head = family.face, heading_face(family)[0]
    stamp_widths = [text_width("lowpixel", stamp, DETAIL_TEXT) for stamp in STAMPS]
    if stamp_widths[0] > U * TIME_LEFT - text_width("lowpixel", " ", DETAIL_TEXT) or stamp_widths[1] > U * (DETAIL_W - TIME_LEFT):
        raise SystemExit(f"a save's date and time ({stamp_widths}) do not share the line under the picture")
    for key in SKILLS + (PLAYED,):
        for language, text in table_text(key).items():
            text = text.replace("%s", LONGEST_PLAY)
            if text_width("lowpixel", text, DETAIL_TEXT) > U * DETAIL_W:
                raise SystemExit(f"{key} in {language} ({text!r}) does not fit a detail line under the picture")
    one_line = (("#str_200001", face, 22.3, U * 186), ("#str_200021", face, 22.3, U * 186),
                ("#str_200925", head, 13, U * (LOCATION_X - 4)), ("#str_200926", head, 13, U * (ROW_W - LOCATION_X)),
                ("#str_231100", face, 20, U * ROW_W), ("#str_231425", face, 16, U * (ROW_W - 18)),
                ("#str_231426", face, 16, U * (ROW_W - 18)), ("#str_200000", "marine", 20, newgame_width() - 30))
    wrapped = (("#str_231101", "lowpixel", 17, U * ROW_W, 2),) + tuple(
        (key, "lowpixel", 15, U * (ERROR_STRIP[2] - 8), 3) for key in ("#str_231102", "#str_231103", "#str_231104", "#str_231105"))
    for key, face, size, width in one_line:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        for language, text in texts.items():
            if text_width(face, text, size) > width:
                raise SystemExit(f"{key} in {language} ({text!r}) needs {text_width(face, text, size):.0f} dp of {width:.0f}")
    for key, face, size, width, allowed in wrapped:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        for language, text in texts.items():
            if lines_needed(face, text, size, width) > allowed:
                raise SystemExit(f"{key} in {language} needs {lines_needed(face, text, size, width)} lines of {allowed}")


def browser_button(doc: Document, ident: str, key: str, action: str, left: float, top: float,
                   width: float, family: Family, selected: dict | None = None, sort: bool = False) -> dict:
    """Header-band filter/action plate; family colors, cut, focus and editable symbols."""
    w, h = width * U, 22 * U
    cut = 5 * U
    shape = [(0, 0), (w, 0), (w, h), (cut, h), (0, h - cut)]
    whole = absolute(left=0, top=0, width=w, height=h)
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.28)}, [
        path("fill", shape, fill=solid([0, 0, 0, 1])),
        path("rail", [(0.75, 0), (0.75, h-cut), (cut, h-0.75), (w, h-0.75)], closed=False,
             stroke=stroke(solid(rgb(family.rule)), 1.2))])
    hot = vector(f"{ident}-hot", {**whole, "opacity": number(0)}, [path("fill", shape, fill=solid(rgb(family.rule, 0.35)))])
    text = label(f"{ident}-label", key, {**absolute(left=8*U, top=0, width=w-24*U, height=h),
                 **typeface("lowpixel", 14, h, family.tint(0.8)), "white-space": keyword("nowrap")})
    parts = [plate, hot, text]
    if selected is not None:
        doc.bind(f"{ident}.selected", f"{ident}-plate", "opacity", {"op": "select", "args": [selected, 0.60, 0.28]})
        cross = vector(f"{ident}-cross", {**whole, "display": keyword("none")}, [
            path("a", [(w-12*U, h/2-3), (w-8*U, h/2+3)], closed=False, stroke=stroke(solid(family.tint(0.8)), 1.2)),
            path("b", [(w-12*U, h/2+3), (w-8*U, h/2-3)], closed=False, stroke=stroke(solid(family.tint(0.8)), 1.2))])
        doc.bind(f"{ident}.cross", f"{ident}-cross", "display", {"op": "select", "args": [selected, "block", "none"]})
        parts.append(cross)
    if sort:
        mode = {"state": "browser.sort"}
        doc.bind(f"{ident}.label", f"{ident}-label", "text", {"op": "select", "args": [
            {"op": "<", "args": [mode, 2]}, "#str_41557", {"op": "select", "args": [
                {"op": "<", "args": [mode, 4]}, "#str_200925", "#str_200926"]}]})
        ascending = {"op": "||", "args": [{"op": "==", "args": [mode, 1]}, {"op": "||", "args": [
            {"op": "==", "args": [mode, 2]}, {"op": "==", "args": [mode, 4]}]}]}
        for name, points, condition in (
            ("up", [(w-13*U, h/2+4), (w-8*U, h/2-4), (w-8*U, h/2+4)], ascending),
            ("down", [(w-13*U, h/2-4), (w-8*U, h/2+4), (w-8*U, h/2-4)], {"op":"!", "args":[ascending]})):
            symbol=vector(f"{ident}-{name}", {**whole, "display": keyword("block")}, [path("marker", points, fill=solid(rgb(family.hot)))])
            doc.bind(f"{ident}.{name}", f"{ident}-{name}", "display", {"op":"select", "args":[condition, "block", "none"]})
            parts.append(symbol)
    ids=doc.states(ident, {state: [(f"{ident}-hot", "opacity", number(0 if state in ("default", "disabled") else 1)),
                                (f"{ident}-label", "color", colour(family.tint(0.8 if state == "default" else 0.3) if state in ("default","disabled") else rgb(family.hot)))]
                           for state in ("default", "hover", "focus", "pressed", "disabled")})
    art=group(f"{ident}-art", {**whole, "pointer-events": keyword("none")}, parts)
    return group(ident, absolute(left=U*left, top=U*top, width=w, height=h), [art],
                 control={"role":"button", "label":key, "action":action, "states":ids})


def save_browser(doc: Document, prefix: str, left: float, width: float, family: Family) -> list:
    doc.state.update({"browser.sort":{"type":"number","initial":0}, "browser.filter":{"type":"number","initial":0},
                      "browser.total":{"type":"number","initial":0}, "browser.generation":{"type":"number","initial":0},
                      "browser.context":{"type":"string","initial":""}})
    for action, command in (("refresh","saveListRefresh"), ("sort","saveListSort"), ("manual","saveListFilterManual"),
                            ("automatic","saveListFilterAuto"), ("clearFilters","saveListClear")):
        doc.session(action,command)
    doc.events["onSecondary"]=[{"op":"action","action":"refresh"}]
    doc.events["onTertiary"]=[{"op":"action","action":"sort"}]
    for row in range(ROWS):
        doc.events[f"browserFocus{row}"]=[{"op":"focus","control":f"row-{row}"}]
    doc.events["browserFocusRefresh"]=[{"op":"focus","control":"browser-refresh"}]
    for action, state in (("select", {"op":"+", "args":[{"state":f"{prefix}.first"},{"state":f"{prefix}.pick"}]}),
                          *(([("save", {"state":"load.selected"}), ("delete", {"state":"load.deleteIndex"})]) if prefix == "load" else [])):
        doc.actions[action]["arguments"].update({"index":state, "generation":{"state":"load.deleteGeneration" if action == "delete" else "browser.generation"}})
    filtered={"op":"!=", "args":[{"state":"browser.filter"},0]}
    for node, ordinary, filtered_key in ((f"{prefix}-empty-heading","#str_231100","#str_231423"),
                                          (f"{prefix}-empty-text","#str_231101","#str_231424")):
        doc.bind(f"{node}.text",node,"text",{"op":"select","args":[filtered,filtered_key,ordinary]})
    nodes=[]
    x=left
    for ident,key,action,index in (("manual","#str_231420","manual",1),("automatic","#str_231421","automatic",2)):
        w=24+max(text_width("lowpixel",text,14) for text in table_text(key).values())/U
        nodes.append(browser_button(doc,f"browser-{ident}",key,action,x,37,w,family,{"op":"==","args":[{"state":"browser.filter"},index]}))
        x+=w+4
    w=24+max(text_width("lowpixel",text,14) for key in ("#str_41557","#str_200925","#str_200926") for text in table_text(key).values())/U
    if x+w > left+width: raise SystemExit("save browser toolbar exceeds its list")
    nodes.append(browser_button(doc,"browser-sort","#str_41557","sort",x,37,w,family,sort=True))
    x=35
    for ident,key,action in (("refresh","#str_200028","refresh"),("clear","#str_231422","clearFilters")):
        w=24+max(text_width("lowpixel",text,14) for text in table_text(key).values())/U
        node=browser_button(doc,f"browser-{ident}",key,action,x,435,w,family)
        if ident == "clear":
            node["properties"]["display"] = keyword("none")
            doc.bind("browser-clear.display","browser-clear","display",{"op":"select","args":[filtered,"block","none"]})
        nodes.append(node)
        x+=w+8
    # The toolbar ends before the prompt bar begins, so Accept and Back stay
    # clear in the longest locale whatever verb Accept shows.
    if U * x > PAGE_BAR_LEFT: raise SystemExit("save browser toolbar reaches the prompt bar")
    return nodes


def load_game_document(family: Family = MARINE) -> dict:
    check_text_fits(family)
    doc = Document(f"openq4.loadgame{family.suffix}")
    doc.state.update({
        "load.ingame": {"type": "boolean", "initial": False},
        "load.count": {"type": "number", "initial": 0},
        "load.first": {"type": "number", "initial": 0},
        "load.selected": {"type": "number", "initial": -1},
        "load.pick": {"type": "number", "initial": -1},
        "load.shot": {"type": "string", "initial": ""},
        "load.date": {"type": "string", "initial": ""},
        "load.time": {"type": "string", "initial": ""},
        "load.skill": {"type": "string", "initial": ""},
        "load.played": {"type": "string", "initial": ""},
        "load.error": {"type": "string", "initial": ""},
        "load.context": {"type": "string", "initial": ""},
        "load.deleteIndex": {"type": "number", "initial": -1},
        "load.deleteGeneration": {"type": "number", "initial": 0},
    })
    for action, command in (("back", "loadBack"), ("select", "loadSelect"), ("save", "loadSave"), ("delete", "loadDelete"),
                            ("newer", "loadNewer"), ("older", "loadOlder"), ("newGame", "loadNewGame")):
        doc.session(action, command)
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    # The Menu button closes the menus and resumes play (section 13.5): the
    # session returns the pause, whose RESUME closes it.
    doc.session("menuResume", "menuResume")
    # At the title, with no game to resume, it goes back as Back does.
    doc.events["onMenuButton"] = [{"op": "if", "condition": {"state": "load.ingame"},
                                   "then": [{"op": "action", "action": "menuResume"}], "else": [{"op": "action", "action": "back"}]}]
    chosen = {"op": ">=", "args": [{"state": "load.selected"}, 0]}

    # The leading side: the selected save's picture, its date and time, its
    # difficulty and play time, and the section plates.
    shot_parts = picture_frame("load-shot", *PREVIEW, frame=family.frame)
    doc.bind("load-shot.image", "load-shot", "image", {"state": "load.shot"})
    error_box = absolute(left=U * ERROR_STRIP[0], top=U * ERROR_STRIP[1], width=U * ERROR_STRIP[2], height=U * ERROR_STRIP[3])
    error = group("load-error", {**error_box, "display": keyword("none"), "background-color": colour([0, 0, 0, 0.8])}, [
        label("load-error-text", PLACEHOLDER, {**absolute(left=U * 4, top=U * 1, width=U * (ERROR_STRIP[2] - 8), height=U * (ERROR_STRIP[3] - 2)),
              **typeface("lowpixel", 15, ERROR_LINE, rgb(ERROR)), "white-space": keyword("normal"), "overflow": keyword("hidden")})])
    doc.bind("load-error.display", "load-error", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "load.error"}, ""]}, "block", "none"]})
    doc.bind("load-error-text.text", "load-error-text", "text", {"state": "load.error"})
    stamp = {"white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    def detail_line(ident: str, state: str, left: float, top: float, width: float, alpha: float) -> dict:
        doc.bind(f"{ident}.text", ident, "text", {"state": state})
        return label(ident, PLACEHOLDER, {**absolute(left=U * left, top=U * top, width=U * width, height=U * 16),
                     **typeface("lowpixel", DETAIL_TEXT, U * 16, family.tint(alpha)), **stamp})
    date = detail_line("load-date", "load.date", DETAIL_LEFT, DATE_TOP, TIME_LEFT, 0.8)
    time_node = detail_line("load-time", "load.time", DETAIL_LEFT + TIME_LEFT, DATE_TOP, DETAIL_W - TIME_LEFT, 0.8)
    skill = detail_line("load-skill", "load.skill", DETAIL_LEFT, SKILL_TOP, DETAIL_W, 0.8)
    played = detail_line("load-played", "load.played", DETAIL_LEFT, PLAYED_TOP, DETAIL_W, 0.6)
    plates = []
    for ident, key, left, top, action in PLATES:
        plates.append(section_plate(doc, ident, key, left, top, action=action, event=None if action else "deleteRequested",
                                    family=family))
        plates[-1]["control"]["context"] = {"state": "load.context"}
        plates[-1]["control"]["prompt"] = VERB_DELETE if ident == "load_delete" else VERB_LOAD
        doc.bind(f"{ident}.enabled", ident, "enabled", chosen)

    # The trailing side: headings over an olive rule, then the list.
    head_face, head_tint = heading_face(family)
    heading = {**typeface(head_face, 13, U * 14, head_tint), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    headings = [
        label("load-head-name", "#str_200925", {**absolute(left=U * LIST_LEFT, top=U * HEAD_TOP, width=U * (LOCATION_X - 4), height=U * 14), **heading}),
        label("load-head-location", "#str_200926", {**absolute(left=U * (LIST_LEFT + LOCATION_X - 1), top=U * HEAD_TOP,
              width=U * (ROW_W - LOCATION_X), height=U * 14), **heading}),
        vector("load-rule", absolute(left=U * LIST_LEFT, top=U * RULE_Y - 0.6, width=U * LIST_W, height=1.2), [
            path("rule", [(0, 0.6), ({"fraction": 1}, 0.6)], closed=False, stroke=stroke(solid(rgb(family.rule, 0.26)), 1.2))]),
    ]
    rows = [save_row(doc, index, family=family, guarded=True) for index in range(ROWS)]
    newer = pager_row(doc, "page-newer", "#str_231425", "newer", {"op": ">", "args": [{"state": "load.first"}, 0]}, family=family)
    older = pager_row(doc, "page-older", "#str_231426", "older",
                      {"op": "<", "args": [{"op": "+", "args": [{"state": "load.first"}, ROWS]}, {"state": "load.count"}]},
                      family=family)
    column = group("load-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")},
                   [newer, *rows, older])
    # Above 100 % text the rows grow with it (section 2's reflow); the
    # headings, plates and filters are furniture and keep their size.
    grow_rows(doc, column)
    for node in [*headings, *plates]:
        fixed_text(doc, node)
    viewport = group("load-list", {**absolute(left=U * LIST_LEFT, top=U * LIST_TOP, width=U * ROW_W, height=U * LIST_H),
                                   "overflow": keyword("auto")}, [column])
    # The bar's trough sits in the list's last 16 u, where the stock bar ran.
    scrollbar = objectives_scrollbar(doc, "load_scroll", "load-list", family.rule, family.hot, U * (LIST_LEFT + ROW_W + 8) - 18, U * LIST_TOP,
                                     U * LIST_H, line_step=U * ROW_H, key="#str_200001")

    # No saves: the empty state of section 13.13 in the list's place; at the
    # title NEW GAME opens the Single Player page. Only the pause reaches the
    # Strogg page, so its NEW GAME never shows.
    newgame = action_plate(doc, "load_newgame", "#str_200000", 0, U * 74, action="newGame", width=newgame_width())
    newgame["properties"]["display"] = keyword("block")
    empty = group("load-empty", {**absolute(left=U * LIST_LEFT, top=U * LIST_TOP, width=U * ROW_W, height=U * 150), "display": keyword("none")}, [
        label("load-empty-heading", "#str_231100", {**absolute(left=0, top=U * 10, width=U * ROW_W, height=U * 16),
              **typeface(family.face, 20, U * 16, empty_heading_tint(family)), "white-space": keyword("nowrap")}),
        label("load-empty-text", "#str_231101", {**absolute(left=0, top=U * 32, width=U * ROW_W, height=U * 36),
              **typeface("lowpixel", 17, 22, family.tint(0.8)), "white-space": keyword("normal"), "overflow": keyword("hidden")}),
        newgame, empty_clear(doc, "load", family),
    ])
    doc.bind("load-empty.display", "load-empty", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "load.count"}, 0]}, "block", "none"]})
    grow_empty(doc, empty, ("#str_231101", "#str_231424"), ("load_newgame", "load-empty-clear"))
    grow_details(doc, [*shot_parts, error, date, time_node, skill, played], "load-shot", "load-error",
                 (("load-date", "load-time"), ("load-skill",), ("load-played",)))
    doc.bind("load_newgame.display", "load_newgame", "display", {"op": "select", "args": [
        {"op":"||", "args":[{"state":"load.ingame"},{"op":"!=", "args":[{"state":"browser.filter"},0]}]}, "none", "block"]})

    toolbar = save_browser(doc, "load", LIST_LEFT, ROW_W, family)
    for node in toolbar:
        fixed_text(doc, node)
    back = page_prompts(doc, "load_back", action="back", verbs=(VERB_LOAD, VERB_DELETE), **family.prompts())
    # The plates show at once, under the title the home carried; the panel
    # and the backing fade in over 150 ms (content.in), as a stock page's do.
    # The plates follow the panel, whose full-size box would take their hits.
    panel = group("load-panel", {**FULL, "opacity": number(1)}, [*shot_parts, error, date, time_node, skill, played, *headings, viewport, scrollbar,
                                                                  empty, back, *toolbar])
    stage = group("load-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)},
                  [panel, *plates])

    modal = confirmation(doc, "deleteModal", "#str_200021", "#str_200245", None, yes_event="deleteConfirmed",
                         strogg=family.strogg)
    doc.events["deleteRequested"] = [{"op":"setState", "values":{"load.deleteIndex":{"state":"load.selected"},
                                                                  "load.deleteGeneration":{"state":"browser.generation"}}},
                                      {"op":"call", "event":"deleteModalShow"}]
    doc.events["deleteConfirmed"] = [{"op": "call", "event": "deleteModalHide"}, {"op": "action", "action": "delete"}]

    doc.timelines.add("arrive", 150, [track(node, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))])
                                      for node in ("load-panel", "load-backing")])
    doc.timelines.add("rest", 1, [track(node, "opacity", [(0, number(0)), (1, number(0))]) for node in ("load-panel", "load-backing")])
    # Each arrival focuses the first row (the newest save, selected), else
    # NEW GAME at the title, else BACK; the last request wins.
    focus = [{"op": "focus", "control": "load_back"},
             {"op": "if", "condition": {"op": "!", "args": [{"state": "load.ingame"}]}, "then": [{"op": "focus", "control": "load_newgame"}]},
             {"op": "if", "condition": {"op": ">", "args": [{"state": "load.count"}, 0]}, "then": [{"op": "focus", "control": "row-0"}]}]
    doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"}, *focus]
    doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}]
    # Paging selects the first save of the older page or the last of the newer.
    doc.events["pagedOlder"] = [{"op": "focus", "control": "row-0"}]
    doc.events["pagedNewer"] = [{"op": "focus", "control": f"row-{ROWS - 1}"}]
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [backing(), stage, modal])
    return tall_load_page(fit_narrow(doc.build(root), pages=True, stretch=("load-list",)))


# Tall views (retained_tall): the filters under the band, then on the Load
# page the selected save's picture, its details and the LOAD GAME and DELETE
# plates beside it, then the headings and the list across the column to the
# bottom band. A row takes two lines there, 1.6 times the landscape size: the
# save's name, and under it its location. The picture and details grow as
# much, the plates 1.3 times so that they fit beside the picture. The Demos
# page builds on the same parts.
TALL_TOP = 80.0                                # dp: the filters, under the band
TALL_GROW = 1.6
TALL_PLATES = 1.3
TALL_PLATE_PITCH = 64.0                        # dp: the plates beside the picture
TALL_SAVE_FOOT = 1.2                           # the Save page's name and SAVE GAME
TALL_FOOT_BOTTOM = 72.0                        # dp: SAVE GAME, over the prompt bar
# A row's cells in a tall view, a line at a time: (cell, left, width), the
# left and width as fractions of the list's width.
SAVE_LINES = ((("name", 0.0, 1.0),), (("location", 0.0, 1.0),))


def tall_list(tall, prefix: str, lines: tuple, list_w: float, heads: dict, top: float, shift: dict | None = None,
              height: dict | None = None, head_ids: dict | None = None) -> float:
    """The list's headings from `top`, a line for each of the row's lines,
    and its rows in those lines across `list_w` dp with its scrollbar beside
    it; returns the list's top. `heads` gives each heading's landscape place
    (u) by cell, `head_ids` the headings' ids where they are not
    {prefix}-head-{cell}. `shift` moves the tops (block()), and a list whose
    viewport does not stretch with the view takes `height`."""
    from build_retained_screens import text_times
    from retained_tall import MARGIN
    line, head_line, inset = U * ROW_H * TALL_GROW, U * 14 * TALL_GROW, U * 4 * TALL_GROW
    for number, cells in enumerate(lines):
        for cell, left, width in cells:
            ident = (head_ids or {}).get(cell, f"{prefix}-head-{cell}")
            tall.block((ident,), heads[cell], (MARGIN + left * list_w, top + number * head_line), TALL_GROW, shift)
            tall.set(ident, "width", width * list_w - inset)
    rule_top = top + len(lines) * head_line + 4
    tall.bind(f"{prefix}-rule", "left", MARGIN)
    tall.top(f"{prefix}-rule", rule_top, shift)
    tall.bind(f"{prefix}-rule", "width", list_w)
    list_top = rule_top + 6
    tall.bind(f"{prefix}-list", "left", MARGIN)
    tall.top(f"{prefix}-list", list_top, shift)
    tall.bind(f"{prefix}-list", "width", list_w)
    if height is not None:
        tall.bind(f"{prefix}-list", "height", height)
        tall.bind(f"{prefix}_scroll", "height", height)
    tall.grow(f"{prefix}-column", TALL_GROW)
    for ident in [f"row-{index}" for index in range(ROWS)] + ["page-newer", "page-older"]:
        if ident not in tall.nodes:
            continue
        row = ident.startswith("row-")
        height = len(lines) * line if row else line
        # Rows that follow the text size (grow_rows) keep following it here.
        grown = (lambda length: text_times(round(length, 4))) if (ident, "height") in tall.text_sized else (lambda length: length)
        for node in (ident, f"{ident}-line", f"{ident}-hover", f"{ident}-selected", f"{ident}-focus"):
            tall.set(node, "width", list_w)
            tall.set(node, "height", grown(height))
        if not row:
            tall.set(f"{ident}-label", "width", list_w - U * 18 * TALL_GROW)
            if (f"{ident}-marker", "top") in tall.text_sized:
                # The grown marker stays centred in the grown row.
                tall.set(f"{ident}-marker", "top", {"op": "*", "args": [round(line / 2, 4),
                                                                        {"op": "-", "args": [{"state": "view.text"}, 1]}]})
            continue
        for number, cells in enumerate(lines):
            for cell, left, width in cells:
                tall.set(f"{ident}-{cell}", "left", inset + left * list_w)
                tall.set(f"{ident}-{cell}", "top", grown(number * line) if number else number * line)
                tall.set(f"{ident}-{cell}", "width", width * list_w - 2 * inset)
    tall.bind(f"{prefix}_scroll", "left", MARGIN + list_w + 4)
    tall.top(f"{prefix}_scroll", list_top, shift)
    return list_top


def tall_filters(tall, ids: tuple, origin: tuple, room: float) -> None:
    """The list's filter, sort or tag plates under the band, grown as the
    rows where they fit across `room` dp, which their landscape width in u
    (`origin`, `span`) tells."""
    from retained_tall import MARGIN
    tall.block(ids, (U * origin[0], U * origin[1]), (MARGIN, TALL_TOP), min(TALL_GROW, room / (U * origin[2])))


def tall_detail(tall, prefix: str, picture: tuple, details: tuple, detail_origin: tuple, plates: tuple) -> float:
    """The selected item's picture with its frame and error strip at the
    column's start, its detail lines and section plates beside it; returns
    where the picture ends."""
    from retained_tall import MARGIN
    picture_top = TALL_TOP + 70
    tall.block((f"{prefix}-shot-back", f"{prefix}-shot", f"{prefix}-shot-border", f"{prefix}-error"), (U * picture[0], U * picture[1]),
               (MARGIN, picture_top), TALL_GROW)
    beside = MARGIN + U * picture[2] * TALL_GROW + 24
    factor = TALL_GROW * 0.97
    tall.block(details, (U * detail_origin[0], U * detail_origin[1]), (beside + 12, picture_top + 6), factor)
    if (details[0], "top") in tall.text_sized:
        # Lines that follow the text size in other views (grow_details) keep
        # the tall view's own size here, beside the picture, as the settings
        # rows do: the plates under them leave no room to grow.
        from build_retained_screens import TEXT
        def kept(ident: str, key: str, value: float) -> None:
            tall.set(ident, key, value)
        def lettered(ident: str, scale: float = factor) -> None:
            for key in ("font-size", "line-height"):
                tall.set(ident, key, {"op": "/", "args": [round(tall.dp(ident, key) * scale, 4), TEXT]})
        for ident in details:
            kept(ident, "top", picture_top + 6 + (tall.dp(ident, "top") - U * detail_origin[1]) * factor)
            kept(ident, "left", beside + 12 + (tall.dp(ident, "left") - U * detail_origin[0]) * factor)
            for key in ("width", "height"):
                kept(ident, key, tall.dp(ident, key) * factor)
            lettered(ident)
        for ident in (f"{prefix}-shot-back", f"{prefix}-shot", f"{prefix}-shot-border"):
            for key in ("width", "height"):
                kept(ident, key, tall.dp(ident, key) * TALL_GROW)
        error = f"{prefix}-error"
        kept(error, "top", picture_top + (tall.dp(error, "top") - U * picture[1]) * TALL_GROW)
        kept(error, "height", tall.dp(error, "height") * TALL_GROW)
        kept(f"{error}-text", "height", tall.dp(f"{error}-text", "height") * TALL_GROW)
        lettered(f"{error}-text", TALL_GROW)
    for index, (ident, _key, left, top, _action) in enumerate(plates):
        tall.block((ident,), (U * left, U * top), (beside, picture_top + 100 + index * TALL_PLATE_PITCH), TALL_PLATES)
    return picture_top + U * picture[3] * TALL_GROW


def tall_empty(tall, prefix: str, origin: tuple, top: float, list_w: float) -> None:
    """The empty state in the list's place, grown as the rows."""
    from retained_tall import MARGIN
    tall.block((f"{prefix}-empty",), (U * origin[0], U * origin[1]), (MARGIN, top), TALL_GROW)
    for part in (f"{prefix}-empty", f"{prefix}-empty-heading", f"{prefix}-empty-text"):
        tall.set(part, "width", list_w)


def tall_load_page(result: dict) -> dict:
    # retained_tall reaches this module through the settings rows.
    from retained_tall import CONTENT_W, TallLayout
    tall = TallLayout(result)
    list_w = CONTENT_W - 40                      # dp: the scrollbar beside it
    tall_filters(tall, ("browser-manual", "browser-automatic", "browser-sort"), (LIST_LEFT, 37, ROW_W), list_w)
    below = tall_detail(tall, "load", PREVIEW, ("load-date", "load-time", "load-skill", "load-played"), (DETAIL_LEFT, DATE_TOP), PLATES)
    heads = {"name": (U * LIST_LEFT, U * HEAD_TOP), "location": (U * (LIST_LEFT + LOCATION_X - 1), U * HEAD_TOP)}
    list_top = tall_list(tall, "load", SAVE_LINES, list_w, heads, below + 30)
    tall_empty(tall, "load", (LIST_LEFT, LIST_TOP), list_top, list_w)
    tall.dialogs(TALL_GROW)
    tall.shade("load-backing", BACKING_ALPHA)
    return result


def tall_save_page(result: dict) -> dict:
    """The Save page in a tall view: the filters, the headings and the list in
    two-line rows down to the name and, under it, SAVE GAME at the column's
    end, over the bottom band. Those two take a line each, 1.2 times the
    landscape size, since they no longer fit beside each other."""
    from retained_tall import CONTENT_W, MARGIN, TallLayout
    tall = TallLayout(result)
    list_w = CONTENT_W - 40
    tall_filters(tall, ("browser-manual", "browser-automatic", "browser-sort"), (SAVE_LIST_LEFT, 37, SAVE_ROW_W), list_w)
    heads = {"name": (U * SAVE_LIST_LEFT, U * SAVE_HEAD_TOP), "location": (U * (SAVE_LIST_LEFT + LOCATION_X - 1), U * SAVE_HEAD_TOP)}
    list_top = tall_list(tall, "save", SAVE_LINES, list_w, heads, TALL_TOP + 60)
    tall_empty(tall, "save", (SAVE_LIST_LEFT, SAVE_LIST_TOP), list_top, list_w)
    place = lambda ident: (tall.dp(ident, "left"), tall.dp(ident, "bottom"))  # noqa: E731
    go_h = tall.dp("save_go", "height") * TALL_SAVE_FOOT
    tall.block_bottom(("save_go",), place("save_go"), (CANVAS_W - MARGIN - tall.dp("save_go", "width") * TALL_SAVE_FOOT, TALL_FOOT_BOTTOM),
                      TALL_SAVE_FOOT)
    plate_bottom = TALL_FOOT_BOTTOM + go_h + 4
    tall.block_bottom(("save-name-plate", "save-name"), place("save-name-plate"), (tall.dp("save-name-plate", "left"), plate_bottom),
                      TALL_SAVE_FOOT)
    list_foot = plate_bottom + tall.dp("save-name-plate", "height") * TALL_SAVE_FOOT + 6
    for ident in ("save-list", "save_scroll"):
        tall.bind(ident, "bottom", list_foot)
    tall.dialogs(TALL_GROW)
    tall.shade("save-backing", BACKING_ALPHA)
    return result


# The Save Game page: the stock p_savegame (section 9 "Save and load"). The
# Marine pause's SAVE GAME hands off to it as LOAD GAME does to its page. The
# saves fill the content column under SAVE NAME and LOCATION headings, 570 u
# wide; the name plate (b1_dark) and the SAVE GAME plate (b2_dark) stand under
# the list, and the prompt bar in the bottom band.
SAVE_LIST_LEFT, SAVE_LIST_TOP, SAVE_LIST_H = 35.0, 78.0, 285.0
SAVE_ROW_W = 550.0                          # tabstops 0, 180 and 550: the scrollbar takes the last 20 u
SAVE_LIST_W = 570.0
SAVE_HEAD_TOP = 62.0
NAME_PLATE = (4.0, 370.0, 484.0, 52.0)      # save_filenamebg: b1_dark at 0.40
NAME_FIELD = (49.0, 382.0, 344.0, 20.0)     # save_filename: Lowpixel at textscale 0.28
NAME_BYTES = 32                              # the stock field held 20 characters; the list column shows about 30
SAVE_PLATE = (397.0, 370.0)                  # save_b1: b2_dark, its corner marker and the SAVE GAME label
SAVE_LABEL_W = 221.0                         # the label may run past the plate to the canvas edge, as the stock one did
NAME_TEXT = 20.0                             # dp: textscale 0.28
VALIDATION = [0.8941, 0.4275, 0.3373, 1]     # a field's validation message (#E46D56)
TITLE_W = 316.0                              # dp: a dialog's title slot, from the slot's leading foot to its trailing flank
WIDE_TITLE_W = 437.2                         # dp: a wide dialog title, from the slot's leading foot to 6 dp short of the frame's edge


def name_plate(family: Family = MARINE) -> dict:
    """The name's plate, b1_dark drawn at 484x52 u at 0.40: its band rows
    16-45 of 64 (13-37.4 u), starting 33 of its 512 texels in, with the rail
    up the leading edge, down the 14-texel lower-leading cut and along the
    foot, and the black fill opaque to 44 % of the width, half at 62 % and
    gone by 88 % (Appendix B.1, as the home's navigation plates). The Strogg
    plate takes the section plates' 30-degree shoulder and #F59512 rail."""
    left, top, width, height = NAME_PLATE
    w = U * width
    band_top, band_bottom = U * 13.0, U * 37.4
    lead, cut_x, cut_y = round(w * 33 / 512, 4), round(w * 14 / 512, 4), U * 11.4
    if family.strogg:
        cut_x, cut_y = round(U * 7 * STROGG_SHOULDER, 4), U * 7
    fade = [(0, [0, 0, 0, 1]), (0.44, [0, 0, 0, 1]), (0.62, [0, 0, 0, 0.5]), (0.88, [0, 0, 0, 0]), (1, [0, 0, 0, 0])]
    tint = family.rail
    rail = [(0, rgb(tint)), (0.44, rgb(tint)), (0.62, rgb(tint, 0.5)), (0.88, rgb(tint, 0)), (1, rgb(tint, 0))]
    return vector("save-name-plate", {**absolute(left=U * left, top=U * top, width=w, height=U * height), "opacity": number(0.4),
                                      "pointer-events": keyword("none")}, [
        path("fill", [(lead, band_top), (w, band_top), (w, band_bottom), (lead + cut_x, band_bottom), (lead, band_bottom - cut_y)],
             fill=linear((0, 0), (w, 0), fade)),
        path("rail", [(lead + 0.75, band_top), (lead + 0.75, band_bottom - cut_y), (lead + cut_x, band_bottom - 0.75), (w, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (w, 0), rail), 1.5)),
    ])


def name_field(doc: Document, family: Family = MARINE) -> dict:
    """The save's name (save_filename): a text field in Lowpixel at the stock
    field's textscale 0.28 (20 dp), white 0.80, on the name plate. Accept or
    typing opens it, and Enter saves under the name (the stock field's
    onEnter). An empty field says what it wants; while it is being edited
    empty, the validation line above the plate asks for a name. The Strogg
    field types in #FCFFC8 and marks its selection and focus #FFCC00."""
    left, top, width, height = NAME_FIELD
    hot = family.hot
    ident = "save-name"
    w, line = U * width, U * height
    parts = {part: f"{ident}-{part}" for part in ("viewport", "text", "selection", "caret", "composition", "validation")}
    def paint(part: str, thickness: float, tall: float, tint: list) -> dict:
        return group(parts[part], {**absolute(left=0, top=0, width=thickness, height=tall), "background-color": colour(tint),
                                   "pointer-events": keyword("none"), "display": keyword("block"), "opacity": number(1)})
    text_node = label(parts["text"], PLACEHOLDER, {**absolute(left=0, top=0, width=4096, height=line),
                      **typeface("lowpixel", NAME_TEXT, line, family.tint(0.8)), "white-space": keyword("pre"), "text-align": keyword("left")})
    viewport = group(parts["viewport"], {**absolute(left=0, top=0, width=w, height=line), "overflow": keyword("hidden")}, [
        paint("selection", 0, line, rgb(hot, 0.4)), text_node, paint("caret", 1.5, line, family.tint(1)),
        paint("composition", 0, 1, rgb(hot))])
    validation = label(parts["validation"], "#str_231108", {**absolute(left=0, top=-U * 13, width=w, height=U * 11),
                       **typeface("lowpixel", 14, U * 11, VALIDATION), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                       "display": keyword("none")})
    # The name the field wants, until it holds one or is being typed into.
    hint = label(f"{ident}-hint", "#str_231109", {**absolute(left=0, top=0, width=w, height=line),
                 **typeface("lowpixel", NAME_TEXT, line, family.tint(0.35)), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                 "pointer-events": keyword("none"), "display": keyword("block")})
    doc.bind(f"{ident}-hint.display", f"{ident}-hint", "display",
             {"op": "select", "args": [{"op": "&&", "args": [{"op": "==", "args": [{"state": "save.name"}, ""]},
                                                              {"op": "!", "args": [{"state": "ui.numberDraftsPending"}]}]}, "block", "none"]})
    # Focus draws the plates' orange inset under the name; the pointer half of it.
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=w, height=line + U * 4), "opacity": number(0),
                                      "pointer-events": keyword("none")}, [
        path("inset", [(0, line + U * 2), (round(w * 0.9, 4), line + U * 2)], closed=False, stroke=stroke(solid(rgb(hot)), 1.2))])
    rest, half, lit = number(0), number(0.5), number(1)
    ids = doc.states(ident, {
        "default": [(f"{ident}-focus", "opacity", rest), (parts["text"], "color", colour(family.tint(0.8)))],
        "hover": [(f"{ident}-focus", "opacity", half), (parts["text"], "color", colour(family.tint(1)))],
        "focus": [(f"{ident}-focus", "opacity", lit), (parts["text"], "color", colour(family.tint(1)))],
        "pressed": [(f"{ident}-focus", "opacity", lit), (parts["text"], "color", colour(family.tint(1)))],
        "disabled": [(f"{ident}-focus", "opacity", rest), (parts["text"], "color", colour(family.tint(0.3)))],
    })
    control = {"role": "text", "label": "#str_200925", "action": "rename", "value": {"state": "save.name"},
               "maxBytes": NAME_BYTES, "parts": parts, "states": ids}
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=line), [focus, hint, viewport, validation], control=control)


def check_save_text_fits(family: Family = MARINE) -> None:
    """Every shipped language's text fits its box on the Save Game page: the
    plates, headings, pager labels, field hint and validation on one line,
    the empty state's sentence on two, and each dialog's body on the three
    lines above its actions (the overwrite dialog keeps the third for the
    save's name). Generation fails otherwise."""
    face, head = family.face, heading_face(family)[0]
    one_line = (("#str_200003", face, 22.3, U * SAVE_LABEL_W), ("#str_200925", head, 13, U * (LOCATION_X - 4)),
                ("#str_200926", head, 13, U * (SAVE_ROW_W - LOCATION_X)), ("#str_231100", face, 20, U * SAVE_ROW_W),
                ("#str_231425", face, 16, U * (SAVE_ROW_W - 18)), ("#str_231426", face, 16, U * (SAVE_ROW_W - 18)),
                ("#str_200923", "marine", 20, CLOSE_W - 30),
                ("#str_231108", "lowpixel", 14, U * NAME_FIELD[2]), ("#str_231109", "lowpixel", NAME_TEXT, U * NAME_FIELD[2]),
                ("#str_200252", "marine", 20, WIDE_TITLE_W), ("#str_200266", "marine", 20, TITLE_W))
    body = 480 - 66
    wrapped = (("#str_231101", "lowpixel", 17, U * SAVE_ROW_W, 2), ("#str_200253", "lowpixel", 17, body, 2)) + tuple(
        (key, "lowpixel", 17, body, 3) for key in ("#str_200982", "#str_231110", "#str_231111", "#str_231112", "#str_231113"))
    for key, face, size, width in one_line:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        # A dialog title is set 0.075 em tighter between its glyphs.
        tighten = 0.075 * size if width in (TITLE_W, WIDE_TITLE_W) else 0
        for language, text in texts.items():
            needed = text_width(face, text, size) - tighten * (len(text) - 1)
            if needed > width:
                raise SystemExit(f"{key} in {language} ({text!r}) needs {needed:.0f} dp of {width:.0f}")
    for key, face, size, width, allowed in wrapped:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        for language, text in texts.items():
            if lines_needed(face, text, size, width) > allowed:
                raise SystemExit(f"{key} in {language} needs {lines_needed(face, text, size, width)} lines of {allowed}")


def save_game_document(family: Family = MARINE) -> dict:
    check_save_text_fits(family)
    doc = Document(f"openq4.savegame{family.suffix}")
    doc.state.update({
        "save.count": {"type": "number", "initial": 0},
        "save.first": {"type": "number", "initial": 0},
        "save.selected": {"type": "number", "initial": -1},
        "save.pick": {"type": "number", "initial": -1},
        "save.name": {"type": "string", "initial": ""},
        "save.target": {"type": "string", "initial": ""},
        "save.notice": {"type": "string", "initial": ""},
        # The adapter keeps this: the name field holds text not yet entered.
        "ui.numberDraftsPending": {"type": "boolean", "initial": False},
    })
    for action, command in (("back", "saveBack"), ("select", "saveSelect"), ("save", "saveSave"), ("overwrite", "saveOverwrite"),
                            ("newer", "saveNewer"), ("older", "saveOlder")):
        doc.session(action, command)
    # Entering the name writes it into save.name, then saves under it.
    doc.actions["rename"] = {"input": "string", "operation": "ui.state.set",
                             "arguments": {"key": "save.name", "value": {"input": "value"}, "event": "nameEntered"}}
    doc.actions["editName"] = {"operation": "ui.numberDrafts.focus", "arguments": {}}
    doc.events["nameEntered"] = [{"op": "action", "action": "save"}]
    # SAVE GAME saves under the entered name. A name typed but not entered
    # takes the focus back, where Enter saves under it.
    doc.events["saveRequested"] = [{"op": "if", "condition": {"state": "ui.numberDraftsPending"},
                                    "then": [{"op": "action", "action": "editName"}], "else": [{"op": "action", "action": "save"}]}]
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    # The Menu button closes the menus and resumes play (section 13.5): the
    # session returns the pause, whose RESUME closes it.
    doc.session("menuResume", "menuResume")
    doc.events["onMenuButton"] = [{"op": "action", "action": "menuResume"}]

    # The list: headings over an olive rule, the saves, newest first, and the
    # pages either side of them. A row's first Accept selects its save, whose
    # name the field then shows; Accept on the selected save saves over it.
    head_face, head_tint = heading_face(family)
    heading = {**typeface(head_face, 13, U * 14, head_tint), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    headings = [
        label("save-head-name", "#str_200925", {**absolute(left=U * SAVE_LIST_LEFT, top=U * SAVE_HEAD_TOP, width=U * (LOCATION_X - 4),
                                                          height=U * 14), **heading}),
        label("save-head-location", "#str_200926", {**absolute(left=U * (SAVE_LIST_LEFT + LOCATION_X - 1), top=U * SAVE_HEAD_TOP,
                                                              width=U * (SAVE_ROW_W - LOCATION_X), height=U * 14), **heading}),
        vector("save-rule", absolute(left=U * SAVE_LIST_LEFT, top=U * RULE_Y - 0.6, width=U * SAVE_LIST_W, height=1.2), [
            path("rule", [(0, 0.6), ({"fraction": 1}, 0.6)], closed=False, stroke=stroke(solid(rgb(family.rule, 0.26)), 1.2))]),
    ]
    chosen = [{"op": "call", "event": "saveRequested"}]
    rows = [save_row(doc, index, "save", SAVE_ROW_W, chosen, family=family, guarded=True, verb=VERB_SAVE) for index in range(ROWS)]
    newer = pager_row(doc, "page-newer", "#str_231425", "newer", {"op": ">", "args": [{"state": "save.first"}, 0]}, SAVE_ROW_W,
                      family=family)
    older = pager_row(doc, "page-older", "#str_231426", "older",
                      {"op": "<", "args": [{"op": "+", "args": [{"state": "save.first"}, ROWS]}, {"state": "save.count"}]}, SAVE_ROW_W,
                      family=family)
    column = group("save-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")},
                   [newer, *rows, older])
    # Above 100 % text the rows grow with it (section 2's reflow); the
    # headings, filters and SAVE GAME are furniture and keep their size.
    grow_rows(doc, column)
    viewport = group("save-list", {**absolute(left=U * SAVE_LIST_LEFT, top=U * SAVE_LIST_TOP, width=U * SAVE_ROW_W, height=U * SAVE_LIST_H),
                                   "overflow": keyword("auto")}, [column])
    scrollbar = objectives_scrollbar(doc, "save_scroll", "save-list", family.rule, family.hot, U * (SAVE_LIST_LEFT + SAVE_ROW_W + 8) - 18,
                                     U * SAVE_LIST_TOP, U * SAVE_LIST_H, line_step=U * ROW_H, key="#str_200003")
    # No saves yet: the empty state of section 13.13 in the list's place;
    # while a filter hides every save, CLEAR FILTERS under it.
    empty = group("save-empty", {**absolute(left=U * SAVE_LIST_LEFT, top=U * SAVE_LIST_TOP, width=U * SAVE_ROW_W, height=U * 110),
                                 "display": keyword("none")}, [
        label("save-empty-heading", "#str_231100", {**absolute(left=0, top=U * 10, width=U * SAVE_ROW_W, height=U * 16),
              **typeface(family.face, 20, U * 16, empty_heading_tint(family)), "white-space": keyword("nowrap")}),
        label("save-empty-text", "#str_231101", {**absolute(left=0, top=U * 32, width=U * SAVE_ROW_W, height=U * 36),
              **typeface("lowpixel", 17, 22, family.tint(0.8)), "white-space": keyword("normal"), "overflow": keyword("hidden")}),
        empty_clear(doc, "save", family),
    ])
    doc.bind("save-empty.display", "save-empty", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "save.count"}, 0]}, "block", "none"]})
    grow_empty(doc, empty, ("#str_231101", "#str_231424"), ("save-empty-clear",))

    # Under the list: the name on its plate, then SAVE GAME, which needs a
    # name, entered or typed.
    save_plate = section_plate(doc, "save_go", "#str_200003", *SAVE_PLATE, event="saveRequested", label_w=SAVE_LABEL_W, family=family)
    save_plate["control"]["prompt"] = VERB_SAVE
    doc.bind("save_go.enabled", "save_go", "enabled",
             {"op": "||", "args": [{"op": "!=", "args": [{"state": "save.name"}, ""]}, {"state": "ui.numberDraftsPending"}]})
    field = name_field(doc, family)
    toolbar = save_browser(doc, "save", SAVE_LIST_LEFT, SAVE_ROW_W, family)
    # The name field keeps its size on its plate, under the list.
    for node in [*headings, save_plate, field, *toolbar]:
        fixed_text(doc, node)
    back = page_prompts(doc, "save_back", action="back", verbs=(VERB_SAVE,), **family.prompts())
    # SAVE GAME follows the panel, whose full-size box would take its hits.
    panel = group("save-panel", {**FULL, "opacity": number(1)}, [*headings, viewport, scrollbar, empty, name_plate(family), field, back, *toolbar])
    stage = group("save-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)},
                  [panel, save_plate])

    # Saving over a save asks first and names it; an autosave, or a save the
    # session refuses, says why.
    overwrite = confirmation(doc, "overwriteModal", "#str_200252", "#str_200253", None, yes_event="overwriteConfirmed",
                             wide_title=True, strogg=family.strogg)
    doc.events["overwriteConfirmed"] = [{"op": "call", "event": "overwriteModalHide"}, {"op": "action", "action": "overwrite"}]
    target = label("overwriteModal-target", PLACEHOLDER, {**absolute(left=33, top=71 + 44, width=480 - 66, height=22),
                   **typeface("lowpixel", 17, 22, rgb(family.hot)), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("overwriteModal-target.text", "overwriteModal-target", "text", {"state": "save.target"})
    contents = next(child for child in walk(overwrite) if child.get("id") == "overwriteModal-contents")
    contents["children"].append(target)
    notice = confirmation(doc, "noticeModal", "#str_200266", PLACEHOLDER, None, close_key="#str_200923", strogg=family.strogg)
    doc.bind("noticeModal-body.text", "noticeModal-body", "text", {"state": "save.notice"})

    doc.timelines.add("arrive", 150, [track(node, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))])
                                      for node in ("save-panel", "save-backing")])
    doc.timelines.add("rest", 1, [track(node, "opacity", [(0, number(0)), (1, number(0))]) for node in ("save-panel", "save-backing")])
    # Each arrival focuses the name, ready for typing, as the stock page's
    # field took the keys.
    doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"}, {"op": "focus", "control": "save-name"}]
    doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}]
    doc.events["pagedOlder"] = [{"op": "focus", "control": "row-0"}]
    doc.events["pagedNewer"] = [{"op": "focus", "control": f"row-{ROWS - 1}"}]
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [backing("save-backing"), stage, overwrite, notice])
    return tall_save_page(fit_narrow(doc.build(root), pages=True, stretch=("save-list",)))


def walk(node: dict):
    """`node` and every node under it."""
    yield node
    for child in node.get("children", ()):
        yield from walk(child)
