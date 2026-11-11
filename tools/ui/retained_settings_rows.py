"""The settings pages' construction (docs/dev/ui-visual-design.md sections 7,
9 and 13.7; UI-01): CONTROLS, GAME OPTIONS and AUDIO are sub-pages of the
SETTINGS page in the main menu's frame, built as Create Server is
(retained_subpage): the top band one notch on, SETTINGS as the crumb and the
page's title at the step's foot, over a backing that darkens the content.

A page's sections are tabs (section 13.7) in the Escape card's strip (section
14.18) across the content's top, which Q and E, the shoulder buttons, the
strip's keycaps or a click switch; a page short enough for one screen has no
strip, only Create Server's header band with its columns' headings. Under it
each tab holds at most two columns of section 7 setting rows on the stock
24 u pitch (retained_mp_menus.value_row), every row's value in one value
column, its label taking two lines where a language needs them. Up and Down
read the rows down the first column and on down the second, then wrap; Up
from the first row reaches the tab, and Left and Right cross between the
columns where the row has no value to change, and stop at the columns' outer
edges. The prompt bar holds Select and Back, Back the page's button.

A portrait view of 3:4 or taller (the runtime's view.aspect source) gives the
rows its height instead of shrinking the two columns into it: the columns
stand one under the other in a single column that runs to the bottom band and
scrolls, its rows, headings and note at the size that fills the content's
width (CONTENT_W / COLUMN_W, about twice the landscape size), while the frame,
strip and prompts keep their narrow-view fit. A text size above 100 % (the
runtime's view.text source) takes the same single column, its rows grown by
the text size so that the text the runtime enlarges keeps its room. Bindings
on every length in the tab's rows make the switch; their vector art is drawn
in fractions of its box so it scales with them."""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, ORANGE, OLIVE, TEXT_LENGTHS, TEXT_STATE, U, VALUE, Document, absolute, colour,
                                    fixed_text, group, keyword, label, length, linear, number, page_prompts, path, rgb, solid, stroke,
                                    text_width, track, typeface, vector)
from retained_create_server import header_band
from retained_mp_menus import (ERROR, KEYCAP_GAP, KEYCAP_W, PLACEHOLDER, STRIP_EDGE, TAB_FLARE, TAB_PAD, TAB_RISE, TAB_SHOULDER,
                               TAB_SIZE, any_text, label_column, row_face, row_states, tab_strip, value_row, wrapped_width)
from retained_subpage import subpage_document

CRUMB = "#str_200009"                     # SETTINGS, the parent page's title
CONTENT_LEFT, CONTENT_W = 44.0, 587.0     # u: Create Server's header band
STRIP_TOP = 46.0                          # u: the tab strip's top, or the header band's, clear of the stepped band
HEAD_TOP, HEAD_H = STRIP_TOP + 24, 16.0   # u: the heading line under the strip
BAND_HEAD_TOP = STRIP_TOP + 16            # u: a heading in the header band, as Create Server's
ROWS_TOP, ROW_PITCH = STRIP_TOP + 41, 24.0   # u: the rows, on the stock settings pitch
COLUMN_GAP = 13.0                         # u
COLUMN_W = (CONTENT_W - COLUMN_GAP) / 2   # u: two columns at most
COLUMN_SLOTS = 14                         # rows and inline headings a column holds above the bottom band
VALUE_MIN = 150.0                         # dp: the value column never starts nearer the row's start
BACKING_ALPHA = 0.6                       # the stock page backing's
BACKING_STOPS = ((4.0, 0.0), (40.0, 1.0), (636.0, 1.0), (660.0, 0.0))   # u on the canvas
TAB_STATE, LEAVING_STATE = "page.tab", "page.leaving"   # "settings." states belong to the settings service
TAB_FADE_MS = 150                         # the Escape card's page cross-fade
HEADING_FACE = ("lowpixel", 14, [1, 1, 1, 0.4])   # Create Server's headings
# A heading or a note starts where its rows' labels do, 21 dp in: at the
# column's edge it began before the strip's backing and the header band's
# cut, over their leading end.
HEADING_INDENT = 21.0
VIEW_STATE = "view.aspect"                # the runtime's view source: the view's width over its height
# Views of 3:4 and taller take the single column: there it holds a landscape
# column's fourteen rows at TALL_SCALE between the strip and the bottom band.
TALL_ASPECT = 0.76
TALL_SCALE = CONTENT_W / COLUMN_W         # a column's rows across the content's width
TALL = {"op": "<", "args": [{"state": VIEW_STATE}, TALL_ASPECT]}
# A note's width keeps its place while its indent grows with the rows in the
# single column, so the note gives up the indent at its largest.
NOTE_INSET = round(HEADING_INDENT * TALL_SCALE, 3)
TEXT = {"state": TEXT_STATE}
# The single column, in a tall view or at a text size above 100 %, where two
# columns grown by the text size would not fit side by side.
STACKED = {"op": "||", "args": [TALL, {"op": ">", "args": [TEXT, 1]}]}
ENTRY_W = 66.0                            # dp: a fine slider's typed number
SCROLL_GUTTER = 4.0                       # dp: the rows' area past the rows on either side
RANGE_MESSAGE = "#str_231560"             # "Enter a number from %s to %s.", the runtime filling the field's range


def heading(key: str) -> tuple:
    """A section heading among a column's rows."""
    return ("heading", key)


def value_column(labels) -> float:
    """The value column (dp from the row's start) that leaves every label,
    in every language, two lines before it."""
    return max(VALUE_MIN, label_column(labels))


def setting_row(doc: Document, ident: str, label_key: str, kind: str, value_x: float, **more) -> dict:
    """A section 7 setting row of the column's width on the stock pitch, its
    list unfolding within the screen. Every settings label takes the 14 dp
    face that wraps to a second line, so a page's labels share one size
    whichever of them a language needs two lines for."""
    return value_row(doc, ident, label_key, kind, U * COLUMN_W, value_x, row_h=U * ROW_PITCH, placement="screen", wrap=True, **more)


def row_chrome(ident: str, row_h: float) -> list:
    """A setting row's plate, its hover and focus wash and its focus rail, as
    value_row draws them, for a row that builds its own content."""
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)]
    return [
        vector(f"{ident}-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1))]),
        vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
            path("wash", shape, fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))]))]),
        vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=row_h), "opacity": number(0)}, [
            path("rail", [(0, 7), (6, 1), (6, row_h - 1), (0, row_h - 1)], fill=solid(rgb(ORANGE)))]),
    ]


def range_text(key: str, minimum: float, maximum: float, decimals: int) -> dict:
    """`key`'s text in every shipped language with its %s filled as the
    runtime fills a number field's message: the bounds to the field's places."""
    bounds = [f"{bound:.{decimals}f}" for bound in (minimum, maximum)]
    return {language: text.replace("%s", bounds[0], 1).replace("%s", bounds[1], 1) for language, text in any_text(key).items()}


def entry_row(doc: Document, ident: str, label_key: str, value_x: float, value: dict, action: str, slider: tuple) -> dict:
    """A fine slider (section 7) with a typed number after it in one row: the
    slider steps, the number (`{ident}_number`) takes any value in range to
    the slider's places, both through `action`. While the typed text cannot
    be taken, its message, the field's own range (RANGE_MESSAGE), covers the
    row's label in the error colour. The number follows its slider in Up and
    Down; Left from it returns to the slider."""
    minimum, maximum, step, decimals = slider
    row_h, width = U * ROW_PITCH, U * COLUMN_W
    slider_w = width - ENTRY_W - 6
    row = value_row(doc, ident, label_key, "slider", slider_w, value_x, value=value, action=action, slider=slider, row_h=row_h,
                    placement="screen", wrap=True)
    row["properties"].update(absolute(left=0, top=0))
    for child in row["children"]:
        if child["id"] == f"{ident}-value":
            child["properties"]["display"] = keyword("none")
        elif child["id"] == f"{ident}-track":
            # The track takes the readout's room, its ticks spread along it:
            # the number shows the value.
            old = child["properties"]["width"]["value"]
            child["properties"]["width"] = length(old + 48)
            for drawn in next(part for part in child["children"] if part["id"] == f"{ident}-ramp")["paths"]:
                for command in drawn["commands"]:
                    command["points"] = [[round(x * (old + 48) / old, 3), y] for x, y in command["points"]]
    paired = f"{ident}_number"
    field_h = 22.0
    top = (row_h - field_h) / 2
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    ink = lambda name, tint, w, h: vector(name, {**absolute(left=0, top=0, width=w, height=h), "display": keyword("block"),
                                                 "opacity": number(1), "pointer-events": keyword("none")},
                                          [path("ink", whole, fill=solid(tint))])
    parts = {part: f"{paired}-{part}" for part in ("viewport", "text", "selection", "caret", "composition", "validation")}
    plate = vector(f"{paired}-field", {**absolute(left=0, top=top, width=ENTRY_W, height=field_h), "pointer-events": keyword("none")},
                   [path("field", whole, fill=solid([0, 0, 0, 0.6]), stroke=stroke(solid(rgb(OLIVE, 0.45)), 1))])
    focus = vector(f"{paired}-focus", {**absolute(left=0, top=top, width=ENTRY_W, height=field_h), "opacity": number(0),
                                       "pointer-events": keyword("none")}, [path("focus", whole, stroke=stroke(solid(rgb(ORANGE)), 1.5))])
    text = label(parts["text"], PLACEHOLDER, {**absolute(left=6, top=0, width=4096, height=field_h),
                 **typeface("lowpixel", 15, field_h, rgb(VALUE)), "white-space": keyword("pre"), "text-align": keyword("left")})
    viewport = group(parts["viewport"], {**absolute(left=0, top=top, width=ENTRY_W, height=field_h), "display": keyword("block"),
                                         "overflow": keyword("hidden")},
                     [ink(parts["selection"], rgb(ORANGE, 0.4), 0, field_h), text, ink(parts["caret"], [1, 1, 1, 1], 1, field_h),
                      ink(parts["composition"], rgb(ORANGE), 0, 1)])
    # The message covers the row's label on the plate's colour, two lines at
    # the label's face.
    over = {language: round(wrapped_width(message)) for language, message in range_text(RANGE_MESSAGE, minimum, maximum, decimals).items()
            if wrapped_width(message) > value_x - 29}
    if over:
        raise SystemExit(f"{ident}'s range message would not fit its label's two lines: {over}")
    message = label(parts["validation"], RANGE_MESSAGE, {
        **absolute(left=21 - slider_w - 6, top=1, width=value_x - 29, height=row_h - 2), "padding-top": length((row_h - 2 - 26) / 2),
        **typeface("lowpixel", 14, 13, rgb(ERROR)), "background-color": colour([0.035, 0.047, 0.031, 1]),
        "white-space": keyword("normal"), "overflow": keyword("hidden"), "display": keyword("none"), "pointer-events": keyword("none")})
    control = {"role": "number", "label": label_key, "action": action, "value": value, "minimum": minimum, "maximum": maximum,
               "exponent": False, "maxBytes": 16, "parts": parts, "navigation": {"left": ident},
               "states": doc.states(paired, {state: [(f"{paired}-focus", "opacity", number(level))] for state, level in (
                   ("default", 0), ("hover", 0.55), ("focus", 1), ("pressed", 1), ("disabled", 0))})}
    control.update({"decimals": decimals} if decimals else {"integer": True})
    entry = group(paired, {**absolute(left=slider_w + 6, top=0, width=ENTRY_W, height=row_h), "opacity": number(1)},
                  [plate, focus, viewport, message], control=control)
    return group(f"{ident}_row", {"position": keyword("relative"), "display": keyword("block"), "width": length(width),
                                  "height": length(row_h), "flex-shrink": number(0)}, [row, entry])


def button_row(doc: Document, ident: str, label_key: str, value_x: float, value: dict | None, control: dict) -> dict:
    """A row that is one button: its label as a setting row's and, in the
    value column, the text `value` binds in a list's value face (none
    without one). `control` names what it runs."""
    row_h = U * ROW_PITCH
    caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True, row_h), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])
    shown = label(f"{ident}-value", PLACEHOLDER, {**absolute(left=value_x, top=0, width=U * COLUMN_W - value_x - 12, height=row_h),
                  **typeface("lowpixel", 14, row_h, rgb(VALUE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    if value is not None:
        doc.bind(f"{ident}-value.text", f"{ident}-value", "text", value)
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(U * COLUMN_W),
                         "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)},
                 [*row_chrome(ident, row_h), head, *([shown] if value is not None else [])],
                 control={"role": "button", "label": label_key, **control, "states": row_states(doc, ident)})


def check_settings_text(tabs: list, note: str | None) -> None:
    """Every shipped language's text fits: the tabs in the strip between its
    keycaps, each heading in its column, the note on the heading line, and
    each label in two lines in its row's label box (before its value
    column)."""
    over = {}
    if len(tabs) > 1:
        room = U * CONTENT_W - 2 * (STRIP_EDGE + KEYCAP_W + KEYCAP_GAP)
        totals = {}
        for _ident, key, _columns in tabs:
            for language, text in any_text(key).items():
                totals[language] = totals.get(language, 0) + text_width("marine", text, TAB_SIZE) + TAB_FLARE + 2 * TAB_PAD + TAB_SHOULDER
        over.update({("tabs", language): round(total) for language, total in totals.items() if total > room})
    face, size, _tint = HEADING_FACE
    for _ident, _key, columns in tabs:
        for column in columns:
            for item in column:
                if isinstance(item, tuple):
                    for language, text in any_text(item[1]).items():
                        if text_width(face, text, size) > U * COLUMN_W - 8 - HEADING_INDENT:
                            over[(item[1], language)] = round(text_width(face, text, size))
                elif item.get("control", {}).get("label"):
                    key = item["control"]["label"]
                    head = next(child for child in item["children"] if child["id"] == f"{item['id']}-head")
                    for language, text in any_text(key).items():
                        if wrapped_width(text) > head["properties"]["width"]["value"]:
                            over[(key, language)] = round(wrapped_width(text))
    if note is not None:
        for language, text in any_text(note).items():
            if text_width(face, text, size) > U * CONTENT_W - NOTE_INSET:
                over[(note, language)] = round(text_width(face, text, size))
    if over:
        raise SystemExit(f"settings page text past its room: {over}")


def settings_backing(ident: str) -> dict:
    """The page backing (section 9) across both columns: black at the stock
    0.6, faded in over the leading margin and out past the content's end, the
    stops on the canvas wherever the view centres it."""
    first, last = BACKING_STOPS[0][0], BACKING_STOPS[-1][0]
    stops = [(round((u - first) / (last - first), 4), [0, 0, 0, round(BACKING_ALPHA * alpha, 4)]) for u, alpha in BACKING_STOPS]
    at = lambda u: {"fraction": 0.5, "dp": round(U * u - CANVAS_W / 2, 4)}
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    return vector(ident, {**FULL, "opacity": number(1), "pointer-events": keyword("none")}, [
        path("backing", whole, fill=linear((at(first), 0), (at(last), 0), stops))])


def heading_label(ident: str, key: str, left: float, top: float, width: float, height: float) -> dict:
    """A heading in a box `width` wide from `left`, its text HEADING_INDENT in."""
    face, size, tint = HEADING_FACE
    return label(ident, key, {**absolute(left=left, top=top, width=width - HEADING_INDENT, height=height),
                              "padding-left": length(HEADING_INDENT), **typeface(face, size, height, tint),
                              "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")})


def _enable_while(doc: Document, control: str, current: dict) -> None:
    """The control answers only while its tab is current, and whatever else
    already decides that still does."""
    for binding in doc.bindings:
        if binding["node"] == control and binding["property"] == "enabled":
            binding["value"] = {"op": "&&", "args": [current, binding["value"]]}
            return
    doc.bind(f"{control}.enabled", control, "enabled", current)


def _controls(node: dict) -> list:
    """The controls a column item holds in reading order: a row is one; a
    composite row (a slider with its number field) holds several; a note
    none."""
    if "control" in node:
        return [node]
    return [found for child in node.get("children", []) for found in _controls(child)]


def _slots(node: dict) -> int:
    """The pitch slots a column item spans: a note may take two lines."""
    height = node.get("properties", {}).get("height", {}).get("value", U * ROW_PITCH)
    return max(1, round(height / (U * ROW_PITCH)))


def _runtime_owned(node: dict, owned: set) -> None:
    """The lengths the runtime writes in the controls under `node`, which no
    binding may own: a slider's fill and thumb, a list's popup, viewport and
    scrollbar, a number or text field's text offset and paint parts."""
    control = node.get("control") or {}
    parts = control.get("parts", {})
    if control.get("role") == "slider":
        owned.update({(parts["fill"], "width"), (parts["thumb"], "left")})
    elif control.get("role") == "choice":
        owned.update((parts["popup"], key) for key in ("display", "left", "top", "width", "height"))
        owned.update({(parts["viewport"], "width"), (parts["viewport"], "height"), (parts["content"], "top")})
        if control.get("scrollbar"):
            owned.update((control["scrollbar"]["track"], key) for key in ("left", "top", "height", "bottom"))
            owned.update((control["scrollbar"]["thumb"], key) for key in ("top", "height", "bottom"))
    elif control.get("role") in ("number", "text"):
        owned.add((parts["text"], "left"))
        owned.update((parts[part], key) for part in ("selection", "caret", "composition") for key in ("left", "top", "width", "height"))
    for child in node.get("children", []):
        _runtime_owned(child, owned)


def _in_fractions(node: dict, width: float, height: float) -> None:
    """A vector's art in fractions of its box, so that it scales with it."""
    def convert(value, extent):
        if isinstance(value, (int, float)):
            return {"fraction": round(value / extent, 6)}
        if isinstance(value, dict) and "dp" in value:
            return {"fraction": round(value.get("fraction", 0) + value["dp"] / extent, 6)}
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


def grown(value: float, text_length: bool = False):
    """A row length in the single column: TALL_SCALE times it in a tall view,
    the text size times it at a larger text size. The runtime itself grows a
    font length by the text size, so a font length takes only the rest."""
    tall = round(value * TALL_SCALE, 4)
    if text_length:
        return {"op": "select", "args": [TALL, {"op": "/", "args": [tall, TEXT]}, value]}
    return {"op": "select", "args": [TALL, tall, {"op": "*", "args": [value, TEXT]}]}


def scale_tall(doc: Document, node: dict, skip: set = frozenset()) -> None:
    """Every dp length in `node`'s subtree grows in the single column
    (grown()), by a binding, but for those `skip` names, the runtime writes
    or something already binds or animates; vector art whose box is known
    turns to fractions of it."""
    owned = set(skip)
    _runtime_owned(node, owned)
    owned.update((binding["node"], binding["property"]) for binding in doc.bindings)
    owned.update((entry["node"], entry["property"]) for timeline in doc.timelines.items for entry in timeline["tracks"])

    def walk(item: dict, parent: tuple) -> None:
        props = item.get("properties", {})
        size = []
        for key, extent in (("width", parent[0]), ("height", parent[1])):
            found = props.get(key) or {}
            unit = found.get("unit") if found.get("type") == "length" else None
            size.append(found["value"] if unit == "dp" else found["value"] * extent / 100 if unit == "%" and extent else None)
        if item.get("type") == "vector" and size[0] and size[1]:
            _in_fractions(item, *size)
        for key, found in props.items():
            if found.get("type") != "length" or found.get("unit") != "dp" or not found["value"] or (item["id"], key) in owned:
                continue
            doc.bind(f"tall.{item['id']}.{key}", item["id"], key, grown(found["value"], key in TEXT_LENGTHS))
        for child in item.get("children", []):
            walk(child, tuple(size))
    walk(node, (None, None))


def _lead_heading(doc: Document, ident: str, key: str, lead: float) -> dict:
    """A column's first heading, in the slot above its first row; in the
    tall column, a heading among its rows like any other."""
    node = heading_label(ident, key, 0, 0, U * COLUMN_W, U * HEAD_H)
    node["properties"].update({"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(0),
                               "margin-bottom": length(U * (lead - HEAD_H))})
    for key_ in ("left", "top"):
        node["properties"].pop(key_)
    for prop, tall, wide in (("height", ROW_PITCH, HEAD_H), ("line-height", ROW_PITCH, HEAD_H), ("margin-bottom", 0, lead - HEAD_H)):
        stacked = grown(U * tall, prop in TEXT_LENGTHS) if tall else 0
        doc.bind(f"tall.{ident}.{prop}", ident, prop, {"op": "select", "args": [STACKED, stacked, U * wide]})
    return node


def _note_line(ident: str, key: str, lead: float) -> dict:
    """The page's note across the content in the slot above the rows: one line
    in a landscape view, as many as it needs at the tall size."""
    face, size, tint = HEADING_FACE
    return label(ident, key, {"position": keyword("relative"), "display": keyword("block"), "width": length(U * CONTENT_W - NOTE_INSET),
                              "padding-left": length(HEADING_INDENT), "margin-bottom": length(U * (lead - HEAD_H)),
                              **typeface(face, size, U * HEAD_H, tint),
                              "white-space": keyword("normal"), "pointer-events": keyword("none")})


def settings_subpage(doc: Document, prefix: str, title_key: str, tabs: list, *, note: str | None = None,
                     activate=None, overlays: list = (), back_event: str | None = None, back_id: str | None = None,
                     slots: int = COLUMN_SLOTS, footer: list = (), body: str | None = None,
                     first_focus: dict | None = None, portrait: bool = True, prompts: dict | None = None) -> dict:
    """The page: `tabs` holds each tab's (id, label, columns), a column a list
    of rows, notes and headings (heading()), at most two columns a tab. One
    tab shows no strip: its columns' first headings stand in the header band.
    `note` is a line on the heading line, for tabs without headings. A page
    whose Back is its own event (`back_event`, the button `back_id`) keeps
    its onBack; `slots` caps a column shorter where `footer` parts stand
    under the columns; `body` names a group around the pages, the bounds
    its rows' lists unfold within; `first_focus` gives a tab whose first rows
    may be hidden the steps that focus its first row shown. Without
    `portrait` the two columns keep their places in every view, shrinking in
    a portrait one. `prompts` are more of page_prompts' arguments: the page's
    own verbs and legends.
    onActivate   the sub-page's arrival, then the current tab's first row
                 takes the focus: the steps `activate` makes of those;
    tab_<id>     switches to a tab at once, cross-fades the pages over 150 ms
                 and focuses the tab's first row; onTabPrevious and
                 onTabNext wrap around the strip."""
    check_settings_text(tabs, note)
    tabbed = len(tabs) > 1
    if back_event is None:
        doc.session("back", f"{prefix}Back")
    doc.state.update({TAB_STATE: {"type": "number", "initial": 0}, LEAVING_STATE: {"type": "number", "initial": -1}})
    width = U * CONTENT_W
    chrome, pages, firsts, above = [], [], [], []
    if tabbed:
        strip = tab_strip(doc, width, [(ident, key, None) for ident, key, _columns in tabs], top=0, state=TAB_STATE)
        # The strip follows the pages in the tree: each page is a full-size
        # group, and a later group's box takes the pointer's hits over earlier
        # controls, so a strip under the pages left its tabs dead to the mouse
        # and the VR pointer. They never overlap on screen. It also follows
        # the footer and Back, the last controls in reading order: Next and
        # Previous walk the tree as a ring, so a tab's last row leads on to
        # Back and Back to the strip, the order the page reads in.
        above.append(group(f"{prefix}-strip", absolute(left=U * CONTENT_LEFT, top=U * STRIP_TOP, width=width, height=2 * TAB_RISE),
                           strip))
        # The tabs keep their size in the strip's slots at any text size.
        fixed_text(doc, above[-1])
    else:
        # The header band stands behind the columns' headings; the tall
        # column's headings stand in its rows instead.
        band = header_band(f"{prefix}-header", CONTENT_LEFT, STRIP_TOP, CONTENT_W, ROWS_TOP - STRIP_TOP - 4)
        if portrait:
            band["properties"]["display"] = keyword("block")
            doc.bind(f"{prefix}-header.display", f"{prefix}-header", "display", {"op": "select", "args": [STACKED, "none", "block"]})
        chrome.append(band)
    if portrait:
        doc.state[VIEW_STATE] = {"type": "number", "initial": round(4 / 3, 4), "view": "aspect"}
        doc.state[TEXT_STATE] = {"type": "number", "initial": 1, "view": "text"}
    elif note is not None:
        chrome.append(heading_label(f"{prefix}-note", note, U * CONTENT_LEFT, U * HEAD_TOP, width, U * HEAD_H))
    # Each tab's rows scroll in one area from its heading line to the bottom
    # band; a tall view stretches it to the view's bottom (fit_narrow).
    flow_top = HEAD_TOP if tabbed else BAND_HEAD_TOP
    lead = ROWS_TOP - flow_top
    scrollers = []
    for index, (ident, _key, columns) in enumerate(tabs):
        if len(columns) > 2:
            raise SystemExit(f"{prefix}'s {ident} tab has {len(columns)} columns; a settings tab holds two at most")
        current = {"op": "==", "args": [{"state": TAB_STATE}, index]}
        headed = [bool(column) and isinstance(column[0], tuple) for column in columns]
        if any(headed) and not all(headed):
            raise SystemExit(f"{prefix}'s {ident} tab heads one column and not the other")
        lines = [_note_line(f"{prefix}-{ident}-note", note, lead)] if note is not None and portrait else []
        laid, parts, order = [], [], []
        for side, column in enumerate(columns):
            left = U * (CONTENT_LEFT + side * (COLUMN_W + COLUMN_GAP))
            items = list(column)
            flow, rows, used = [], [], 0
            if headed[side] and portrait:
                flow.append(_lead_heading(doc, f"{prefix}-{ident}-head-{side}", items.pop(0)[1], lead))
            elif headed[side]:
                parts.append(heading_label(f"{prefix}-{ident}-head-{side}", items.pop(0)[1], left, U * flow_top, U * COLUMN_W, U * HEAD_H))
            for position, item in enumerate(items):
                if isinstance(item, tuple):
                    flow.append(heading_label(f"{prefix}-{ident}-head-{side}-{position}", item[1], 0, 0, U * COLUMN_W, U * ROW_PITCH))
                    flow[-1]["properties"].update({"position": keyword("relative"), "display": keyword("block"),
                                                   "flex-shrink": number(0)})
                    used += 1
                    continue
                flow.append(item)
                used += _slots(item)
                if _controls(item):
                    rows.append(_controls(item))
            if used > slots:
                raise SystemExit(f"{prefix}'s {ident} tab puts {used} slots in a column of {slots}")
            order.append(rows)
            if not portrait:
                parts.append(group(f"{prefix}-{ident}-column-{side}", {
                    **absolute(left=left, top=U * ROWS_TOP, width=U * COLUMN_W, height=U * ROW_PITCH * used),
                    "display": keyword("flex"), "flex-direction": keyword("column")}, flow))
                continue
            laid.append(group(f"{prefix}-{ident}-column-{side}", {"position": keyword("relative"), "display": keyword("flex"),
                                                                  "flex-direction": keyword("column"), "width": length(U * COLUMN_W),
                                                                  "flex-shrink": number(0)}, flow))
        if portrait:
            # Side by side, or one under the other in a tall view.
            arranged = group(f"{prefix}-{ident}-columns", {"position": keyword("relative"), "display": keyword("flex"),
                                                           "flex-direction": keyword("row"), "align-items": keyword("flex-start")}, laid)
            doc.bind(f"{arranged['id']}.flex-direction", arranged["id"], "flex-direction",
                     {"op": "select", "args": [STACKED, "column", "row"]})
            if not lines and not any(headed):
                arranged["properties"]["padding-top"] = length(U * lead)
            if len(laid) == 2:
                laid[0]["properties"]["margin-right"] = length(U * COLUMN_GAP)
                doc.bind(f"{laid[0]['id']}.margin-right", laid[0]["id"], "margin-right",
                         {"op": "select", "args": [STACKED, 0, U * COLUMN_GAP]})
            scale_tall(doc, arranged)
            for line in lines:
                scale_tall(doc, line, {(line["id"], "width")})
            scrollers.append(f"{prefix}-{ident}-rows")
            # A gutter each side keeps the rows' outlines and focus rail inside
            # the area's clip.
            parts = [group(scrollers[-1], {**absolute(left=U * CONTENT_LEFT - SCROLL_GUTTER, top=U * flow_top, width=width + SCROLL_GUTTER,
                                                      height=U * (lead + slots * ROW_PITCH)),
                                           "padding-left": length(SCROLL_GUTTER), "overflow": keyword("auto")}, [*lines, arranged])]
        # Up and Down read down the first column and on down the second,
        # wrapping; Left and Right cross to the row beside.
        nodes = [control for rows in order for unit in rows for control in unit]
        sequence = [control["id"] for control in nodes]
        for position, node in enumerate(nodes):
            navigation = node["control"].setdefault("navigation", {})
            navigation["down"] = sequence[(position + 1) % len(sequence)]
            navigation["up"] = sequence[position - 1] if position else (f"tab-{ident}" if tabbed else sequence[-1])
        if len(order) == 2 and order[1]:
            for side, rows in enumerate(order):
                other = order[1 - side]
                for slot, unit in enumerate(rows):
                    unit[0]["control"]["navigation"]["right" if side == 0 else "left"] = other[min(slot, len(other) - 1)][0]["id"]
        # The columns' outer edges hold the focus: a row with no value to
        # change would otherwise send Left or Right looking for the nearest
        # control in that direction, which from the trailing column is a tab.
        columns = [rows for rows in order if rows]
        for side, rows in enumerate(columns):
            for unit in rows:
                if side == 0:
                    unit[0]["control"]["navigation"].setdefault("left", unit[0]["id"])
                if side == len(columns) - 1:
                    unit[-1]["control"]["navigation"].setdefault("right", unit[-1]["id"])
        firsts.append(sequence[0])
        if tabbed:
            for control in sequence:
                _enable_while(doc, control, current)
            shown = {"op": "||", "args": [current, {"op": "==", "args": [{"state": LEAVING_STATE}, index]}]}
            doc.bind(f"{prefix}-page-{ident}.display", f"{prefix}-page-{ident}", "display", {"op": "select", "args": [shown, "block", "none"]})
        pages.append(group(f"{prefix}-page-{ident}", {**FULL, "display": keyword("none" if tabbed and index else "block"),
                                                      "opacity": number(1)}, parts))
    focus = [(first_focus or {}).get(ident, [{"op": "focus", "control": first}]) for (ident, _key, _columns), first in zip(tabs, firsts)]
    if tabbed:
        _tab_programs(doc, prefix, tabs, focus)
    steps = list(focus[-1])
    for index in range(len(firsts) - 2, -1, -1):
        steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": TAB_STATE}, index]},
                  "then": list(focus[index]), "else": steps}]
    if back_event is None:
        doc.events["onBack"] = [{"op": "action", "action": "back"}]
        back = page_prompts(doc, back_id or f"{prefix}_back", action="back", **(prompts or {}))
    else:
        back = page_prompts(doc, back_id or f"{prefix}_back", event=back_event, **(prompts or {}))
    if body is not None:
        pages = [group(body, FULL, pages)]
    return subpage_document(doc, prefix, CRUMB, title_key, firsts[0], [*chrome, *pages, *footer, back, *above], list(overlays),
                            stretch=tuple(scrollers), activate=activate(steps) if activate else steps,
                            backing_node=settings_backing(f"{prefix}-backing"))


def show_tab(doc: Document, prefix: str, tabs: list, index: int) -> list:
    """Steps that make tab `index` current at once, without the cross-fade:
    for an arrival that must land on a row in another tab."""
    return [{"op": "setState", "values": {TAB_STATE: index, LEAVING_STATE: -1}},
            {"op": "playTimeline", "timeline": f"place-{tabs[index][0]}"}]


def _tab_programs(doc: Document, prefix: str, tabs: list, focus: list) -> None:
    count = len(tabs)
    for index, (ident, _key, _columns) in enumerate(tabs):
        page = f"{prefix}-page-{ident}"
        others = [f"{prefix}-page-{other}" for other, _k, _c in tabs if other != ident]
        # The page arriving fades in from nothing; every other page fades from
        # where it stands to nothing, which leaves hidden pages at 0.
        doc.timelines.add(f"show-{ident}", TAB_FADE_MS, [
            track(page, "opacity", [(0, number(0)), (1, number(0)), (TAB_FADE_MS, number(1))]),
            *[track(other, "opacity", [(0, number(1)), (TAB_FADE_MS, number(0))]) for other in others],
        ], complete="pageShown")
        doc.timelines.add(f"place-{ident}", 1, [
            track(page, "opacity", [(0, number(1)), (1, number(1))]),
            *[track(other, "opacity", [(0, number(0)), (1, number(0))]) for other in others],
        ])
        doc.events[f"tab_{ident}"] = [
            {"op": "if", "condition": {"op": "!=", "args": [{"state": TAB_STATE}, index]},
             "then": [{"op": "setState", "values": {LEAVING_STATE: {"state": TAB_STATE}, TAB_STATE: index}},
                      {"op": "playTimeline", "timeline": f"show-{ident}"}, *focus[index]]},
        ]
    doc.events["pageShown"] = [{"op": "setState", "values": {LEAVING_STATE: -1}}]
    for name, step in (("onTabPrevious", -1), ("onTabNext", 1)):
        steps: list = []
        for index in reversed(range(count)):
            target = tabs[(index + step) % count][0]
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": TAB_STATE}, index]},
                      "then": [{"op": "call", "event": f"tab_{target}"}], **({"else": steps} if steps else {})}]
        doc.events[name] = steps
