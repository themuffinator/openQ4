"""The retained Join Game sub-page, Multiplayer's server browser
(docs/dev/ui-visual-design.md sections 8 and 9, "Menu levels" and "Lists";
FLOW-050).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. JOIN GAME on the Multiplayer page goes one level
deeper to guis/menu/joingame.q4ui as CREATE SERVER goes to Create Server,
on the same sub-page frame (retained_subpage); Back climbs out onto JOIN
GAME.

The content is the stock p_mp_browse's over the stock page backing: MOD with
its spinner at 27,46 u and SOURCE with its INTERNET and LAN tags at 324,46 u;
the server list at 23,83 u under its column headings on the stock 18 u rows,
ten of them, the sort's column marked (a heading sorts by its column, and
again the other way); under it the filter row (section 7's filter tags): the
game type's spinner and the NOT FULL, NOT EMPTY and NO PASSWORD tags; the
scan's status at 41,291 u, a player search's tag after it, and CLEAR SORTING; the scan's progress bar and its notice at 21,312 u; REFRESH LIST, GET NEW LIST, JOIN IP ADDRESS and FIND
PLAYER/CLAN from 19,330 u; the chosen server's levelshot at 240,337 u; JOIN
SERVER at 385,330 u and the favorite toggle under it; the prompt bar in the
bottom band, its BACK where the stock BACK plate stood. JOIN IP ADDRESS and
FIND PLAYER/CLAN open their dialogs (section 6's entry dialog): an address the
session connects to, and a name it searches the list for. The list's symbols
(favorite, password, dedicated server, repeater) sit on their tiles as section
9's symbol table draws them; the stock PunkBuster column is gone (a
discontinued service, no replacement column).

The session owns the list: it publishes the scan's visible servers a page of
JOIN_ROWS at a time (join.row<i>.*), their count, the scan's status, the
source, the mod, game type and filter tags' filters and the sort, and the
chosen server (join.selected,
its levelshot and whether it is a favorite), and each control asks through
its own verb. It offers all the stock page does, so the session presents it
by default (RETAINED_JOIN_GAME_MISSING is empty).
"""
from __future__ import annotations

import math

from build_retained_screens import (FULL, OLIVE, ORANGE, PROGRESS, U, VALUE, VERB_JOIN, Document, absolute, action_plate,
                                    colour, confirmation, device_glyphs, group, keyword, label, length, linear, number,
                                    objectives_scrollbar,
                                    page_prompts, path, rgb, solid, stroke, text_width, track, typeface, vector)
from retained_create_server import header_band, text_row
from retained_mp_menus import stack_row
from retained_difficulty import check_fits, primary_plate
from retained_saves import (BACKING_ALPHA, ERROR, HEADING, PLACEHOLDER, bind_selected, grow_rows, pager_row, picture_frame, row_plates,
                           row_states, table_text)
from retained_subpage import subpage_document

JOIN_ROWS = 64                             # RETAINED_JOIN_ROWS in the session
# u: mp_browse_serverlist, ten rows high (the stock 202 u less a row and a
# little) so the filter row stands under it
LIST = (23.0, 83.0, 604.0, 180.0)
ROW_H = 18.0                               # u: its itemheight
REVEAL_MS = 120                            # row.reveal (section 8): each row's fade
REVEAL_STAGGER_MS = 15                     # one row after another, capped at 90 ms in all
REVEAL_STAGGER_CAP_MS = 90
HEADER = (22.0, 54.0, 587.0, 37.0)         # u: mp_browse_header
HEADING_TOP = 70.0                         # u: the headings' row
# The columns: the session's sort for each (serverSort_t), its heading (a
# symbol column has none), its tab stop and width in the list (u) and its sort
# verb. The symbol columns keep the stock list's tab stops, the repeater's
# after PING as the stock list had it; without the PunkBuster column the name
# starts at its stop. The stock columns after the name were too narrow for
# every language's heading with the sort's chevron, so they take what the
# name column spared.
COLUMNS = (
    ("fav", 6, None, 5.0, 21.0, "joinSortFavorite"),
    ("lock", 7, None, 26.0, 21.0, "joinSortLocked"),
    ("ded", 8, None, 47.0, 21.0, "joinSortDedicated"),
    ("name", 1, "#str_200036", 68.0, 211.0, "joinSortName"),
    ("ping", 0, "#str_200037", 279.0, 40.0, "joinSortPing"),
    ("rep", 10, None, 319.0, 21.0, "joinSortRepeater"),
    ("players", 2, "#str_200038", 340.0, 60.0, "joinSortPlayers"),
    ("type", 3, "#str_200039", 400.0, 90.0, "joinSortGameType"),
    ("map", 4, "#str_200040", 490.0, 96.0, "joinSortMap"),
)
SYMBOL = 16.0                              # dp: a list symbol's tile
# The stock list symbols' colors (gfx/guis/mainmenu/icon_*): the server
# case's gray and its green power light, the television's gray and its
# color bars.
CASE_GRAY, CASE_PANEL, POWER_GREEN = "#A5AAB5", "#394D52", "#18BE08"
TV_GRAY, TV_SCREEN, TV_BARS = "#ADAAA5", "#141414", ("#B4B45A", "#31DBA5", "#CE1052", "#08088A")
# Section 13.13's network quality beside the ping: four bars rising like
# slider ticks from the column's start, lit up to the row's quality in the
# status color of its worst (success at three and four, warning at two, error
# at one), the rest faint; the ping follows them.
QUALITY_BARS = 4
BAR_W, BAR_GAP, BAR_LOW, BAR_STEP = 1.6, 0.9, 2.4, 1.6     # u: width, gap, the first bar's height, each next one's rise
BAR_BOTTOM = 12.2                          # u: the bars stand on the row text's baseline
QUALITY_COLORS = ("#E46D56", "#E3AD36", "#B5C784")         # status.error, status.warning, status.success
BAR_UNLIT = [1, 1, 1, 0.18]
MOD = (27.0, 46.0, 59.0)                   # u: mp_browse_mod, "MOD:" right-aligned, clear of the spinner
MOD_VALUE = (113.0, 150.0)                 # u: mp_browse_filter_mod, inside the spinner squares
MOD_SPIN = ((88.0, "joinModPrevious", "previous"), (266.0, "joinModNext", "next"))  # u: the spinner squares, 22x16
# u: mp_browse_info_mapshot, 20 u left of the stock 260 u so the favorite
# toggle, as wide as its longest translation, clears it
SHOT = (240.0, 337.0, 116.0, 88.0)
FAVORITE_TOP = 400.0                       # u: mp_b_browse_favsel
FAVORITE_KEYS = ("#str_200292", "#str_200293")  # ADD TO FAVORITES, REMOVE FAVORITE
SPECTATE_KEY = "#str_200195"               # SPECTATE, the stock label in every language
STICK_GLYPH = 22.0                         # dp: a stick press's glyph past SPECTATE and the favorite toggle
SOURCE = (324.0, 46.0)                     # u: mp_browse_source
SOURCE_END = 613.0                         # u: where the stock LAN tag ends
# The source tags: right-aligned at SOURCE_END, each as wide as its longest
# translation (the stock 50 u LAN tag held "LAN" only).
TAG_KEYS = (("internet", "#str_200033", "joinSourceInternet", False), ("lan", "#str_200034", "joinSourceLan", True))
TAG_GAP = 6.0
STATUS = (41.0, 291.0, 175.0)              # u: mp_browse_t_sortnotice; every language's status fits 175 u
# The filter row under the list (section 7's filter tags, section 13.7's
# lists): the game type's spinner from the list's leading edge, its value
# ALL GAME TYPES while it shows every type, and the tags ending where the
# source tags do, each as wide as its longest translation with room for the set
# tag's close cross. A tag's verb toggles its filter, which the session
# publishes as the tag's state.
FILTER_TOP = 265.0
FILTER_TAGS = (("full", "#str_231296", "joinFilterFull", "join.filterFull"),
               ("empty", "#str_231297", "joinFilterEmpty", "join.filterEmpty"),
               ("password", "#str_231298", "joinFilterPassword", "join.filterPassword"))
FILTER_ALL_TYPES = "#str_231299"
FILTER_CROSS = 14.0                        # u: the close cross's room at a tag's end
GAMETYPE_SPIN = ("joinGameTypePrevious", "joinGameTypeNext")
SEARCH_TAG_LEFT = 222.0                    # u: a player search's tag, after the status
# The scan's progress under the list: the stock load_bar3 tube, its track at
# 0.30 and its fill at 0.50 in marine.progress, with the notice over it.
LOAD_BAR = (21.0, 312.0, 593.0, 16.0)      # u: mp_browse_loadbarbg and mp_browse_p_loadbar
LOAD_NOTICE = (41.0, 311.0, 569.0)         # u: mp_browse_t_loadnotice (textscale 0.22, white 0.80)
# load_bar3's alpha down the tube, sampled from its 14 inner texel rows (the
# first and last are clear): a bright top edge, dim under the notice, a
# lighter foot.
LOAD_PROFILE = ((0.0, 1.0), (0.15, 0.85), (0.31, 0.6), (0.46, 0.4), (0.62, 0.31), (0.77, 0.39), (0.85, 0.48), (1.0, 0.7))
# CLEAR SORTING ends where the stock plate does (615 u) and is as wide as its
# longest translation needs, at least the stock 181 u; the status line runs
# to 6 u before it.
CLEAR_SORT = (615.0, 286.0, 181.0)         # u: right end, top, least width
# The list's actions down the leading column (the stock mp_browse_b1-b3, b5),
# on a 26 u pitch so the fourth clears the bottom band: a session verb, or an
# event opening a dialog. Each plate takes the longest label of any language.
LIST_X = 19.0
LIST_ACTIONS = (("join_refresh", "#str_200028", 330.0, "joinRefresh", None), ("join_newlist", "#str_200029", 356.0, "joinNewList", None),
                ("join_ip", "#str_200025", 382.0, None, "joinIpOpen"), ("join_find", "#str_200182", 408.0, None, "findOpen"))
ADDRESS_BYTES = 99                         # pop_joinIp_address's maxchars
FIND_BYTES = 31                            # a player name's (MAX_NICKLEN - 1)
ENTRY_ROW = 24.0                           # u: an entry dialog's field row
JOIN = (385.0, 330.0, 230.0, 37.0)         # u: mp_browse_b4 (JOIN SERVER)
CELL_TEXT = 14.0                           # dp: the stock list's 0.2 textscale
HEADING_TEXT = 12.0
CHEVRON_ROOM = 10.0                        # dp: the sort's chevron after a heading
TAG_TEXT = 15.0


def circle_points(cx: float, cy: float, radius: float, start: float, end: float, steps: int) -> list:
    """Points along an arc, from `start` to `end` degrees (0 up, clockwise)."""
    return [(round(cx + radius * math.sin(math.radians(start + (end - start) * step / steps)), 3),
             round(cy - radius * math.cos(math.radians(start + (end - start) * step / steps)), 3)) for step in range(steps + 1)]


def symbol(ident: str, kind: str, opacity: float = 1) -> dict:
    """A server-list symbol on its tile (section 9: a small dark tile with a
    cut upper-leading corner, part of the symbol), SYMBOL dp square: the
    favorite's gold star, the password's padlock, the dedicated server's case
    with its green power light, the repeater's television with its antennas
    and color bars."""
    s = SYMBOL
    paths = [path("tile", [(3, 0), (s, 0), (s, s), (0, s), (0, 3)], fill=solid([0, 0, 0, 0.5]))]
    if kind == "fav":
        points = []
        for index in range(10):
            radius = 6.2 if index % 2 == 0 else 2.5
            angle = math.pi / 2 + index * math.pi / 5
            points.append((round(8 + radius * math.cos(angle), 3), round(8.6 - radius * math.sin(angle), 3)))
        paths.append(path("star", points, fill=solid(rgb(VALUE)), stroke=stroke(solid(rgb("#C8501E", 0.8)), 0.8)))
    elif kind == "lock":
        paths += [path("shackle", [(5.5, 8), (5.5, 5), (6.75, 3.25), (9.25, 3.25), (10.5, 5), (10.5, 8)], closed=False,
                       stroke=stroke(solid(rgb(CASE_GRAY)), 1.5)),
                  path("body", [(3.5, 7.5), (12.5, 7.5), (12.5, 13.5), (3.5, 13.5)], fill=solid(rgb(CASE_GRAY))),
                  path("keyhole", [(7.4, 9.3), (8.6, 9.3), (8.6, 11.9), (7.4, 11.9)], fill=solid([0, 0, 0, 0.85]))]
    elif kind == "ded":
        paths += [path("case", [(4, 1.75), (12, 1.75), (12, 14.25), (4, 14.25)], fill=solid(rgb(CASE_GRAY))),
                  path("drive", [(5.25, 3), (10.75, 3), (10.75, 4.25), (5.25, 4.25)], fill=solid([0, 0, 0, 0.9])),
                  path("button", [(9.5, 5.25), (10.5, 5.25), (10.5, 6.25), (9.5, 6.25)], fill=solid([0, 0, 0, 0.9])),
                  path("panel", [(5.25, 7.5), (10.75, 7.5), (10.75, 13), (5.25, 13)], fill=solid(rgb(CASE_PANEL))),
                  path("power-ring", circle_points(8, 10.6, 1.75, 35, 325, 10), closed=False, stroke=stroke(solid(rgb(POWER_GREEN)), 0.9)),
                  path("power-bar", [(8, 8.4), (8, 10.4)], closed=False, stroke=stroke(solid(rgb(POWER_GREEN)), 0.9))]
    else:
        paths += [path("antennas", [(5, 1.5), (8, 5), (11, 1.5)], closed=False, stroke=stroke(solid(rgb(TV_GRAY)), 0.9)),
                  path("set", [(2, 5), (14, 5), (14, 14), (2, 14)], fill=solid(rgb(TV_GRAY))),
                  path("screen", [(3.25, 6.25), (12.75, 6.25), (12.75, 12.75), (3.25, 12.75)], fill=solid(rgb(TV_SCREEN)))]
        for index, colour_value in enumerate(TV_BARS):
            x0 = round(3.75 + index * 2.25, 3)
            paths.append(path(f"bar{index}", [(x0, 6.75), (x0 + 2.25, 6.75), (x0 + 2.25, 11), (x0, 11)], fill=solid(rgb(colour_value))))
    return vector(ident, {"position": keyword("absolute"), "display": keyword("none"), "width": length(s), "height": length(s),
                          "opacity": number(opacity), "pointer-events": keyword("none")}, paths)


def quality_bars(doc: Document, ident: str, key: str, left: float) -> tuple:
    """A row's network quality (join.row<i>.quality, 0-4) as four bars from
    `left` (u), and where the ping's text starts after them."""
    doc.state[f"{key}.quality"] = {"type": "number", "initial": 0}
    quality = {"state": f"{key}.quality"}
    lit = [rgb(color, 0.9) for color in QUALITY_COLORS]
    bars = []
    for bar in range(1, QUALITY_BARS + 1):
        height = BAR_LOW + (bar - 1) * BAR_STEP
        x = left + 1.0 + (bar - 1) * (BAR_W + BAR_GAP)
        node = f"{ident}-bar-{bar}"

        def component(channel: int) -> dict:
            worst = {"op": "select", "args": [{"op": ">=", "args": [quality, 3]}, lit[2][channel],
                                              {"op": "select", "args": [{"op": "==", "args": [quality, 2]}, lit[1][channel], lit[0][channel]]}]}
            return {"op": "select", "args": [{"op": ">=", "args": [quality, bar]}, worst, BAR_UNLIT[channel]]}
        bars.append(group(node, {**absolute(left=round(U * x, 3), top=round(U * (BAR_BOTTOM - height), 3), width=round(U * BAR_W, 3),
                                            height=round(U * height, 3)), "background-color": colour(BAR_UNLIT),
                                 "pointer-events": keyword("none")}))
        doc.bind(f"{node}.background-color", node, "background-color", [component(channel) for channel in range(4)])
    text_left = left + 1.0 + QUALITY_BARS * (BAR_W + BAR_GAP) + 2.0
    return bars, text_left


def server_row(doc: Document, index: int, row_w: float) -> dict:
    """One of the list's rows: a server's symbols (favorite, password,
    dedicated server, repeater) and its name, ping, players, game type and map
    at the stock tab stops. Its first Accept chooses the server; Accept on the
    chosen one moves on to JOIN SERVER."""
    ident, key = f"server-{index}", f"join.row{index}"
    width, height = U * row_w, U * ROW_H
    tint = [1, 1, 1, 0.8]
    cells, texts = [], []
    for column, _sort, key_text, left, column_w, _verb in COLUMNS:
        node = f"{ident}-{column}"
        if key_text is None:
            doc.state[f"{key}.{column}"] = {"type": "boolean", "initial": False}
            icon = symbol(node, column)
            icon["properties"].update({"left": length(round(U * left + (U * column_w - SYMBOL) / 2, 3)),
                                       "top": length(round((height - SYMBOL) / 2, 3))})
            doc.bind(f"{node}.display", node, "display", {"op": "select", "args": [{"state": f"{key}.{column}"}, "block", "none"]})
            cells.append(icon)
            continue
        doc.state[f"{key}.{column}"] = {"type": "string", "initial": ""}
        if column == "ping":
            bars, text_left = quality_bars(doc, ident, key, left)
            cells += bars
            column_w -= text_left - left
            left = text_left
        # A server's name or map longer than its column ends in an ellipsis
        # rather than mid-letter.
        cells.append(label(node, PLACEHOLDER, {**absolute(left=U * left, top=0, width=U * column_w - 4, height=height),
                                                **typeface("lowpixel", CELL_TEXT, height, tint), "white-space": keyword("nowrap"),
                                                "overflow": keyword("hidden"), "text-overflow": keyword("ellipsis"),
                                                "pointer-events": keyword("none")}))
        doc.bind(f"{node}.text", node, "text", {"state": f"{key}.{column}"})
        texts.append(node)
    chosen = {"op": "+", "args": [{"state": "join.first"}, index]}
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [chosen, {"state": "join.count"}]}, "block", "none"]})
    bind_selected(doc, ident, {"op": "==", "args": [{"state": "join.selected"}, chosen]})
    event = f"serverRow{index}"
    doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": "join.selected"}, chosen]},
                          "then": [{"op": "focus", "control": "join_server"}],
                          "else": [{"op": "setState", "values": {"join.pick": index}}, {"op": "action", "action": "joinPick"}]}]
    ids = row_states(doc, ident, tuple(texts), tuple(tint for _ in texts))
    # The row's opacity rests at 1; row.reveal fades it in (rowReveal).
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                         "flex-shrink": number(0), "opacity": number(1)}, [*row_plates(ident, width, height, selectable=True), *cells],
                 control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids, "detail": index})


# The server details card (the stock p_mp_browse_tooltip as section 7's hover
# card): its frame, the address, PLAYERS and SERVER SETTINGS with their lists.
DETAIL = (270.0, 216.0)                    # u: the stock tooltip, and the name's two lines above it
DETAIL_NAME = 30.0                         # u: the name's two lines, which move the stock layout down
DETAIL_PLAYERS = 11                        # the stock list's 139 u at 12 u items
DETAIL_SETTINGS = 7                        # the stock settings (GetServerDetails)
DETAIL_HEADING = 11.0                      # dp: Lowpixel at the stock .16 scale
DETAIL_TEXT = 13.0                         # dp: Profont (section 7's hover card)
VIEW_GLYPH = 18.0                          # dp: the View button's glyph on the card


def detail_card(doc: Document) -> dict:
    """The server under the pointer, or the focused row's: the runtime writes
    the row (join.detail) and places the card beside it; the session fills it
    (join.detail.*, the stock tooltip's address, players and settings) and
    names the row it describes (join.detail.row), so the card shows only
    once its content matches the row. It holds no controls and never takes
    the pointer."""
    doc.state["join.detail"] = {"type": "number", "initial": -1}
    doc.state["join.detail.row"] = {"type": "number", "initial": -1}
    doc.state["join.detail.address"] = {"type": "string", "initial": ""}
    doc.state["join.detail.name"] = {"type": "string", "initial": ""}
    # The View button shows or hides the card (section 13.5).
    doc.state["join.detail.hidden"] = {"type": "boolean", "initial": False}
    doc.events["onViewButton"] = [{"op": "setState", "values": {
        "join.detail.hidden": {"op": "!", "args": [{"state": "join.detail.hidden"}]}}}]
    doc.detail = {"state": "join.detail", "card": "join-detail", "none": -1}
    width, height = U * DETAIL[0], U * DETAIL[1]
    cut, bevel = U * 8, U * 4
    frame = vector("join-detail-frame", {**FULL, "pointer-events": keyword("none")}, [
        path("card", [(0, 0), (width - bevel, 0), (width, bevel), (width, height), (cut, height), (0, height - cut)],
             fill=solid([0, 0, 0, 0.9]), stroke=stroke(solid(rgb(OLIVE, 0.85)), 1)),
        path("rule", [(U * 7, U * (23 + DETAIL_NAME)), (U * 252, U * (23 + DETAIL_NAME))], closed=False,
             stroke=stroke(solid(rgb(OLIVE, 0.32)), 2)),
        path("divider", [(U * 89, U * (29 + DETAIL_NAME)), (U * 89, U * (179 + DETAIL_NAME))], closed=False,
             stroke=stroke(solid(rgb(OLIVE, 0.32)), 2)),
    ])
    # The server's name in full, which its row cuts with an ellipsis: up to
    # two lines over the stock address (section 13.13's detail area shows what
    # a row truncates).
    name = label("join-detail-name", PLACEHOLDER, {**absolute(left=U * 6, top=U * 5, width=U * 244, height=U * 28),
                                                   **typeface("lowpixel", 14, U * 14, [1, 1, 1, 1]), "white-space": keyword("normal"),
                                                   "overflow": keyword("hidden")})
    doc.bind("join-detail-name.text", "join-detail-name", "text", {"state": "join.detail.name"})
    address = label("join-detail-address", PLACEHOLDER, {**absolute(left=U * 6, top=U * (5 + DETAIL_NAME), width=U * 244, height=U * 16),
                                                          **typeface("lowpixel", 14, U * 16, [1, 1, 1, 0.7]), "white-space": keyword("nowrap"),
                                                          "overflow": keyword("hidden")})
    doc.bind("join-detail-address.text", "join-detail-address", "text", {"state": "join.detail.address"})
    headings = [label("join-detail-players", "#str_200038", {**absolute(left=U * 7, top=U * (27 + DETAIL_NAME), width=U * 80, height=U * 12),
                                                             **typeface("lowpixel", DETAIL_HEADING, U * 12, [1, 1, 1, 0.4]),
                                                             "white-space": keyword("nowrap"), "overflow": keyword("hidden")}),
                label("join-detail-settings", "#str_200928", {**absolute(left=U * 92, top=U * (27 + DETAIL_NAME), width=U * 160,
                                                                       height=U * 12),
                                                              **typeface("lowpixel", DETAIL_HEADING, U * 12, [1, 1, 1, 0.4]),
                                                              "white-space": keyword("nowrap"), "overflow": keyword("hidden")})]
    lines = []
    for prefix, count, left, line_w in (("player", DETAIL_PLAYERS, 7, 80), ("setting", DETAIL_SETTINGS, 92, 172)):
        for index in range(count):
            ident, key = f"join-detail-{prefix}{index}", f"join.detail.{prefix}{index}"
            doc.state[key] = {"type": "string", "initial": ""}
            lines.append(label(ident, PLACEHOLDER, {**absolute(left=U * left, top=U * (40 + DETAIL_NAME + 12 * index), width=U * line_w,
                                                                height=U * 12),
                                                    **typeface("profont", DETAIL_TEXT, U * 12, [1, 1, 1, 0.8]),
                                                    "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
            doc.bind(f"{ident}.text", ident, "text", {"state": key})
    # The View button that hides the card, at its top trailing corner beside
    # the address: a controller's only (no key presses it, so the keyboard
    # shows none).
    view = group("join-detail-view", {**absolute(left=width - U * 6 - VIEW_GLYPH, top=U * 4, width=VIEW_GLYPH, height=VIEW_GLYPH),
                                      "pointer-events": keyword("none")},
                 device_glyphs(doc, "join-detail-view", "view", [1, 1, 1, 0.8], VIEW_GLYPH))
    card = group("join-detail", {**absolute(left=0, top=0, width=width, height=height), "display": keyword("none"),
                                 "pointer-events": keyword("none")}, [frame, name, address, view, *headings, *lines])
    doc.bind("join-detail.display", "join-detail", "display", {"op": "select", "args": [
        {"op": "&&", "args": [{"op": "!", "args": [{"state": "join.detail.hidden"}]},
                              {"op": "&&", "args": [{"op": ">=", "args": [{"state": "join.detail"}, 0]},
                                                    {"op": "==", "args": [{"state": "join.detail"}, {"state": "join.detail.row"}]}]}]},
        "block", "none"]})
    return card


def heading(doc: Document, column: str, sort: int, key: str | None, left: float, width: float, verb: str) -> dict:
    """A column's heading in the header band: its label (an icon column's
    icon), a button sorting by the column, and the sort's chevron while the
    list is sorted by it, pointing up while it runs ascending."""
    ident = f"heading-{column}"
    doc.session(verb, verb)
    height = U * 16
    tint = [1, 1, 1, 0.6]
    if key is not None:
        face = label(f"{ident}-label", key, {**absolute(left=0, top=0, width=U * width - CHEVRON_ROOM, height=height),
                                              **typeface("lowpixel", HEADING_TEXT, height, tint), "white-space": keyword("nowrap"),
                                              "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    else:
        face = symbol(f"{ident}-label", column, 0.6)
        face["properties"].update({"display": keyword("block"), "left": length(round((U * width - SYMBOL) / 2, 3)),
                                   "top": length(round((height - SYMBOL) / 2, 3))})
    sorted_by, ascending = {"op": "==", "args": [{"state": "join.sort"}, sort]}, {"state": "join.ascending"}
    # The chevron follows its heading: past the longest translation of its
    # label (check_join_fits keeps that inside the column), or past the icon,
    # so it never reads as the next column's.
    after = (max(text_width("lowpixel", text, HEADING_TEXT) for text in table_text(key).values()) if key is not None
             else (U * width + SYMBOL) / 2) + 3
    chevrons = []
    for name, points, top, shown in (("up", [(0, 5), (4, 0), (8, 5)], height / 2 - 3, ascending),
                                     ("down", [(0, 0), (4, 5), (8, 0)], height / 2 - 2, {"op": "!", "args": [ascending]})):
        node = f"{ident}-{name}"
        chevrons.append(vector(node, {**absolute(left=round(min(after, U * width - CHEVRON_ROOM + 1), 3), top=top, width=8, height=5),
                                      "display": keyword("none"), "pointer-events": keyword("none")},
                               [path("chevron", points, closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5))]))
        doc.bind(f"{node}.display", node, "display", {"op": "select", "args": [{"op": "&&", "args": [sorted_by, shown]}, "block", "none"]})
    rule = vector(f"{ident}-focus", {**FULL, "opacity": number(0), "pointer-events": keyword("none")},
                  [path("rule", [(0, {"fraction": 1, "dp": -1}), ({"fraction": 1}, {"fraction": 1, "dp": -1})], closed=False,
                        stroke=stroke(solid(rgb(ORANGE)), 1.5))])
    ids = doc.states(ident, {
        "default": [(f"{ident}-focus", "opacity", number(0))], "hover": [(f"{ident}-focus", "opacity", number(0.6))],
        "focus": [(f"{ident}-focus", "opacity", number(1))], "pressed": [(f"{ident}-focus", "opacity", number(1))],
        "disabled": [(f"{ident}-focus", "opacity", number(0))],
    })
    return group(ident, absolute(left=U * (LIST[0] + left), top=U * HEADING_TOP, width=U * width, height=height),
                 [face, *chevrons, rule], control={"role": "button", "label": key or PLACEHOLDER, "action": verb, "states": ids})


def source_tag(doc: Document, ident: str, key: str, left: float, width: float, verb: str, lan: bool) -> dict:
    """A source's cut tag (section 7's filter tags, built like the header
    band): the list's source lit and resting at 0.60, the other at 0.28."""
    doc.session(verb, verb)
    height, cut = U * 16, U * 5
    shape = [(cut, 0), (U * width, 0), (U * width, height), (0, height), (0, cut)]
    chosen = {"state": "join.lan"} if lan else {"op": "!", "args": [{"state": "join.lan"}]}
    tag = vector(f"{ident}-tag", {**FULL, "opacity": number(0.28), "pointer-events": keyword("none")},
                 [path("tag", shape, fill=solid(rgb(OLIVE, 0.5)), stroke=stroke(solid(rgb(OLIVE)), 1))])
    lit = vector(f"{ident}-lit", {**FULL, "display": keyword("none"), "opacity": number(0.6), "pointer-events": keyword("none")},
                 [path("tag", shape, fill=solid(rgb(ORANGE, 0.35)), stroke=stroke(solid(rgb(ORANGE)), 1))])
    doc.bind(f"{ident}-lit.display", f"{ident}-lit", "display", {"op": "select", "args": [chosen, "block", "none"]})
    caption = label(f"{ident}-label", key, {**absolute(left=U * 6, top=0, width=U * width - U * 8, height=height),
                                             **typeface("marine", TAG_TEXT, height, [1, 1, 1, 0.85]), "white-space": keyword("nowrap"),
                                             "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    ids = doc.states(ident, {
        "default": [(f"{ident}-tag", "opacity", number(0.28))], "hover": [(f"{ident}-tag", "opacity", number(0.7))],
        "focus": [(f"{ident}-tag", "opacity", number(1))], "pressed": [(f"{ident}-tag", "opacity", number(1))],
        "disabled": [(f"{ident}-tag", "opacity", number(0.2))],
    })
    return group(ident, absolute(left=U * left, top=U * SOURCE[1], width=U * width, height=height), [tag, lit, caption],
                 control={"role": "button", "label": key, "action": verb, "states": ids})


def source_tags() -> list:
    """The source tags' layout: (ident, key, left u, width u, verb, lan)."""
    right, layout = SOURCE_END, []
    for ident, key, verb, lan in reversed(TAG_KEYS):
        width = float(math.ceil(max(text_width("marine", text, TAG_TEXT) for text in table_text(key).values()) / U + 8))
        right -= width
        layout.append((ident, key, right, width, verb, lan))
        right -= TAG_GAP
    return list(reversed(layout))


def spinner_square(doc: Document, ident: str, left: float, top: float, verb: str, forward: bool, key: str) -> dict:
    """A filter spinner's square (section 7's filter spinner, the stock
    arrow6 on a 22x16 u tile) at `left`,`top` u, asking the session for the
    previous or next value."""
    doc.session(verb, verb)
    width, height = U * 22, U * 16
    points = [(width / 2 + 3, height / 2 - 4.5), (width / 2 - 3, height / 2), (width / 2 + 3, height / 2 + 4.5)]
    if forward:
        points = [(width - x, y) for x, y in points]
    square = vector(f"{ident}-square", {**FULL, "opacity": number(0.6), "pointer-events": keyword("none")},
                    [path("square", [(0, 0), (width, 0), (width, height), (0, height)], fill=solid([0, 0, 0, 0.7]),
                          stroke=stroke(solid(rgb(OLIVE)), 1))])
    arrow = vector(f"{ident}-arrow", {**FULL, "pointer-events": keyword("none")}, [path("arrow", points, fill=solid(rgb(VALUE)))])
    ids = doc.states(ident, {
        "default": [(f"{ident}-square", "opacity", number(0.6))], "hover": [(f"{ident}-square", "opacity", number(0.85))],
        "focus": [(f"{ident}-square", "opacity", number(1))], "pressed": [(f"{ident}-square", "opacity", number(1))],
        "disabled": [(f"{ident}-square", "opacity", number(0.3))],
    })
    return group(ident, absolute(left=U * left, top=U * top, width=width, height=height), [square, arrow],
                 control={"role": "button", "label": key, "action": verb, "states": ids})


def mod_filter(doc: Document) -> list:
    """MOD (the stock page's mod filter): the label, the filter's value (the
    session's join.mod, "All" when it shows every mod) and its spinner
    squares, each asking the session for the previous or next mod the list
    holds."""
    doc.state["join.mod"] = {"type": "string", "initial": ""}
    height = U * 16
    parts = [label("join-mod", "#str_223005", {**absolute(left=U * MOD[0], top=U * MOD[1], width=U * MOD[2], height=height),
                                                **typeface("marine", TAG_TEXT, height, [1, 1, 1, 0.5]), "white-space": keyword("nowrap"),
                                                "overflow": keyword("hidden"), "text-align": keyword("right")}),
             label("join-mod-value", PLACEHOLDER, {**absolute(left=U * MOD_VALUE[0], top=U * MOD[1], width=U * MOD_VALUE[1], height=height),
                                                   **typeface("marine", TAG_TEXT, height, rgb(VALUE, 0.8)), "white-space": keyword("nowrap"),
                                                   "overflow": keyword("hidden"), "pointer-events": keyword("none")})]
    doc.bind("join-mod-value.text", "join-mod-value", "text", {"state": "join.mod"})
    for left, verb, name in MOD_SPIN:
        parts.append(spinner_square(doc, f"join_mod_{name}", left, MOD[1], verb, name == "next", "#str_223005"))
    return parts


def filter_tag(doc: Document, ident: str, key: str, left: float, width: float, verb: str, state: str) -> dict:
    """A filter tag (section 7: a small cut tag built like the header band,
    with a close cross once set; set tags rest at 0.60 and unset ones at
    0.28, and Accept toggles): its label and, while the session's `state`
    says the filter is set, the lit plate and the cross."""
    doc.session(verb, verb)
    doc.state[state] = {"type": "boolean", "initial": False}
    height, cut = U * 16, U * 5
    shape = [(cut, 0), (U * width, 0), (U * width, height), (0, height), (0, cut)]
    tag = vector(f"{ident}-tag", {**FULL, "opacity": number(0.28), "pointer-events": keyword("none")},
                 [path("tag", shape, fill=solid(rgb(OLIVE, 0.5)), stroke=stroke(solid(rgb(OLIVE)), 1))])
    lit = vector(f"{ident}-lit", {**FULL, "display": keyword("none"), "opacity": number(0.6), "pointer-events": keyword("none")},
                 [path("tag", shape, fill=solid(rgb(ORANGE, 0.35)), stroke=stroke(solid(rgb(ORANGE)), 1))])
    cross = vector(f"{ident}-cross", {**absolute(left=U * width - 17, top=height / 2 - 5, width=10, height=10), "display": keyword("none"),
                                      "pointer-events": keyword("none")},
                   [path("a", [(1, 1), (9, 9)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5)),
                    path("b", [(9, 1), (1, 9)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5))])
    for node in (f"{ident}-lit", f"{ident}-cross"):
        doc.bind(f"{node}.display", node, "display", {"op": "select", "args": [{"state": state}, "block", "none"]})
    caption = label(f"{ident}-label", key, {**absolute(left=U * 6, top=0, width=U * (width - 8 - FILTER_CROSS), height=height),
                                             **typeface("marine", TAG_TEXT, height, [1, 1, 1, 0.85]), "white-space": keyword("nowrap"),
                                             "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    ids = doc.states(ident, {
        "default": [(f"{ident}-tag", "opacity", number(0.28))], "hover": [(f"{ident}-tag", "opacity", number(0.7))],
        "focus": [(f"{ident}-tag", "opacity", number(1))], "pressed": [(f"{ident}-tag", "opacity", number(1))],
        "disabled": [(f"{ident}-tag", "opacity", number(0.2))],
    })
    return group(ident, absolute(left=U * left, top=U * FILTER_TOP, width=U * width, height=height), [tag, lit, cross, caption],
                 control={"role": "button", "label": key, "action": verb, "states": ids})


def filter_tags() -> list:
    """The filter tags' layout: (ident, key, left u, width u, verb, state),
    ending where the source tags do."""
    right, layout = SOURCE_END, []
    for ident, key, verb, state in reversed(FILTER_TAGS):
        width = float(math.ceil(max(text_width("marine", text, TAG_TEXT) for text in table_text(key).values()) / U + 8 + FILTER_CROSS))
        right -= width
        layout.append((ident, key, right, width, verb, state))
        right -= TAG_GAP
    return list(reversed(layout))


def game_type_value_width() -> float:
    """The game type spinner's value (u): from after its previous square to
    its next square, which stands 8 u before the first tag."""
    return filter_tags()[0][2] - 8 - 22 - 3 - (LIST[0] + 22 + 3)


def filter_row(doc: Document) -> list:
    """The filter row under the list: the game type's spinner (the session's
    join.gameType, ALL GAME TYPES while it is 0, else its name
    join.gameTypeName) and the filter tags."""
    doc.state.update({"join.gameType": {"type": "number", "initial": 0}, "join.gameTypeName": {"type": "string", "initial": ""}})
    height = U * 16
    value_left = LIST[0] + 22 + 3
    value_w = game_type_value_width()
    value = label("join-gametype-value", FILTER_ALL_TYPES, {**absolute(left=U * value_left, top=U * FILTER_TOP, width=U * value_w,
                                                                       height=height), **typeface("marine", TAG_TEXT, height, rgb(VALUE, 0.8)),
                                                            "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                                                            "text-overflow": keyword("ellipsis"), "pointer-events": keyword("none")})
    doc.bind("join-gametype-value.text", "join-gametype-value", "text", {"op": "select", "args": [
        {"op": ">", "args": [{"state": "join.gameType"}, 0]}, {"state": "join.gameTypeName"}, FILTER_ALL_TYPES]})
    parts = [spinner_square(doc, "join_gametype_previous", LIST[0], FILTER_TOP, GAMETYPE_SPIN[0], False, "#str_200039"), value,
             spinner_square(doc, "join_gametype_next", value_left + value_w + 3, FILTER_TOP, GAMETYPE_SPIN[1], True, "#str_200039")]
    parts += [filter_tag(doc, f"join_filter_{ident}", key, left, width, verb, state) for ident, key, left, width, verb, state in filter_tags()]
    return parts


def list_action_width() -> float:
    """The list's action plates' width (u): the longest label of any of them
    in any language and the plate's 30 dp, at least the stock 208 u."""
    longest = max(text_width("marine", text, 20) for _i, key, *_rest in LIST_ACTIONS for text in table_text(key).values())
    return float(max(208.0, math.ceil((longest + 30) / U)))


def entry_dialog_size(keys: tuple) -> tuple:
    """An entry dialog's width and its two plates' (dp): each plate takes the
    longest of `keys` in any language and 50 dp, at least the stock 180 dp,
    and the dialog both plates with its 30 and 33 dp margins and a 12 dp gap,
    at least the stock 480 dp."""
    longest = max(text_width("marine", text, 20) for key in keys for text in table_text(key).values())
    action_w = round(max(180.0, longest + 50), 1)
    return round(max(480.0, 2 * action_w + 30 + 33 + 12), 1), action_w


def join_ip_modal(doc: Document) -> dict:
    """JOIN IP ADDRESS (the stock pop_p_joinIp as section 6's entry dialog):
    the address field, which Enter commits into join.address and connects
    with; the session's reason when it cannot (join.addressError); "your ip
    address:" and this machine's (join.clientip); CONNECT, while there is an
    address, and CANCEL. The session closes the dialog once it connects."""
    doc.state.update({"join.address": {"type": "string", "initial": ""}, "join.addressError": {"type": "string", "initial": ""},
                      "join.clientip": {"type": "string", "initial": ""}})
    doc.session("joinConnect", "joinConnect")
    width_dp, action_w = entry_dialog_size(("#str_200171", "#str_200168"))
    row_w = width_dp - U * 50
    doc.events["joinAddressEntered"] = [{"op": "action", "action": "joinConnect"}]
    field = text_row(doc, "join-address", "#str_200025", row_w, 29, "join.address", "joinAddressEntered", ADDRESS_BYTES, U * ENTRY_ROW,
                     caption=False)
    column = group("join-ip-rows", {**absolute(left=U * 25, top=U * 25, width=row_w, height=U * ENTRY_ROW), "display": keyword("flex"),
                                    "flex-direction": keyword("column")}, [field])
    # Above 100 % text the field grows in its place, the lines under it move
    # down and grow, and the dialog grows with them (section 2's reflow).
    from build_retained_screens import LARGER, TEXT, text_source, text_sum, text_times
    text_source(doc)
    stack_row(doc, field, row_w, 29, U * ENTRY_ROW)
    doc.bind("text.join-ip-rows.height", "join-ip-rows", "height", text_times(U * ENTRY_ROW))
    line = U * 16
    grown = lambda height: {"op": "*", "args": [height, {"op": "-", "args": [TEXT, 1]}]}
    error = label("join-address-error", PLACEHOLDER, {**absolute(left=U * 25 + 21, top=U * 51, width=row_w - 21, height=U * 16),
                                                      **typeface("lowpixel", 13, U * 16, rgb(ERROR)), "white-space": keyword("nowrap"),
                                                      "overflow": keyword("hidden"), "display": keyword("none")})
    doc.bind("join-address-error.text", "join-address-error", "text", {"state": "join.addressError"})
    doc.bind("join-address-error.display", "join-address-error", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "join.addressError"}, ""]}, "block", "none"]})
    yours = label("join-yourip", "#str_200180", {**absolute(left=U * 25 + 21, top=U * 70, width=U * 130, height=U * 16),
                                                  **typeface("marine", 15, U * 16, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"),
                                                  "overflow": keyword("hidden")})
    address = label("join-yourip-value", PLACEHOLDER, {**absolute(left=U * 25 + 21 + U * 134, top=U * 70, width=row_w - 21 - U * 134,
                                                                 height=U * 16), **typeface("marine", 15, U * 16, rgb(VALUE, 0.8)),
                                                       "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("join-yourip-value.text", "join-yourip-value", "text", {"state": "join.clientip"})
    for node, below in ((error, grown(U * ENTRY_ROW)), (yours, text_sum(grown(U * ENTRY_ROW), grown(line))),
                        (address, text_sum(grown(U * ENTRY_ROW), grown(line)))):
        top = node["properties"]["top"]["value"]
        doc.bind(f"text.{node['id']}.top", node["id"], "top", {"op": "select", "args": [LARGER, text_sum(top, below), top]})
        doc.bind(f"text.{node['id']}.height", node["id"], "height", text_times(line))
    # "your ip address:" keeps its size, centred in its grown line.
    doc.bind("text.join-yourip.font-size", "join-yourip", "font-size", {"op": "/", "args": [yours["properties"]["font-size"]["value"], TEXT]})
    doc.events["joinIpConnect"] = [{"op": "if", "condition": {"op": "!=", "args": [{"state": "join.address"}, ""]},
                                    "then": [{"op": "action", "action": "joinConnect"}]}]
    # Each opening starts without the last refusal's reason.
    doc.events["joinIpOpen"] = [{"op": "setState", "values": {"join.addressError": ""}}, {"op": "call", "event": "joinIpModalShow"}]
    modal = confirmation(doc, "joinIpModal", "#str_200025", PLACEHOLDER, None, yes_event="joinIpConnect", wide_title=True,
                         contents=[column, error, yours, address], yes_key="#str_200171", no_key="#str_200168", initial="join-address",
                         width_dp=width_dp, action_w=action_w, contents_grow=text_sum(grown(U * ENTRY_ROW), grown(line), grown(line)))
    doc.bind("joinIpModal_yes.enabled", "joinIpModal_yes", "enabled", {"op": "!=", "args": [{"state": "join.address"}, ""]})
    return modal


def find_modal(doc: Document) -> dict:
    """FIND PLAYER/CLAN (the stock pop_p_find_playerClan as section 6's entry
    dialog): SEARCH FOR: and the name field, which Enter commits into
    join.find and searches with; SEARCH and CANCEL. A clan tag is part of a
    player's name, so the stock PLAYER and CLAN choice has no use."""
    doc.state["join.find"] = {"type": "string", "initial": ""}
    doc.session("joinSearch", "joinSearch")
    width_dp, action_w = entry_dialog_size(("#str_200257", "#str_200168"))
    row_w = width_dp - U * 50
    search = [{"op": "action", "action": "joinSearch"}, {"op": "call", "event": "findModalHide"}]
    doc.events["joinFindEntered"] = search
    field = text_row(doc, "join-find", "#str_200254", row_w, round(row_w * 0.42, 1), "join.find", "joinFindEntered", FIND_BYTES,
                     U * ENTRY_ROW, allow_empty=True)
    column = group("join-find-rows", {**absolute(left=U * 25, top=U * 25, width=row_w, height=U * ENTRY_ROW), "display": keyword("flex"),
                                      "flex-direction": keyword("column")}, [field])
    # Above 100 % text the field stands under SEARCH FOR: and the dialog grows
    # with its row (section 2's reflow).
    from build_retained_screens import text_sum
    stack_row(doc, field, row_w, round(row_w * 0.42, 1), U * ENTRY_ROW)
    height = next(binding["value"] for binding in doc.bindings if binding["id"] == "text.join-find.height")
    doc.bind("text.join-find-rows.height", "join-find-rows", "height", height)
    doc.events["findSearch"] = search
    doc.events["findOpen"] = [{"op": "call", "event": "findModalShow"}]
    return confirmation(doc, "findModal", "#str_200182", PLACEHOLDER, None, yes_event="findSearch", wide_title=True, contents=[column],
                        yes_key="#str_200257", no_key="#str_200168", initial="join-find", width_dp=width_dp, action_w=action_w,
                        contents_grow=text_sum(height, -U * ENTRY_ROW))


def search_tag(doc: Document) -> dict:
    """A player search's filter tag (section 7: a small cut tag with a close
    cross, set at 0.60) after the status line while the list is searched:
    the name and the cross; Accept ends the search."""
    doc.state["join.search"] = {"type": "string", "initial": ""}
    doc.session("joinSearchClear", "joinSearchClear")
    left = SEARCH_TAG_LEFT
    width = U * (CLEAR_SORT[0] - clear_sort_width() - 6 - left)
    height, cut = U * 16, U * 5
    shape = [(cut, 0), (width, 0), (width, height), (0, height), (0, cut)]
    tag = vector("join-search-tag", {**FULL, "opacity": number(0.6), "pointer-events": keyword("none")},
                 [path("tag", shape, fill=solid(rgb(ORANGE, 0.35)), stroke=stroke(solid(rgb(ORANGE)), 1))])
    name = label("join-search-name", PLACEHOLDER, {**absolute(left=U * 6, top=0, width=width - U * 6 - 22, height=height),
                                                   **typeface("lowpixel", 14, height, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"),
                                                   "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    doc.bind("join-search-name.text", "join-search-name", "text", {"state": "join.search"})
    cross = vector("join-search-cross", {**absolute(left=width - 17, top=height / 2 - 5, width=10, height=10), "pointer-events": keyword("none")},
                   [path("a", [(1, 1), (9, 9)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5)),
                    path("b", [(9, 1), (1, 9)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5))])
    ids = doc.states("join_search", {
        "default": [("join-search-tag", "opacity", number(0.6))], "hover": [("join-search-tag", "opacity", number(0.85))],
        "focus": [("join-search-tag", "opacity", number(1))], "pressed": [("join-search-tag", "opacity", number(1))],
        "disabled": [("join-search-tag", "opacity", number(0.3))],
    })
    tag_group = group("join_search", {**absolute(left=U * left, top=U * (STATUS[1] - 1), width=width, height=height), "display": keyword("none")},
                      [tag, name, cross], control={"role": "button", "label": "#str_200182", "action": "joinSearchClear", "states": ids})
    doc.bind("join_search.display", "join_search", "display",
             {"op": "select", "args": [{"op": "!=", "args": [{"state": "join.search"}, ""]}, "block", "none"]})
    return tag_group


def load_progress(doc: Document) -> list:
    """The scan's progress under the list (the stock browser's load bar): the
    track across the list's width, the fill to the scan's fraction
    (join.progress) and its notice over them (join.notice)."""
    left, top, width, height = LOAD_BAR
    bar_w, tube_h = U * width, U * (height - 2)

    def tube(ident: str, alpha: float) -> dict:
        stops = [(at, rgb(PROGRESS, round(alpha * level, 3))) for at, level in LOAD_PROFILE]
        return path(ident, [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
                    fill=linear((0, 0), (0, {"fraction": 1}), stops))

    track = vector("join-load-track", absolute(left=U * left, top=U * (top + 1), width=bar_w, height=tube_h), [tube("track", 0.30)])
    fill = group("join-load-fill", {**absolute(left=U * left, top=U * (top + 1), width=0, height=tube_h), "overflow": keyword("hidden")}, [
        vector("join-load-fill-tube", absolute(left=0, top=0, width=bar_w, height=tube_h), [tube("fill", 0.50)])])
    doc.bind("join-load-fill.width", "join-load-fill", "width",
             {"op": "*", "args": [{"op": "clamp", "args": [{"state": "join.progress"}, 0, 1]}, round(bar_w, 4)]})
    # The page carries a transform, so this box clips through a clip mask, and
    # a box with no width has no mask geometry: at no progress the fill drew
    # whole over the track. It leaves the page until the scan has some.
    fill["properties"]["display"] = keyword("none")
    doc.bind("join-load-fill.display", "join-load-fill", "display",
             {"op": "select", "args": [{"op": ">", "args": [{"state": "join.progress"}, 0]}, "block", "none"]})
    notice = label("join-load-notice", PLACEHOLDER, {**absolute(left=U * LOAD_NOTICE[0], top=U * LOAD_NOTICE[1], width=U * LOAD_NOTICE[2],
                                                                height=U * 16), **typeface("lowpixel", 15, U * 16, [1, 1, 1, 0.8]),
                                                     "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("join-load-notice.text", "join-load-notice", "text", {"state": "join.notice"})
    return [track, fill, notice]


def favorite_width() -> float:
    """The favorite toggle's plate width (u): its longer label and the plate's
    30 dp, at least JOIN SERVER's width."""
    longest = max(text_width("marine", text, 20) for key in FAVORITE_KEYS for text in table_text(key).values())
    return float(max(JOIN[2], math.ceil((longest + 30) / U)))


def spectate_width() -> float:
    """SPECTATE's plate width (u), as the favorite toggle's: its label and the
    plate's 30 dp, at least JOIN SERVER's width."""
    longest = max(text_width("marine", text, 20) for text in table_text(SPECTATE_KEY).values())
    return float(max(JOIN[2], math.ceil((longest + 30) / U)))


def stick_glyph_on(doc: Document, plate: dict, width: float, side: str) -> None:
    """The stick press that works a plate (section 13.5), 8 dp past its
    trailing end and centred on its band, for a controller only: the
    keyboard has no key for it and shows none. The bar has no room for the
    prompt in any language, so the glyph sits on what the stick presses, as
    the Menu and View glyphs do."""
    ident = plate["id"]
    plate["children"].append(group(f"{ident}-stick", {**absolute(left=width + 8, top=(45 - STICK_GLYPH) / 2, width=STICK_GLYPH,
                                                                  height=STICK_GLYPH), "pointer-events": keyword("none")},
                                    device_glyphs(doc, f"{ident}-stick", f"{side}-stick", [1, 1, 1, 0.85], STICK_GLYPH)))


def clear_sort_width() -> float:
    """CLEAR SORTING's plate width (u): its label and the plate's 30 dp."""
    longest = max(text_width("marine", text, 20) for text in table_text("#str_200951").values())
    return float(max(CLEAR_SORT[2], math.ceil((longest + 30) / U)))


def check_join_fits() -> None:
    """Every shipped language's labels fit on one line: the headings in their
    columns, the source and its tags, the list's empty line and pagers, and
    the plates' labels."""
    one_line = [(key, "lowpixel", HEADING_TEXT, U * width - CHEVRON_ROOM) for _c, _s, key, _l, width, _v in COLUMNS if key is not None]
    tags = source_tags()
    one_line += [("#str_200032", "marine", TAG_TEXT, U * (tags[0][2] - TAG_GAP - SOURCE[0]))]
    one_line += [(key, "marine", TAG_TEXT, U * width - U * 8) for _i, key, _l, width, _v, _lan in tags]
    one_line += [("#str_231133", "lowpixel", 15, U * (LIST[2] - 16)), ("#str_231134", "lowpixel", 15, U * (LIST[2] - 16)),
                 ("#str_231131", "marine", 16, U * (LIST[2] - 16 - 18)),
                 ("#str_231132", "marine", 16, U * (LIST[2] - 16 - 18)), ("#str_200951", "marine", 20, U * clear_sort_width() - 30)]
    one_line += [(key, "marine", 20, U * list_action_width() - 30) for _i, key, *_rest in LIST_ACTIONS]
    one_line += [("#str_200180", "marine", 15, U * 130), ("#str_200254", "lowpixel", 15, (entry_dialog_size(("#str_200257", "#str_200168"))[0]
                  - U * 50) * 0.42 - 29)]
    for key in ("#str_200025", "#str_200182"):     # the dialogs' titles in the wide title slot
        width_dp = entry_dialog_size(("#str_200171", "#str_200168") if key == "#str_200025" else ("#str_200257", "#str_200168"))[0]
        one_line += [(key, "marine", 20, width_dp * (1 - 7 / 512) - 6 - (width_dp * 14 / 512 + 17))]
    one_line += [("#str_223005", "marine", TAG_TEXT, U * MOD[2])]
    one_line += [(key, "marine", TAG_TEXT, U * (width - 8 - FILTER_CROSS)) for _i, key, _l, width, _v, _s in filter_tags()]
    one_line += [(FILTER_ALL_TYPES, "marine", TAG_TEXT, U * game_type_value_width())]
    one_line += [("#str_200038", "lowpixel", DETAIL_HEADING, U * 80), ("#str_200928", "lowpixel", DETAIL_HEADING, U * 160)]
    one_line += [(key, "marine", 20, U * favorite_width() - 30) for key in FAVORITE_KEYS]
    check_fits(tuple(one_line), ())


def join_game_document() -> dict:
    """The Join Game sub-page (see the module's description)."""
    check_join_fits()
    doc = Document("openq4.joingame")
    doc.session("back", "joinBack")
    doc.session("joinPick", "joinPick")
    doc.session("joinServer", "joinServer")
    doc.session("joinClearSort", "joinClearSort")
    doc.session("serversNewer", "joinNewer")
    doc.session("serversOlder", "joinOlder")
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    # The controller (section 13.5): west refreshes the list and north sorts it
    # by the next column, while no dialog is open.
    doc.session("joinSortNext", "joinSortNext")
    quiet = {"op": "!", "args": [{"op": "||", "args": [{"state": "joinIpModal.visible"}, {"state": "findModal.visible"}]}]}
    doc.events["onSecondary"] = [{"op": "if", "condition": quiet, "then": [{"op": "action", "action": "joinRefresh"}]}]
    doc.events["onTertiary"] = [{"op": "if", "condition": quiet, "then": [{"op": "action", "action": "joinSortNext"}]}]
    # A focused row's server keeps the focus through a new order: the session
    # names the row that now shows it (idSessionLocal::PublishRetainedJoinGame).
    for row in range(JOIN_ROWS):
        doc.events[f"joinFocus{row}"] = [{"op": "focus", "control": f"server-{row}"}]
    doc.state.update({
        "join.count": {"type": "number", "initial": 0},
        "join.listed": {"type": "number", "initial": 0},
        "join.first": {"type": "number", "initial": 0},
        "join.selected": {"type": "number", "initial": -1},
        "join.pick": {"type": "number", "initial": -1},
        "join.sort": {"type": "number", "initial": 0},
        "join.ascending": {"type": "boolean", "initial": True},
        "join.lan": {"type": "boolean", "initial": False},
        "join.scanning": {"type": "boolean", "initial": False},
        "join.status": {"type": "string", "initial": ""},
        "join.progress": {"type": "number", "initial": 0},
        "join.notice": {"type": "string", "initial": ""},
    })
    heading_tint = {**typeface("marine", TAG_TEXT, U * 16, [1, 1, 1, 0.5]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    tags = source_tags()
    parts = [header_band("join-header", *HEADER),
             label("join-source", "#str_200032", {**absolute(left=U * SOURCE[0], top=U * SOURCE[1],
                                                             width=U * (tags[0][2] - TAG_GAP - SOURCE[0]), height=U * 16), **heading_tint})]
    parts += [source_tag(doc, f"join_{ident}", key, left, width, verb, lan) for ident, key, left, width, verb, lan in tags]
    parts += mod_filter(doc)
    parts += filter_row(doc)
    parts += [heading(doc, column, sort, key, left, width, verb) for column, sort, key, left, width, verb in COLUMNS]

    left, top, list_w, list_h = LIST
    row_w = list_w - 16
    newer = pager_row(doc, "servers-newer", "#str_231131", "serversNewer", {"op": ">", "args": [{"state": "join.first"}, 0]}, row_w=row_w)
    older = pager_row(doc, "servers-older", "#str_231132", "serversOlder",
                      {"op": "<", "args": [{"op": "+", "args": [{"state": "join.first"}, JOIN_ROWS]}, {"state": "join.count"}]},
                      row_w=row_w)
    servers = group("servers-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")},
                    [newer, *[server_row(doc, index, row_w) for index in range(JOIN_ROWS)], older])
    # Above 100 % text the servers' rows grow with it (section 2's reflow).
    grow_rows(doc, servers)
    parts.append(group("servers-list", {**absolute(left=U * left, top=U * top, width=U * row_w, height=U * list_h),
                                        "overflow": keyword("auto")}, [servers]))
    parts.append(objectives_scrollbar(doc, "servers_scroll", "servers-list", OLIVE, ORANGE, U * (left + list_w) - 16, U * top, U * list_h,
                                      line_step=U * ROW_H, key="#str_200187"))
    # While a scan has listed nothing yet the list shows its rows at rest,
    # without labels (section 13.13's loading rows; no shimmer, no spinner),
    # and the first servers replace them with row.reveal: each visible row
    # fades in over 120 ms, one 15 ms after another, capped at 90 ms in all
    # (a track runs from the property's present value, so each holds 0 from
    # its first millisecond).
    # The session asks for the reveal (joinRowsReveal) only when servers
    # replace the loading rows, not when the page opens on a list.
    visible = int(list_h // ROW_H)
    loading = group("join-loading", {**absolute(left=U * left, top=U * top, width=U * (list_w - 16), height=U * visible * ROW_H),
                                     "display": keyword("none"), "pointer-events": keyword("none")},
                    [group(f"join-loading-{row}", absolute(left=0, top=U * ROW_H * row, width=U * (list_w - 16), height=U * ROW_H),
                           row_plates(f"join-loading-{row}", U * (list_w - 16), U * ROW_H)[:1]) for row in range(visible)])
    doc.bind("join-loading.display", "join-loading", "display", {"op": "select", "args": [
        {"op": "&&", "args": [{"op": "==", "args": [{"state": "join.count"}, 0]}, {"state": "join.scanning"}]}, "block", "none"]})
    delays = [min(REVEAL_STAGGER_MS * row, REVEAL_STAGGER_CAP_MS) for row in range(visible)]
    doc.timelines.add("rowReveal", max(delays) + REVEAL_MS, [
        track(f"server-{row}", "opacity", [(0, number(0)), (1, number(0)), *([(delay, number(0))] if delay > 1 else []),
                                           (delay + REVEAL_MS, number(1))])
        for row, delay in enumerate(delays)])
    doc.events["joinRowsReveal"] = [{"op": "playTimeline", "timeline": "rowReveal"}]
    # An empty list is the empty state of section 13.13, unless the scan is
    # still running: a heading, the sentence that says how to fill it and
    # the action that does. When the scan holds servers that the filters or a
    # search all hide, they say none matches and CLEAR FILTERS clears them;
    # otherwise none was found and GET NEW LIST asks for them again.
    shown = {"op": "&&", "args": [{"op": "==", "args": [{"state": "join.count"}, 0]}, {"op": "!", "args": [{"state": "join.scanning"}]}]}
    filtered = {"op": ">", "args": [{"state": "join.listed"}, 0]}
    empty_heading = label("join-empty-heading", "#str_231537", {**absolute(left=U * (left + 4), top=U * (top + 8), width=U * (list_w - 16),
                                                                           height=U * 16),
                                                                 **typeface("marine", 20, U * 16, HEADING), "white-space": keyword("nowrap"),
                                                                 "overflow": keyword("hidden"), "display": keyword("none")})
    doc.bind("join-empty-heading.display", "join-empty-heading", "display", {"op": "select", "args": [shown, "block", "none"]})
    doc.bind("join-empty-heading.text", "join-empty-heading", "text", {"op": "select", "args": [filtered, "#str_231538", "#str_231537"]})
    empty = label("join-empty", "#str_231133", {**absolute(left=U * (left + 4), top=U * (top + 28), width=U * (list_w - 16), height=U * 16),
                                                 **typeface("lowpixel", 15, U * 16, [1, 1, 1, 0.6]), "white-space": keyword("nowrap"),
                                                 "overflow": keyword("hidden"), "display": keyword("none")})
    doc.bind("join-empty.display", "join-empty", "display", {"op": "select", "args": [shown, "block", "none"]})
    doc.bind("join-empty.text", "join-empty", "text", {"op": "select", "args": [filtered, "#str_231134", "#str_231133"]})
    doc.session("joinClearFilters", "joinClearFilters")
    fix_w = max(180.0, math.ceil(max(text_width("marine", text, 20) for key in ("#str_231422", "#str_200029")
                                     for text in table_text(key).values()) + 30))
    fixes = []
    for ident, key, verb, condition in (("join_emptyclear", "#str_231422", "joinClearFilters", filtered),
                                        ("join_emptynew", "#str_200029", "joinNewList", {"op": "!", "args": [filtered]})):
        plate = action_plate(doc, ident, key, U * (left + 4), U * (top + 48) - 5, action=verb, width=fix_w)
        plate["properties"]["display"] = keyword("none")
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"op": "&&", "args": [shown, condition]}, "block", "none"]})
        fixes.append(plate)
    clear_w = clear_sort_width()
    status = label("join-status", PLACEHOLDER, {**absolute(left=U * STATUS[0], top=U * STATUS[1], width=U * STATUS[2], height=U * 16),
                                                 **typeface("lowpixel", 14, U * 16, [1, 1, 1, 0.7]), "white-space": keyword("nowrap"),
                                                 "overflow": keyword("hidden")})
    doc.bind("join-status.text", "join-status", "text", {"state": "join.status"})
    parts += [loading, empty_heading, empty, *fixes, status, action_plate(doc, "join_clearsort", "#str_200951", U * (CLEAR_SORT[0] - clear_w), U * CLEAR_SORT[1] - 5,
                                          action="joinClearSort", width=U * clear_w)]
    parts += load_progress(doc)
    action_w = list_action_width()
    for ident, key, y, verb, event in LIST_ACTIONS:
        if verb is not None:
            doc.session(verb, verb)
        parts.append(action_plate(doc, ident, key, U * LIST_X, U * y - 5, action=verb, event=event, width=U * action_w))
    parts.append(search_tag(doc))
    # The chosen server's levelshot (the session's join.shot), in the stock
    # frame between the list's actions and JOIN SERVER.
    doc.state["join.shot"] = {"type": "string", "initial": ""}
    parts += picture_frame("join-shot", *SHOT)
    doc.bind("join-shot.image", "join-shot", "image", {"state": "join.shot"})
    join = primary_plate(doc, "join_server", "#str_200031", *JOIN, action="joinServer", label_box=(43.0, 6.0, 180.0, 25.0), text=23.7,
                         marker=(13.0, 9.5, 18.0))
    join["control"]["prompt"] = VERB_JOIN
    doc.bind("join_server.enabled", "join_server", "enabled", {"op": ">=", "args": [{"state": "join.selected"}, 0]})
    # The favorite toggle under it, ending where it ends: ADD TO FAVORITES, or
    # REMOVE FAVORITE while the chosen server is one (join.favorite).
    doc.state["join.favorite"] = {"type": "boolean", "initial": False}
    doc.session("joinFavorite", "joinFavorite")
    favorite_w = favorite_width()
    favorite = action_plate(doc, "join_favorite", FAVORITE_KEYS[0], U * (JOIN[0] + JOIN[2] - favorite_w), U * FAVORITE_TOP - 5,
                            action="joinFavorite", width=U * favorite_w)
    doc.bind("join_favorite-label.text", "join_favorite-label", "text",
             {"op": "select", "args": [{"state": "join.favorite"}, FAVORITE_KEYS[1], FAVORITE_KEYS[0]]})
    doc.bind("join_favorite.enabled", "join_favorite", "enabled", {"op": ">=", "args": [{"state": "join.selected"}, 0]})
    # SPECTATE between them, ending where they end (section 13.7's list
    # pattern: Join, Spectate and Favorite): it joins the chosen server
    # watching. A press of the left stick spectates and of the right one
    # toggles the favorite, while a server is chosen and no dialog is open.
    doc.session("joinSpectate", "joinSpectate")
    spectate_w = spectate_width()
    spectate = action_plate(doc, "join_spectate", SPECTATE_KEY, U * (JOIN[0] + JOIN[2] - spectate_w), U * (JOIN[1] + JOIN[3]),
                            action="joinSpectate", width=U * spectate_w)
    spectate["control"]["prompt"] = SPECTATE_KEY
    doc.bind("join_spectate.enabled", "join_spectate", "enabled", {"op": ">=", "args": [{"state": "join.selected"}, 0]})
    stick_glyph_on(doc, spectate, U * spectate_w, "left")
    stick_glyph_on(doc, favorite, U * favorite_w, "right")
    chosen = {"op": "&&", "args": [quiet, {"op": ">=", "args": [{"state": "join.selected"}, 0]}]}
    doc.events["onLeftStick"] = [{"op": "if", "condition": chosen, "then": [{"op": "action", "action": "joinSpectate"}]}]
    doc.events["onRightStick"] = [{"op": "if", "condition": chosen, "then": [{"op": "action", "action": "joinFavorite"}]}]
    back = page_prompts(doc, "join_back", action="back", verbs=(VERB_JOIN, SPECTATE_KEY))
    detail = detail_card(doc)
    # Above 100 % text the servers' rows, the empty state's sentence, the
    # scan's status and the details card grow with it (section 2's reflow);
    # the filters, headings, tags and actions are furniture and keep their
    # size.
    from build_retained_screens import fixed_text
    grow_empty_line(doc, empty, fixes)
    grow_card(doc, detail)
    for node in [*parts, join, spectate, favorite]:
        if node["id"] not in ("servers-list", "join-status", "join-empty"):
            fixed_text(doc, node)
    result = subpage_document(doc, "join", "#str_200002", "#str_200187", "join_newlist", [*parts, join, spectate, favorite, back, detail],
                              [join_ip_modal(doc), find_modal(doc)], stretch=("servers-list",))
    grow_status(doc, result)
    return tall_join_page(result)


def grow_empty_line(doc: Document, sentence: dict, plates: list) -> None:
    """The empty list's sentence above 100 % text: it wraps onto the lines the
    running language needs and CLEAR FILTERS or GET NEW LIST moves down with
    it (retained_saves.grow_empty's rule)."""
    from build_retained_screens import LARGER, per_language, text_buckets, text_source, text_sum, text_times, wrapped_lines
    text_source(doc)
    props = sentence["properties"]
    counts = wrapped_lines(("#str_231133", "#str_231134"), props["font-family"]["value"], props["font-size"]["value"], props["width"]["value"])
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    authored = props["height"]["value"]
    needed = {"op": "max", "args": [authored, text_times({"op": "*", "args": [lines, props["line-height"]["value"]]})]}
    doc.bind("text.join-empty.height", "join-empty", "height", {"op": "select", "args": [LARGER, needed, authored]})
    doc.bind("text.join-empty.white-space", "join-empty", "white-space", {"op": "select", "args": [LARGER, "normal", "nowrap"]})
    for plate in plates:
        top = plate["properties"]["top"]["value"]
        doc.bind(f"text.{plate['id']}.top", plate["id"], "top", {"op": "select", "args": [LARGER, text_sum(top - authored, needed), top]})


def grow_card(doc: Document, card: dict) -> None:
    """The details card above 100 % text: the card and everything on it are
    the text size times their 100 % size, so its lines keep their pitch and
    columns; its headings are furniture and keep their size. The runtime
    still sets the card beside its row."""
    from build_retained_screens import fixed_text, in_fractions, text_source, text_times, walk_nodes
    text_source(doc)
    def walk(node: dict, box: tuple) -> None:
        props = node.get("properties", {})
        size = []
        for key, extent in (("width", box[0]), ("height", box[1])):
            found = props.get(key) or {}
            size.append(found["value"] if found.get("unit") == "dp" else found["value"] * extent / 100 if found.get("unit") == "%" and extent else None)
        if node.get("type") == "vector" and size[0] and size[1]:
            in_fractions(node, *size)
        for key in ("left", "top", "width", "height"):
            found = props.get(key) or {}
            if found.get("type") == "length" and found.get("unit") == "dp" and found["value"]:
                doc.bind(f"text.{node['id']}.{key}", node["id"], key, text_times(found["value"]))
        for child in node.get("children", []):
            walk(child, tuple(size))
    walk(card, (None, None))
    for node in walk_nodes(card):
        if node["id"] in ("join-detail-players", "join-detail-settings"):
            fixed_text(doc, node)


def grow_status(doc: Document, result: dict) -> None:
    """The scan's status above 100 % text: its line grows upward with the
    text from where it stands over the bottom band, and the filter row over
    it and the list's foot move up as far, the list scrolling through fewer
    rows (the narrow-view fit holds these parts from the view's bottom).
    Every binding gives the authored value at 100 % (check_text_bindings)."""
    from build_retained_screens import LARGER, TEXT, text_source, text_sum, text_times, walk_nodes
    text_source(doc)
    nodes = {node["id"]: node for node in walk_nodes(result["root"])}
    status = nodes["join-status"]["properties"]
    height = status["height"]["value"]
    growth = {"op": "*", "args": [height, {"op": "-", "args": [TEXT, 1]}]}
    doc.bind("text.join-status.height", "join-status", "height", text_times(height))
    # Without a player search's tag after it, the status runs on to CLEAR
    # SORTING.
    searching = {"op": "!=", "args": [{"state": "join.search"}, ""]}
    width = status["width"]["value"]
    room = round(U * (CLEAR_SORT[0] - clear_sort_width() - STATUS[0] - 6), 4)
    doc.bind("text.join-status.width", "join-status", "width",
             {"op": "select", "args": [{"op": "&&", "args": [LARGER, {"op": "!", "args": [searching]}]}, room, width]})
    status["text-overflow"] = {"type": "keyword", "value": "clip"}
    doc.bind("text.join-status.text-overflow", "join-status", "text-overflow", {"op": "select", "args": [LARGER, "ellipsis", "clip"]})
    moved = ["servers-list", "servers_scroll", "join-gametype-value", "join_gametype_previous", "join_gametype_next"]
    moved += [f"join_filter_{ident}" for ident, *_ in filter_tags()]
    for ident in moved:
        bottom = nodes[ident]["properties"]["bottom"]["value"]
        doc.bind(f"text.{ident}.bottom", ident, "bottom", {"op": "select", "args": [LARGER, text_sum(bottom, growth), bottom]})


# A tall view's Join Game page (retained_tall): the mod spinner and the source
# tags in two lines under the band, then the sort headings and the list across
# the column, a server in two lines 1.5 times the landscape size: its symbols
# and name, then its ping, repeater, players, game type and map. Under the
# list, 1.2 times the landscape size so that each line fits across the column:
# the game type spinner (with the search tag beside it) and the filter tags in
# two lines, the status with CLEAR SORTING, the load bar, then the list's
# actions in one column with the levelshot beside them, and JOIN SERVER with
# ADD TO FAVORITES over the bottom band.
TALL_GROW = 1.5
TALL_CONTROLS = 1.3
TALL_FOOT = 1.2
SECOND_LINE = 279.0                        # u: where the row's second line starts (the ping's tab stop)
FOOT_BOTTOM = 72.0                         # dp: JOIN SERVER, over the prompt bar
FOOT_GAP = 10.0                            # dp: between the footer's lines


def tall_join_page(result: dict) -> dict:
    from retained_tall import CONTENT_W, MARGIN, TallLayout
    tall = TallLayout(result)
    list_w = CONTENT_W - 40
    controls_h = U * 16 * TALL_CONTROLS
    tall.block(("join-mod", "join_mod_previous", "join-mod-value", "join_mod_next"), (U * MOD[0], U * MOD[1]), (MARGIN, 80), TALL_CONTROLS)
    tall.block(("join-source", "join_internet", "join_lan"), (U * SOURCE[0], U * SOURCE[1]), (MARGIN, 86 + controls_h), TALL_CONTROLS)
    tall.show("join-header", "none", "block")
    heads_top = 86 + 2 * controls_h + 14
    head_h = U * 16 * TALL_GROW
    list_left = U * LIST[0]
    for column, *_rest in COLUMNS:
        ident = f"heading-{column}"
        left = tall.dp(ident, "left") - list_left
        second = left >= U * SECOND_LINE - 0.5
        tall.bind(ident, "left", MARGIN + ((left - U * SECOND_LINE + U * COLUMNS[0][3]) if second else left) * TALL_GROW)
        tall.top(ident, heads_top + (head_h if second else 0))
        tall.grow(ident, TALL_GROW, keep_place=True)
    list_top = heads_top + 2 * head_h + 6
    tall.bind("servers-list", "left", MARGIN)
    tall.top("servers-list", list_top)
    tall.bind("servers-list", "width", list_w)
    tall.bind("servers_scroll", "left", MARGIN + list_w + 4)
    tall.top("servers_scroll", list_top)
    tall.grow("servers-column", TALL_GROW)
    line = U * ROW_H * TALL_GROW
    for index in range(JOIN_ROWS):
        ident = f"server-{index}"
        for node in (ident, f"{ident}-line", f"{ident}-hover", f"{ident}-selected", f"{ident}-focus"):
            tall.set(node, "width", list_w)
            tall.set(node, "height", 2 * line)
        for child in tall.nodes[ident]["children"]:
            node = child["id"]
            if node.endswith(("-line", "-hover", "-selected", "-focus")):
                continue
            left, top = tall.dp(node, "left"), tall.dp(node, "top") or 0
            if left >= U * SECOND_LINE - 0.5:
                tall.set(node, "left", (left - U * SECOND_LINE + U * COLUMNS[0][3]) * TALL_GROW)
                tall.set(node, "top", top * TALL_GROW + line)
        for cell in ("name", "map"):
            node = f"{ident}-{cell}"
            start = tall.dp(node, "left") - (U * SECOND_LINE - U * COLUMNS[0][3] if cell == "map" else 0)
            tall.set(node, "width", list_w - start * TALL_GROW - 8)
    tall.bind("join-loading", "left", MARGIN)
    tall.top("join-loading", list_top)
    tall.bind("join-loading", "width", list_w)
    empty_left = tall.dp("join-empty-heading", "left")
    origin = (empty_left, tall.canvas_top("join-empty-heading"))
    # The action moves down by what the sentence grew with the text, grown.
    from build_retained_screens import text_sum
    sentence = next(binding["value"] for binding in tall.bindings if binding["id"] == "text.join-empty.height")
    grew = {"op": "*", "args": [TALL_CONTROLS, text_sum(sentence, -tall.dp("join-empty", "height"))]}
    tall.block(("join-empty-heading", "join-empty"), origin, (MARGIN + 6, list_top + 12), TALL_CONTROLS)
    tall.block(("join_emptyclear", "join_emptynew"), origin, (MARGIN + 6, list_top + 12), TALL_CONTROLS, grew)
    for node in ("join-empty-heading", "join-empty"):
        tall.set(node, "width", list_w - 12)
    tall_join_foot(tall)
    tall.dialogs(TALL_GROW)
    tall.shade("join-backing", BACKING_ALPHA)
    return result


def tall_join_foot(tall) -> None:
    """The parts under the list in a tall view, from the bottom band up (see
    TALL_FOOT), and the list's foot over them."""
    from retained_tall import CANVAS_W, MARGIN
    grown = lambda ident, prop: tall.dp(ident, prop) * TALL_FOOT  # noqa: E731
    right = CANVAS_W - MARGIN

    def foot(ids, at: tuple, factor: float = TALL_FOOT) -> None:
        tall.block_bottom(ids, (tall.dp(ids[0], "left"), tall.dp(ids[0], "bottom")), at, factor)

    server_h, favorite_h = grown("join_server", "height"), grown("join_favorite", "height")
    if MARGIN + grown("join_server", "width") + 12 + grown("join_favorite", "width") > right:
        raise SystemExit("JOIN SERVER and ADD TO FAVORITES do not fit across a tall view's column")
    foot(("join_server",), (MARGIN, FOOT_BOTTOM))
    beside = MARGIN + grown("join_server", "width") + 12
    foot(("join_favorite",), (beside, FOOT_BOTTOM + (server_h - favorite_h) / 2))
    # SPECTATE stands over the favorite toggle, a row of its own.
    spectate_bottom = FOOT_BOTTOM + server_h + 4
    if beside + grown("join_spectate", "width") > right:
        raise SystemExit("SPECTATE does not fit over ADD TO FAVORITES in a tall view's column")
    foot(("join_spectate",), (beside, spectate_bottom))
    actions = tuple(ident for ident, *_rest in reversed(LIST_ACTIONS))
    actions_bottom = spectate_bottom + grown("join_spectate", "height") + 10
    foot(actions, (MARGIN, actions_bottom))
    actions_top = actions_bottom + (tall.dp(actions[-1], "bottom") + tall.dp(actions[-1], "height") - tall.dp(actions[0], "bottom")) * TALL_FOOT
    shot = ("join-shot-back", "join-shot", "join-shot-border")
    scale = (actions_top - actions_bottom) / tall.dp(shot[0], "height")
    foot(shot, (right - tall.dp(shot[0], "width") * scale, actions_bottom), scale)
    # The load bar keeps its width across the view (its fill's length is the
    # scan's fraction of it) and grows in height with its notice.
    load_bottom = actions_top + FOOT_GAP
    track_h, notice_h = grown("join-load-track", "height"), grown("join-load-notice", "height")
    for ident in ("join-load-track", "join-load-fill"):
        tall.bind(ident, "bottom", load_bottom)
        tall.bind(ident, "height", track_h)
    tall.bind("join-load-fill-tube", "height", track_h)
    tall.bind("join-load-notice", "bottom", load_bottom + (track_h - notice_h) / 2)
    for prop in ("height", "font-size", "line-height"):
        tall.bind("join-load-notice", prop, grown("join-load-notice", prop))
    status_bottom = load_bottom + track_h + FOOT_GAP
    clear_h, status_h = grown("join_clearsort", "height"), grown("join-status", "height")
    foot(("join_clearsort",), (right - grown("join_clearsort", "width"), status_bottom))
    foot(("join-status",), (MARGIN + (tall.dp("join-status", "left") - tall.dp(actions[0], "left")) * TALL_FOOT,
                            status_bottom + (clear_h - status_h) / 2))
    filters_bottom = status_bottom + clear_h + 8
    foot(("join_filter_full", "join_filter_empty", "join_filter_password"), (MARGIN, filters_bottom))
    spin = ("join_gametype_previous", "join-gametype-value", "join_gametype_next")
    spin_bottom = filters_bottom + grown(spin[0], "height") + 8
    foot(spin, (MARGIN, spin_bottom))
    spin_w = (tall.dp(spin[2], "left") + tall.dp(spin[2], "width") - tall.dp(spin[0], "left")) * TALL_FOOT
    foot(("join_search",), (MARGIN + spin_w + 20, spin_bottom))
    list_foot = spin_bottom + grown(spin[0], "height") + 14
    for ident in ("servers-list", "servers_scroll"):
        tall.bind(ident, "bottom", list_foot)
