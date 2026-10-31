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
columns where the row has no value to change. The prompt bar holds Select and
Back, Back the page's button."""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, ORANGE, OLIVE, U, VALUE, Document, absolute, group, keyword, label, length,
                                    linear, number, page_prompts, path, rgb, solid, stroke, text_width, track, typeface, vector)
from retained_create_server import header_band
from retained_mp_menus import (KEYCAP_GAP, KEYCAP_W, PLACEHOLDER, STRIP_EDGE, TAB_FLARE, TAB_PAD, TAB_RISE, TAB_SHOULDER, TAB_SIZE,
                               any_text, label_column, row_face, row_states, tab_strip, value_row, wrapped_width)
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


def button_row(doc: Document, ident: str, label_key: str, value_x: float, value: dict, control: dict) -> dict:
    """A row that is one button: its label as a setting row's and, in the
    value column, the text `value` binds in a list's value face. `control`
    names what it runs."""
    row_h = U * ROW_PITCH
    caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True, row_h), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])
    shown = label(f"{ident}-value", PLACEHOLDER, {**absolute(left=value_x, top=0, width=U * COLUMN_W - value_x - 12, height=row_h),
                  **typeface("lowpixel", 14, row_h, rgb(VALUE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-value.text", f"{ident}-value", "text", value)
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(U * COLUMN_W),
                         "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)},
                 [*row_chrome(ident, row_h), head, shown],
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
                        if text_width(face, text, size) > U * COLUMN_W - 8:
                            over[(item[1], language)] = round(text_width(face, text, size))
                elif item.get("control", {}).get("label"):
                    key = item["control"]["label"]
                    head = next(child for child in item["children"] if child["id"] == f"{item['id']}-head")
                    for language, text in any_text(key).items():
                        if wrapped_width(text) > head["properties"]["width"]["value"]:
                            over[(key, language)] = round(wrapped_width(text))
    if note is not None:
        for language, text in any_text(note).items():
            if text_width(face, text, size) > U * CONTENT_W:
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
    face, size, tint = HEADING_FACE
    return label(ident, key, {**absolute(left=left, top=top, width=width, height=height), **typeface(face, size, height, tint),
                              "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")})


def _enable_while(doc: Document, control: str, current: dict) -> None:
    """The control answers only while its tab is current, and whatever else
    already decides that still does."""
    for binding in doc.bindings:
        if binding["node"] == control and binding["property"] == "enabled":
            binding["value"] = {"op": "&&", "args": [current, binding["value"]]}
            return
    doc.bind(f"{control}.enabled", control, "enabled", current)


def settings_subpage(doc: Document, prefix: str, title_key: str, tabs: list, *, note: str | None = None,
                     activate=None, overlays: list = ()) -> dict:
    """The page: `tabs` holds each tab's (id, label, columns), a column a list
    of rows and headings (heading()), at most two columns a tab. One tab
    shows no strip: its columns' first headings stand in the header band.
    `note` is a line on the heading line, for tabs without headings.
    onActivate   the sub-page's arrival, then the current tab's first row
                 takes the focus: the steps `activate` makes of those;
    tab_<id>     switches to a tab at once, cross-fades the pages over 150 ms
                 and focuses the tab's first row; onTabPrevious and
                 onTabNext wrap around the strip."""
    check_settings_text(tabs, note)
    tabbed = len(tabs) > 1
    doc.session("back", f"{prefix}Back")
    doc.state.update({TAB_STATE: {"type": "number", "initial": 0}, LEAVING_STATE: {"type": "number", "initial": -1}})
    width = U * CONTENT_W
    chrome, pages, firsts = [], [], []
    if tabbed:
        strip = tab_strip(doc, width, [(ident, key, None) for ident, key, _columns in tabs], top=0, state=TAB_STATE)
        chrome.append(group(f"{prefix}-strip", absolute(left=U * CONTENT_LEFT, top=U * STRIP_TOP, width=width, height=2 * TAB_RISE),
                            strip))
    else:
        chrome.append(header_band(f"{prefix}-header", CONTENT_LEFT, STRIP_TOP, CONTENT_W, ROWS_TOP - STRIP_TOP - 4))
    if note is not None:
        chrome.append(heading_label(f"{prefix}-note", note, U * CONTENT_LEFT, U * HEAD_TOP, width, U * HEAD_H))
    for index, (ident, _key, columns) in enumerate(tabs):
        if len(columns) > 2:
            raise SystemExit(f"{prefix}'s {ident} tab has {len(columns)} columns; a settings tab holds two at most")
        current = {"op": "==", "args": [{"state": TAB_STATE}, index]}
        parts, order = [], []
        for side, column in enumerate(columns):
            left = U * (CONTENT_LEFT + side * (COLUMN_W + COLUMN_GAP))
            items = list(column)
            if items and isinstance(items[0], tuple):
                top = BAND_HEAD_TOP if not tabbed else HEAD_TOP
                parts.append(heading_label(f"{prefix}-{ident}-head-{side}", items.pop(0)[1], left, U * top, U * COLUMN_W, U * HEAD_H))
            flow, rows = [], []
            for position, item in enumerate(items):
                if isinstance(item, tuple):
                    flow.append(heading_label(f"{prefix}-{ident}-head-{side}-{position}", item[1], 0, 0, U * COLUMN_W, U * ROW_PITCH))
                    flow[-1]["properties"].update({"position": keyword("relative"), "display": keyword("block"),
                                                   "flex-shrink": number(0)})
                    continue
                flow.append(item)
                rows.append(item)
            if len(flow) > COLUMN_SLOTS:
                raise SystemExit(f"{prefix}'s {ident} tab puts {len(flow)} slots in a column of {COLUMN_SLOTS}")
            parts.append(group(f"{prefix}-{ident}-column-{side}", {**absolute(left=left, top=U * ROWS_TOP, width=U * COLUMN_W,
                                                                             height=U * ROW_PITCH * len(flow)),
                                                                 "display": keyword("flex"), "flex-direction": keyword("column")}, flow))
            order.append(rows)
        # Up and Down read down the first column and on down the second,
        # wrapping; Left and Right cross to the row beside.
        sequence = [row["id"] for rows in order for row in rows]
        for position, control in enumerate(sequence):
            node = next(row for rows in order for row in rows if row["id"] == control)
            navigation = node["control"].setdefault("navigation", {})
            navigation["down"] = sequence[(position + 1) % len(sequence)]
            navigation["up"] = sequence[position - 1] if position else (f"tab-{ident}" if tabbed else sequence[-1])
        if len(order) == 2 and order[1]:
            for side, rows in enumerate(order):
                other = order[1 - side]
                for slot, row in enumerate(rows):
                    row["control"]["navigation"]["right" if side == 0 else "left"] = other[min(slot, len(other) - 1)]["id"]
        firsts.append(sequence[0])
        if tabbed:
            for control in sequence:
                _enable_while(doc, control, current)
            shown = {"op": "||", "args": [current, {"op": "==", "args": [{"state": LEAVING_STATE}, index]}]}
            doc.bind(f"{prefix}-page-{ident}.display", f"{prefix}-page-{ident}", "display", {"op": "select", "args": [shown, "block", "none"]})
        pages.append(group(f"{prefix}-page-{ident}", {**FULL, "display": keyword("none" if tabbed and index else "block"),
                                                      "opacity": number(1)}, parts))
    if tabbed:
        _tab_programs(doc, prefix, tabs, firsts)
    focus_current = {"op": "focus", "control": firsts[-1]}
    steps = [focus_current]
    for index in range(len(firsts) - 2, -1, -1):
        steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": TAB_STATE}, index]},
                  "then": [{"op": "focus", "control": firsts[index]}], "else": steps}]
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    back = page_prompts(doc, f"{prefix}_back", action="back")
    return subpage_document(doc, prefix, CRUMB, title_key, firsts[0], [*chrome, *pages, back], list(overlays),
                            activate=activate(steps) if activate else steps, backing_node=settings_backing(f"{prefix}-backing"))


def show_tab(doc: Document, prefix: str, tabs: list, index: int) -> list:
    """Steps that make tab `index` current at once, without the cross-fade:
    for an arrival that must land on a row in another tab."""
    return [{"op": "setState", "values": {TAB_STATE: index, LEAVING_STATE: -1}},
            {"op": "playTimeline", "timeline": f"place-{tabs[index][0]}"}]


def _tab_programs(doc: Document, prefix: str, tabs: list, firsts: list) -> None:
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
                      {"op": "playTimeline", "timeline": f"show-{ident}"}, {"op": "focus", "control": firsts[index]}]},
        ]
    doc.events["pageShown"] = [{"op": "setState", "values": {LEAVING_STATE: -1}}]
    for name, step in (("onTabPrevious", -1), ("onTabNext", 1)):
        steps: list = []
        for index in reversed(range(count)):
            target = tabs[(index + step) % count][0]
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": TAB_STATE}, index]},
                      "then": [{"op": "call", "event": f"tab_{target}"}], **({"else": steps} if steps else {})}]
        doc.events[name] = steps
