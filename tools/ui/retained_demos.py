"""The retained Demos sub-page (docs/dev/ui-visual-design.md sections 9 and
13.13, FLOW-013, FLOW-050): DEMOS on the Multiplayer page goes one level
deeper to it, as CREATE SERVER and JOIN GAME do to theirs.

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The page is the Load Game page's construction for
the demo library openQ4's legacy browser (guis/demo_menu.gui) shows: the
selected demo's map in the stock preview, 183x137 u at 25,78 u, with its kind
and status below it; PLAY, DELETE and REFRESH as section navigation plates
under them; the demos on the trailing side under Lowpixel NAME, MAP, DATE and
DURATION headings and an olive rule, with filter tags above (section 13.13:
small header-band plates, the set one at 0.60 with its close cross, the rest
at 0.28); the prompt bar in the bottom band, its BACK where the stock BACK
plate stood at 532,441 u.

The session publishes the library newest first, filtered (the legacy
browser's MULTI-VIEW, RENDER, LEGACY and INCOMPLETE; none set shows them
all), ROWS rows at a time with NEWER and OLDER rows paging through more. A
row's first Accept selects its demo; Accept on the selected demo, or PLAY,
plays it where it can play. DELETE asks with the stock confirmation, which
names the demo, and the demo that takes the deleted one's place is selected.
With no demos the list's place is the empty state of section 13.13: a heading
and one sentence, which says so when a filter hides every demo and then offers
CLEAR FILTERS, the action that fixes it.

It stands in the sub-page frame (retained_subpage): MULTIPLAYER as the crumb,
DEMOS carried to the step's foot, and Back climbing out onto DEMOS.
"""
from __future__ import annotations

from build_retained_screens import (LANGUAGES, OLIVE, ORANGE, U, VERB_DELETE, VERB_PLAY, Document, absolute, action_plate, colour,
                                    confirmation, fixed_text, group, keyword, label, length, number, objectives_scrollbar, page_prompts,
                                    path, rgb, row_prompt, solid, stroke, text_width, typeface, vector)
from retained_saves import (ERROR, ERROR_LINE, ERROR_STRIP, HEADING, LIST_H, LIST_LEFT, LIST_TOP, LIST_W, PLACEHOLDER, PREVIEW,
                            ROW_H, ROW_W, RULE_Y, bind_selected, grow_details, grow_empty, grow_rows, lines_needed, pager_row,
                            picture_frame, row_plates, row_states, section_plate, table_text)
from retained_subpage import subpage_document

ROWS = 64                      # RETAINED_DEMO_ROWS in the session: the demos the list holds at a time
TITLE = "#str_41500"           # DEMOS, the Multiplayer page's section that opens the sub-page
DETAIL_LEFT, DETAIL_W, KIND_TOP, STATUS_TOP, NOTE_TOP = 37.0, 165.0, 215.0, 230.0, 245.0
# The plates and headings in capitals, as Join Game's and the save pages'
# are (RmlUi's text-transform changes ASCII letters only): the pages' PLAY
# and DELETE verbs, the stock capital MAP and DATE, and openQ4's REFRESH,
# NAME and DURATION (the stock capital NAME names a player in Ukrainian, and
# REFRESH LIST runs past a plate in German).
PLATES = (("demo_play", "#str_231523", 21.0, 256.0, "play"), ("demo_delete", "#str_231521", 20.0, 293.0, None),
          ("demo_refresh", "#str_231536", 20.0, 330.0, "refresh"))
# The columns' tabstops across the 394 u row (the scrollbar takes the last 16 u).
COLUMNS = (("name", "#str_231530", 0.0, 146.0), ("map", "#str_200040", 150.0, 96.0), ("date", "#str_200243", 250.0, 72.0),
           ("length", "#str_231531", 324.0, 70.0))
HEAD_TOP = 63.0
# The filter tags over the headings: the legacy browser's filters after ALL,
# which no tag set stands for (demos.filter 1-4, as demoFilter names them).
TAGS = (("tag-mvd", "#str_41503"), ("tag-render", "#str_41504"), ("tag-legacy", "#str_41505"), ("tag-incomplete", "#str_41506"))
TAG_TOP, TAG_H, TAG_GAP, TAG_TEXT = 43.0, 15.0, 4.0, 14.0
TAG_LEAD, TAG_CROSS = 8.0, 9.0           # u: the cut and text inset before the label, the cross's slot after it
TAG_SET, TAG_UNSET = 0.6, 0.28
ROW_TEXT = 15.8                          # dp: the stock list's textscale 0.22
TINTS = {"name": [1, 1, 1, 0.9], "map": [1, 1, 1, 0.65], "date": [1, 1, 1, 0.65], "length": [1, 1, 1, 0.65]}


def tag_width(key: str) -> float:
    """A tag's width in u: its cut and inset, its longest translation and
    the slot its close cross takes."""
    widest = max(text_width("lowpixel", text, TAG_TEXT) for text in table_text(key).values())
    return round(TAG_LEAD + widest / U + 4 + TAG_CROSS, 1)


def filter_tag(doc: Document, ident: str, key: str, index: int, left: float, width: float) -> dict:
    """A filter tag (section 13.13): a small header-band plate, its lower
    leading corner cut at 45 degrees, its olive rail along the foot, at 0.28
    while unset and 0.60 with its close cross while set. The pointer or focus
    lights it and turns its label orange. Accept sets its filter, or clears
    it while set."""
    w, h, cut = U * width, U * TAG_H, U * 5
    shape = [(0, 0), (w, 0), (w, h), (cut, h), (0, h - cut)]
    rail = [(0.75, 0), (0.75, h - cut), (cut, h - 0.75), (w, h - 0.75)]
    whole = absolute(left=0, top=0, width=w, height=h)

    def plate(suffix: str, alpha: float, shown: bool) -> dict:
        return vector(f"{ident}-{suffix}", {**whole, "opacity": number(alpha), "pointer-events": keyword("none"),
                                            "display": keyword("block" if shown else "none")}, [
            path("fill", shape, fill=solid([0, 0, 0, 1])),
            path("rail", rail, closed=False, stroke=stroke(solid(rgb(OLIVE)), 1.2))])
    unset, set_plate = plate("plate", TAG_UNSET, True), plate("plate-set", TAG_SET, False)
    hot = vector(f"{ident}-hot", {**whole, "opacity": number(0), "pointer-events": keyword("none")}, [
        path("fill", shape, fill=solid(rgb(OLIVE, 0.35)))])
    x0, size = w - U * (TAG_CROSS - 2), U * 4
    cross = vector(f"{ident}-cross", {**whole, "pointer-events": keyword("none"), "display": keyword("none")}, [
        path("a", [(x0, h / 2 - size / 2), (x0 + size, h / 2 + size / 2)], closed=False, stroke=stroke(solid([1, 1, 1, 0.8]), 1.2)),
        path("b", [(x0, h / 2 + size / 2), (x0 + size, h / 2 - size / 2)], closed=False, stroke=stroke(solid([1, 1, 1, 0.8]), 1.2))])
    text_node = label(f"{ident}-label", key, {**absolute(left=U * TAG_LEAD, top=0, width=w - U * (TAG_LEAD + TAG_CROSS), height=h),
                      **typeface("lowpixel", TAG_TEXT, h, [1, 1, 1, 0.8]), "white-space": keyword("nowrap"),
                      "pointer-events": keyword("none")})
    chosen = {"op": "==", "args": [{"state": "demos.filter"}, index]}
    doc.bind(f"{ident}-plate.display", f"{ident}-plate", "display", {"op": "select", "args": [chosen, "none", "block"]})
    doc.bind(f"{ident}-plate-set.display", f"{ident}-plate-set", "display", {"op": "select", "args": [chosen, "block", "none"]})
    doc.bind(f"{ident}-cross.display", f"{ident}-cross", "display", {"op": "select", "args": [chosen, "block", "none"]})
    event = f"filterTag{index}"
    doc.events[event] = [{"op": "setState", "values": {"demos.filter": {"op": "select", "args": [chosen, 0, index]}}},
                         {"op": "action", "action": "filter"}]
    lit, rest = number(1), number(0)
    ids = doc.states(ident, {
        "default": [(f"{ident}-hot", "opacity", rest), (f"{ident}-label", "color", colour([1, 1, 1, 0.8]))],
        "hover": [(f"{ident}-hot", "opacity", lit), (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "focus": [(f"{ident}-hot", "opacity", lit), (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-hot", "opacity", lit), (f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-hot", "opacity", rest), (f"{ident}-label", "color", colour([1, 1, 1, 0.3]))],
    })
    return group(ident, absolute(left=U * left, top=U * TAG_TOP, width=w, height=h),
                 [unset, set_plate, hot, cross, text_node],
                 control={"role": "button", "label": key, "event": event, "states": ids})


def demo_row(doc: Document, index: int) -> dict:
    """One of the list's rows: the demo's name, map, date and duration at
    the column tabstops, Lowpixel at the stock list's textscale 0.22 on its
    20 u pitch. Its first Accept selects the demo; Accept on the selected
    demo plays it where it can play."""
    ident, key = f"row-{index}", f"demos.row{index}"
    width, height = U * ROW_W, U * ROW_H
    cell = {"white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")}
    cells = []
    for column, _, left, column_w in COLUMNS:
        doc.state[f"{key}.{column}"] = {"type": "string", "initial": ""}
        node = f"{ident}-{column}"
        cells.append(label(node, PLACEHOLDER, {**absolute(left=U * (left + (4 if left == 0 else 0)), top=0,
                                                           width=U * (column_w - (4 if left == 0 else 0)), height=height),
                                               **typeface("lowpixel", ROW_TEXT, height, TINTS[column]), **cell}))
        doc.bind(f"{node}.text", node, "text", {"state": f"{key}.{column}"})
    demo = {"op": "+", "args": [{"state": "demos.first"}, index]}
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [demo, {"state": "demos.count"}]}, "block", "none"]})
    bind_selected(doc, ident, {"op": "==", "args": [{"state": "demos.selected"}, demo]})
    event = f"demosRow{index}"
    doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": "demos.selected"}, demo]},
                          "then": [{"op": "if", "condition": {"state": "demos.canPlay"}, "then": [{"op": "action", "action": "play"}]}],
                          "else": [{"op": "setState", "values": {"demos.pick": index}}, {"op": "action", "action": "select"}]}]
    ids = row_states(doc, ident, tuple(f"{ident}-{column}" for column, *_ in COLUMNS), tuple(TINTS[column] for column, *_ in COLUMNS))
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                         "flex-shrink": number(0)},
                 [*row_plates(ident, width, height, selectable=True), *cells],
                 control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids,
                          "prompt": row_prompt(VERB_PLAY, {"op": "&&", "args": [
                              {"op": "==", "args": [{"state": "demos.selected"}, demo]}, {"state": "demos.canPlay"}]})})


def check_demo_text_fits() -> None:
    """Every shipped language's text fits its box; generation fails otherwise."""
    tags = sum(tag_width(key) for _, key in TAGS) + TAG_GAP * (len(TAGS) - 1)
    if tags > ROW_W:
        raise SystemExit(f"the filter tags need {tags:.0f} u of {ROW_W:.0f}")
    one_line = [(key, "marine", 22.3, U * 186) for _, key, *_ in PLATES]
    one_line += [(key, "lowpixel", 13, U * (column_w - 4)) for _, key, _, column_w in COLUMNS]
    one_line += [("#str_231114", "marine", 20, U * ROW_W), ("#str_231116", "marine", 16, U * (ROW_W - 18)),
                 ("#str_231117", "marine", 16, U * (ROW_W - 18)),
                 (TITLE, "marine", 18, U * 300), ("#str_41556", "marine", 18, 316)]
    wrapped = [("#str_231115", "lowpixel", 17, U * ROW_W, 2), ("#str_231118", "lowpixel", 17, U * ROW_W, 2),
               ("#str_231119", "lowpixel", 15, U * (ERROR_STRIP[2] - 8), 3), ("#str_231120", "lowpixel", 15, U * (ERROR_STRIP[2] - 8), 3)]
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


def demos_document() -> dict:
    check_demo_text_fits()
    doc = Document("openq4.demos")
    doc.state.update({
        "demos.count": {"type": "number", "initial": 0},
        "demos.first": {"type": "number", "initial": 0},
        "demos.selected": {"type": "number", "initial": -1},
        "demos.pick": {"type": "number", "initial": -1},
        "demos.filter": {"type": "number", "initial": 0},
        "demos.shot": {"type": "string", "initial": ""},
        "demos.kind": {"type": "string", "initial": ""},
        "demos.status": {"type": "string", "initial": ""},
        "demos.detail": {"type": "string", "initial": ""},
        "demos.playable": {"type": "boolean", "initial": False},
        "demos.canPlay": {"type": "boolean", "initial": False},
        "demos.canDelete": {"type": "boolean", "initial": False},
        "demos.error": {"type": "string", "initial": ""},
        "demos.confirm": {"type": "string", "initial": ""},
    })
    for action, command in (("back", "demosBack"), ("select", "demosSelect"), ("play", "demosPlay"), ("delete", "demosDelete"),
                            ("refresh", "demosRefresh"), ("filter", "demosFilter"), ("newer", "demosNewer"), ("older", "demosOlder")):
        doc.session(action, command)
    doc.events["onBack"] = [{"op": "action", "action": "back"}]

    # The leading side: the selected demo's map, its kind and status, and the
    # section plates.
    shot_parts = picture_frame("demo-shot", *PREVIEW)
    doc.bind("demo-shot.image", "demo-shot", "image", {"state": "demos.shot"})
    error_box = absolute(left=U * ERROR_STRIP[0], top=U * ERROR_STRIP[1], width=U * ERROR_STRIP[2], height=U * ERROR_STRIP[3])
    error = group("demo-error", {**error_box, "display": keyword("none"), "background-color": colour([0, 0, 0, 0.8])}, [
        label("demo-error-text", PLACEHOLDER, {**absolute(left=U * 4, top=U * 1, width=U * (ERROR_STRIP[2] - 8), height=U * (ERROR_STRIP[3] - 2)),
              **typeface("lowpixel", 15, ERROR_LINE, rgb(ERROR)), "white-space": keyword("normal"), "overflow": keyword("hidden")})])
    doc.bind("demo-error.display", "demo-error", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "demos.error"}, ""]}, "block", "none"]})
    doc.bind("demo-error-text.text", "demo-error-text", "text", {"state": "demos.error"})
    stamp = {"white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    kind = label("demo-kind", PLACEHOLDER, {**absolute(left=U * DETAIL_LEFT, top=U * KIND_TOP, width=U * DETAIL_W, height=U * 16),
                 **typeface("lowpixel", 18, U * 16, [1, 1, 1, 0.8]), **stamp})
    status = label("demo-status", PLACEHOLDER, {**absolute(left=U * DETAIL_LEFT, top=U * STATUS_TOP, width=U * DETAIL_W, height=U * 16),
                   **typeface("lowpixel", 18, U * 16, [1, 1, 1, 0.8]), **stamp})
    note = label("demo-detail", PLACEHOLDER, {**absolute(left=U * DETAIL_LEFT, top=U * NOTE_TOP, width=U * DETAIL_W, height=U * 16),
                 **typeface("lowpixel", 18, U * 16, [1, 1, 1, 0.6]), **stamp})
    doc.bind("demo-kind.text", "demo-kind", "text", {"state": "demos.kind"})
    doc.bind("demo-status.text", "demo-status", "text", {"state": "demos.status"})
    doc.bind("demo-detail.text", "demo-detail", "text", {"state": "demos.detail"})
    # A demo this build cannot play says so in the error color.
    doc.bind("demo-status.color", "demo-status", "color",
             [{"op": "select", "args": [{"state": "demos.playable"}, 1, 0.8941]},
              {"op": "select", "args": [{"state": "demos.playable"}, 1, 0.4275]},
              {"op": "select", "args": [{"state": "demos.playable"}, 1, 0.3373]},
              0.8])
    plates = []
    for ident, key, left, top, action in PLATES:
        plates.append(section_plate(doc, ident, key, left, top, action=action, event=None if action else "deleteModalShow"))
        if ident in ("demo_play", "demo_delete"):
            plates[-1]["control"]["prompt"] = VERB_PLAY if ident == "demo_play" else VERB_DELETE
    doc.bind("demo_play.enabled", "demo_play", "enabled", {"state": "demos.canPlay"})
    doc.bind("demo_delete.enabled", "demo_delete", "enabled", {"state": "demos.canDelete"})

    # The trailing side: the filter tags, the headings over an olive rule,
    # then the list.
    tags, left = [], LIST_LEFT
    for index, (ident, key) in enumerate(TAGS, start=1):
        width = tag_width(key)
        tags.append(filter_tag(doc, ident, key, index, left, width))
        left += width + TAG_GAP
    heading = {**typeface("lowpixel", 13, U * 14, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    headings = [label(f"demo-head-{column}", key, {**absolute(left=U * (LIST_LEFT + max(0.0, left_u - 1)), top=U * HEAD_TOP,
                                                              width=U * (column_w - 4), height=U * 14), **heading})
                for column, key, left_u, column_w in COLUMNS]
    headings.append(vector("demo-rule", absolute(left=U * LIST_LEFT, top=U * RULE_Y - 0.6, width=U * LIST_W, height=1.2), [
        path("rule", [(0, 0.6), ({"fraction": 1}, 0.6)], closed=False, stroke=stroke(solid(rgb(OLIVE, 0.26)), 1.2))]))
    rows = [demo_row(doc, index) for index in range(ROWS)]
    newer = pager_row(doc, "page-newer", "#str_231116", "newer", {"op": ">", "args": [{"state": "demos.first"}, 0]})
    older = pager_row(doc, "page-older", "#str_231117", "older",
                      {"op": "<", "args": [{"op": "+", "args": [{"state": "demos.first"}, ROWS]}, {"state": "demos.count"}]})
    column = group("demo-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")},
                   [newer, *rows, older])
    # Above 100 % text the rows and the selected demo's lines grow with it
    # (section 2's reflow); the headings, tags and plates are furniture and
    # keep their size.
    grow_rows(doc, column)
    grow_details(doc, [*shot_parts, error, kind, status, note], "demo-shot", "demo-error",
                 (("demo-kind",), ("demo-status",), ("demo-detail",)))
    for node in [*headings, *tags, *plates]:
        fixed_text(doc, node)
    viewport = group("demo-list", {**absolute(left=U * LIST_LEFT, top=U * LIST_TOP, width=U * ROW_W, height=U * LIST_H),
                                   "overflow": keyword("auto")}, [column])
    scrollbar = objectives_scrollbar(doc, "demo_scroll", "demo-list", OLIVE, ORANGE, U * (LIST_LEFT + ROW_W + 8) - 18, U * LIST_TOP,
                                     U * LIST_H, line_step=U * ROW_H, key=TITLE)

    # No demos: the empty state of section 13.13 in the list's place; its
    # sentence says when a filter hides them all, and CLEAR FILTERS clears it.
    clear_w = max(180.0, round(max(text_width("marine", text, 20) for text in table_text("#str_231422").values()) + 50))
    clear = action_plate(doc, "demo_emptyclear", "#str_231422", 0, U * 74, event="clearDemoFilter", width=clear_w)
    clear["properties"]["display"] = keyword("none")
    doc.events["clearDemoFilter"] = [{"op": "setState", "values": {"demos.filter": 0}}, {"op": "action", "action": "filter"}]
    doc.bind("demo_emptyclear.display", "demo_emptyclear", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "demos.filter"}, 0]}, "block", "none"]})
    empty = group("demo-empty", {**absolute(left=U * LIST_LEFT, top=U * LIST_TOP, width=U * ROW_W, height=U * 150), "display": keyword("none")}, [
        label("demo-empty-heading", "#str_231114", {**absolute(left=0, top=U * 10, width=U * ROW_W, height=U * 16),
              **typeface("marine", 20, U * 16, HEADING), "white-space": keyword("nowrap")}),
        label("demo-empty-text", "#str_231115", {**absolute(left=0, top=U * 32, width=U * ROW_W, height=U * 36),
              **typeface("lowpixel", 17, 22, [1, 1, 1, 0.8]), "white-space": keyword("normal"), "overflow": keyword("hidden")}),
        clear,
    ])
    doc.bind("demo-empty.display", "demo-empty", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "demos.count"}, 0]}, "block", "none"]})
    doc.bind("demo-empty-text.text", "demo-empty-text", "text",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "demos.filter"}, 0]}, "#str_231115", "#str_231118"]})
    grow_empty(doc, empty, ("#str_231115", "#str_231118"), ("demo_emptyclear",))

    back = page_prompts(doc, "demo_back", action="back", verbs=(VERB_PLAY, VERB_DELETE))
    modal = confirmation(doc, "deleteModal", "#str_41556", PLACEHOLDER, None, yes_event="deleteConfirmed")
    doc.bind("deleteModal-body.text", "deleteModal-body", "text", {"state": "demos.confirm"})
    doc.events["deleteConfirmed"] = [{"op": "call", "event": "deleteModalHide"}, {"op": "action", "action": "delete"}]

    # Each arrival focuses the first row (the newest demo, selected), else
    # REFRESH; the last request wins.
    focus = [{"op": "focus", "control": "demo_refresh"},
             {"op": "if", "condition": {"op": ">", "args": [{"state": "demos.count"}, 0]}, "then": [{"op": "focus", "control": "row-0"}]}]
    doc.events["pagedOlder"] = [{"op": "focus", "control": "row-0"}]
    doc.events["pagedNewer"] = [{"op": "focus", "control": f"row-{ROWS - 1}"}]
    # The plates come after the panel's other parts, so its full-size box
    # never takes their hits.
    return tall_demos_page(subpage_document(doc, "demo", "#str_200002", TITLE, "demo_refresh",
                                            [*shot_parts, error, kind, status, note, *tags, *headings, viewport, scrollbar, empty,
                                             back, *plates], [modal], stretch=("demo-list",), activate=focus))


# A tall view's Demos page (retained_tall, as the Load page's): the filter
# tags under the band, the selected demo's picture with its kind, status and
# detail and the PLAY, DELETE and REFRESH plates beside it, then the headings
# and the list. A row's name takes the first line and its map, date and
# duration the second.
DEMO_LINES = ((("name", 0.0, 1.0),), (("map", 0.0, 0.44), ("date", 0.45, 0.3), ("length", 0.76, 0.24)))


def tall_demos_page(result: dict) -> dict:
    from retained_saves import BACKING_ALPHA, TALL_GROW, tall_detail, tall_empty, tall_filters, tall_list
    from retained_tall import CONTENT_W, TallLayout
    tall = TallLayout(result)
    list_w = CONTENT_W - 40
    # Tag widths use tenths of a unit. Quantize their total too: Python 3.12's
    # compensated sum otherwise changes the serialized scale's final bits.
    span = round(sum(tag_width(key) for _, key in TAGS) + TAG_GAP * (len(TAGS) - 1), 1)
    tall_filters(tall, tuple(ident for ident, _key in TAGS), (LIST_LEFT, TAG_TOP, span), list_w)
    below = tall_detail(tall, "demo", PREVIEW, ("demo-kind", "demo-status", "demo-detail"), (DETAIL_LEFT, KIND_TOP), PLATES)
    heads = {column: (U * (LIST_LEFT + max(0.0, left - 1)), U * HEAD_TOP) for column, _key, left, _width in COLUMNS}
    list_top = tall_list(tall, "demo", DEMO_LINES, list_w, heads, below + 30)
    tall_empty(tall, "demo", (LIST_LEFT, LIST_TOP), list_top, list_w)
    tall.dialogs(TALL_GROW)
    tall.shade("demo-backing", BACKING_ALPHA)
    return result
