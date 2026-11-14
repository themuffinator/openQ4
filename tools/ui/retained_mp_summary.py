"""The retained match summary (docs/dev/ui-visual-design.md section 14.14).

Built by tools/ui/build_retained_screens.py with the multiplayer menus. When a
match ends the summary card covers the game's summary (summary.gui) the way
the Escape card covers its menu (retained_mp_menus.py): the multiplayer card
of section 14.18, with SUMMARY, STATISTICS and SCOREBOARD tabs, over the
softened view. The game publishes the result, the standings with each
player's best weapons, the chat and the lists; the session hands the card's
verbs to the game as the summary's own commands and owns the chat field's
typing (idSessionLocal::HandleRetainedMultiplayerRequest).
"""
from __future__ import annotations

import build_retained_screens as b
from build_retained_screens import (FULL, ORANGE, VALUE, Document, absolute, colour, group, keyword, label, length, linear, number,
                                    path, picture, rgb, solid, stroke, track, transform, typeface, vector)
from remastered_hud_base import AND, NOT, OR, S, op, sel
import retained_mp_menus as mp
from retained_mp_menus import (BAND_TINTS, ESCAPE_H, ESCAPE_TABS, INSET, KEY_ENTER, KEY_ESCAPE, KEY_NEXT, KEY_PREVIOUS, MAIN_MENU, fit_card,
                               DISCONNECT, PAGE_TOP, PLACEHOLDER, PROMPT_BOTTOM, PROMPT_H, VERB_RESUME, VERB_SELECT, VERB_TABS, WEAPONS,
                               any_text, backdrop, bound_text, card_frame, card_header, card_motion, fitted_card_width, keycap,
                               page_group, player_lists, player_stats, tab_strip, team_band)

# The tabs in the stock summary's order; the stock summary has no stock page
# to hand off to.
SUMMARY_TABS = [("summary", "#str_200282", None), ("statistics", "#str_200283", None), ("scoreboard", "#str_200284", None)]
RETURN_TO_GAME = "#str_200005"
CHAT_HEADING, ACCURACY_KEY, CLAN_KEY = "#str_200196", "#str_200203", "#str_200256"
NAME_KEY, SCORE_KEY, KILLS_KEY, DEATHS_KEY, TIME_KEY, PING_KEY = (
    "#str_200242", "#str_200198", "#str_200201", "#str_200202", "#str_200244", "#str_200037")
# The prompt bar's verbs in capitals, as its others are.
SEND_WORD, CHANNEL_WORD, CANCEL_WORD, KEY_TAB = "#str_200319", "#str_231260", "#str_200168", "#str_107018"
ALL_TAG, TEAM_TAG, BOT_TAG, COLON = "#str_231254", "#str_231001", "#str_231231", "#str_231227"
CHAT_HINT = "#str_231259"
NOTE, ERROR, DARK = "#FFFF8D", "#E46D56", "#10140A"
TEAM_TEXT = ("#AAE355", "#FF8E00")            # the chat's Marine and Strogg text colors (section 14.9)
CHAT_RAIL = "#CCCC51"                         # the stock chat plate's rails

# The SUMMARY page: the result band across the page, then the standings and
# the chat side by side under it.
RESULT_H, COLUMN_TOP = 40.0, 52.0
STAND_W, COLUMN_GAP = 488.0, 24.0
HEAD_H, BAND_H, PANEL_GAP = 16.0, 22.0, 6.0
ROW_MIN, ROW_MAX, ROW_FACE = 14.0, 24.0, 14.0
# The standings' panels: the winners' (or deathmatch's and Tourney's top
# ten) and the other team's top five (RETAINED_SUMMARY_ROWS in the game).
SUMMARY_ROWS = (10, 5)
MARK_W, PLACE_W, CLAN_W, SCORE_W, ICON, ICON_GAP = 12.0, 26.0, 88.0, 50.0, 18.0, 4.0
# The stock's first three places: blue, red and yellow bands at 0.15.
PLACE_TINTS = ((0, 0, 1), (1, 0, 0), (1, 1, 0))
# The chat: the newest messages that fit (RETAINED_SUMMARY_CHAT in the game),
# then the field.
CHAT_SLOTS, CHAT_LIMIT = 10, 128
FIELD_H, CHAT_FACE, CHAT_LINE = 30.0, 14.0, 18.0
# The STATISTICS page: the player's end-of-match awards where the Players
# page has Mute and Friend (RETAINED_SUMMARY_AWARDS in the game).
MATCH_AWARDS, AWARD_TOP, AWARD_PITCH = 3, 196.0, 34.0
# The SCOREBOARD page's columns.
BOARD_CLAN_W, NUMBER_W, BARS_W, BOARD_PING_W = 96.0, 56.0, 22.0, 44.0


def card_width() -> float:
    """The Escape card's width, so the summary shares its Players page."""
    return fitted_card_width([key for _, key, _ in ESCAPE_TABS], (VERB_RESUME, [MAIN_MENU, DISCONNECT]))


def shown(doc: Document, node: str, condition, on: str = "block") -> None:
    doc.bind(f"{node}.display", node, "display", sel(condition, on, "none"))


def flow(ident: str, key: str, face: str, size: float, line: float, tint: list, *, width: float | None = None,
         grow: bool = False, align: str | None = None, **more) -> dict:
    """A label in a flex row: `width` dp wide or growing into what is left,
    clipped with an ellipsis, or as wide as its text."""
    props = {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"),
             "flex-shrink": number(1 if grow else 0), **typeface(face, size, line, tint)}
    if grow or width is not None:
        props.update({"overflow": keyword("hidden"), "text-overflow": keyword("ellipsis")})
    if grow:
        props.update({"flex-grow": number(1), "min-width": length(0)})
    if width is not None:
        props["width"] = length(width)
    if align:
        props["text-align"] = keyword(align)
    props.update(more)
    return label(ident, key, props)


def row_flex(ident: str, props: dict, children: list) -> dict:
    return group(ident, {"display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("center"), **props},
                 children)


def slot(ident: str, width: float, children=(), **more) -> dict:
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(width), "height": length(16),
                         "flex-shrink": number(0), **more}, children)


def rule(ident: str, props: dict, alpha: float = 0.1) -> dict:
    return vector(ident, props, [path("rule", [(0, 0.5), ({"fraction": 1}, 0.5)], closed=False, stroke=stroke(solid([1, 1, 1, alpha]), 1))])


def you_marker(ident: str, top: float = 4) -> dict:
    """Your row's marker (section 14.14): the orange triangle."""
    return vector(ident, {**absolute(left=0, top=top, width=8, height=8), "display": keyword("none")},
                  [path("m", [(0, 0), (8, 0), (8, 8)], fill=solid(rgb(ORANGE)))])


def tag(doc: Document, ident: str, key: str, variants: list, **more) -> dict:
    """A tag in the header-band construction (section 14.9): a small plate
    with a cut lower-leading corner in the given color at 0.85 and dark
    Lowpixel text. `variants` are (suffix, color, condition) plates."""
    plates = []
    for suffix, tint, condition in variants:
        node = f"{ident}-{suffix}"
        plates.append(vector(node, {**FULL, "display": keyword("block")}, [
            path("t", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (4, {"fraction": 1}),
                       (0, {"fraction": 1, "dp": -4})], fill=solid([*tint[:3], 0.85]))]))
        if condition is not None:
            shown(doc, node, condition)
    text = label(f"{ident}-text", key, {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"),
                                        **typeface("lowpixel", 10.8, 15, rgb(DARK)), "letter-spacing": length(0.6)})
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "height": length(15), "flex-shrink": number(0),
                         "padding-left": length(6), "padding-right": length(6), **more}, [*plates, text])


# ------------------------------------------------------------- the result

def result_band(doc: Document, width: float) -> dict:
    """The result (section 14.14): a header band across the page in the
    winners' color, olive for deathmatch, Tourney and a draw, holding the
    result in Marine, its detail and the final score in the value color."""
    doc.state.update({
        "mp.summary.result": {"type": "string", "initial": ""}, "mp.summary.result_color": {"type": "number", "initial": 3},
        "mp.summary.result_detail": {"type": "string", "initial": ""}, "mp.summary.result_score": {"type": "string", "initial": ""},
    })
    bands = []
    for index, tint in enumerate(BAND_TINTS):
        node = f"summary-result-band-{index}"
        bands.append(team_band(node, width, RESULT_H, tint, cut=8))
        shown(doc, node, op("==", S("mp.summary.result_color"), index))
    title = flow("summary-result-title", PLACEHOLDER, "marine", 22, RESULT_H, [1, 1, 1, 0.95], grow=True)
    detail = flow("summary-result-detail", PLACEHOLDER, "lowpixel", 15, RESULT_H, [1, 1, 1, 0.75], **{"margin-left": length(16)})
    score = flow("summary-result-score", PLACEHOLDER, "lowpixel", 24, RESULT_H, rgb(VALUE), **{"margin-left": length(16)})
    for node, key in (("title", "result"), ("detail", "result_detail"), ("score", "result_score")):
        doc.bind(f"summary-result-{node}.text", f"summary-result-{node}", "text", S(f"mp.summary.{key}"))
    row = row_flex("summary-result-row", absolute(left=20, top=0, right=12, height=RESULT_H), [title, detail, score])
    return group("summary-result", absolute(left=0, top=0, width=width, height=RESULT_H), [*bands, row])


# ------------------------------------------------------------ the standings

def weapon_icon(doc: Document, ident: str, weapon) -> dict:
    """One of a player's three most accurate weapons: its icon in its color
    code (Appendix B.4), `weapon` the index into WEAPONS or -1."""
    path_expr, tints = WEAPONS[-1][1], WEAPONS[-1][2]
    source = f"gfx/guis/hud/icons/{path_expr}"
    for index in reversed(range(len(WEAPONS) - 1)):
        source = sel(op("==", weapon, index), f"gfx/guis/hud/icons/{WEAPONS[index][1]}", source)
    tint = []
    for channel in range(3):
        value = tints[channel]
        for index in reversed(range(len(WEAPONS) - 1)):
            value = sel(op("==", weapon, index), WEAPONS[index][2][channel], value)
        tint.append(value)
    node = picture(ident, f"gfx/guis/hud/icons/{WEAPONS[0][1]}", {"position": keyword("relative"), "display": keyword("none"),
                   "width": length(ICON), "height": length(ICON), "flex-shrink": number(0), "margin-right": length(ICON_GAP),
                   "image-color": colour([*WEAPONS[0][2], 1])}, fit="contain")
    doc.bind(f"{ident}.image", ident, "image", source)
    doc.bind(f"{ident}.tint", ident, "image-color", [*tint, 1])
    shown(doc, ident, op(">=", weapon, 0))
    return node


def accuracy_width() -> float:
    """The weapons' column: three icons, or the ACCURACY heading in the
    widest language."""
    return round(max(3 * (ICON + ICON_GAP), mp.widest([ACCURACY_KEY], "lowpixel", 11) + 4), 3)


def standing_row(doc: Document, p: int, r: int, place: bool) -> dict:
    """One row of the standings: the scoreboard's band at 0.08, your row
    0.29 with the orange marker; the first three places keep the stock's
    blue, red and yellow bands; the place, the name, the clan, the three most
    accurate weapons and the score."""
    ident = f"summary-p{p}r{r}"
    key = f"mp.summary.p{p}r{r}"
    doc.state.update({
        f"{key}.name": {"type": "string", "initial": ""}, f"{key}.clan": {"type": "string", "initial": ""},
        f"{key}.score": {"type": "string", "initial": ""}, f"{key}.place": {"type": "string", "initial": ""},
        f"{key}.you": {"type": "boolean", "initial": False}, f"{key}.tint": {"type": "number", "initial": 0},
        **{f"{key}.w{k}": {"type": "number", "initial": -1} for k in range(3)},
    })
    whole = [(0, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1})]
    tint = S(f"{key}.tint")
    band = vector(f"{ident}-band", {**FULL, "opacity": number(0.08)}, [path("band", whole, fill=solid([1, 1, 1, 1]))])
    doc.bind(f"{ident}-band.opacity", f"{ident}-band", "opacity", sel(op(">", tint, 0), 0, sel(S(f"{key}.you"), 0.29, 0.08)))
    places = []
    for index, colour_value in enumerate(PLACE_TINTS):
        node = f"{ident}-tint{index + 1}"
        places.append(vector(node, {**FULL, "display": keyword("none")}, [path("band", whole, fill=solid([*colour_value, 0.15]))]))
        shown(doc, node, op("==", tint, index + 1))
    marker = you_marker(f"{ident}-marker")
    shown(doc, f"{ident}-marker", S(f"{key}.you"))
    white = [1, 1, 1, 0.85]
    cells = [slot(f"{ident}-mark", MARK_W, [marker])]
    if place:
        cells.append(flow(f"{ident}-place", PLACEHOLDER, "lowpixel", ROW_FACE, 16, [1, 1, 1, 0.6], width=PLACE_W,
                          **{"margin-right": length(6)}))
        doc.bind(f"{ident}-place.text", f"{ident}-place", "text", S(f"{key}.place"))
    name = flow(f"{ident}-name", PLACEHOLDER, "lowpixel", ROW_FACE, 16, white, grow=True)
    clan = flow(f"{ident}-clan", PLACEHOLDER, "lowpixel", ROW_FACE, 16, [1, 1, 1, 0.6], width=CLAN_W, **{"margin-left": length(8)})
    for part, node in (("name", name), ("clan", clan)):
        doc.bind(f"{ident}-{part}.text", f"{ident}-{part}", "text", S(f"{key}.{part}"))
    icons = row_flex(f"{ident}-weapons", {"position": keyword("relative"), "width": length(accuracy_width()), "height": length(16),
                                          "flex-shrink": number(0), "margin-left": length(8)},
                     [weapon_icon(doc, f"{ident}-w{k}", S(f"{key}.w{k}")) for k in range(3)])
    score = flow(f"{ident}-score", PLACEHOLDER, "lowpixel", ROW_FACE, 16, white, width=SCORE_W, align="right",
                 **{"margin-right": length(4)})
    doc.bind(f"{ident}-score.text", f"{ident}-score", "text", S(f"{key}.score"))
    content = row_flex(f"{ident}-content", {**absolute(left=0, top=0, right=0, bottom=0), "pointer-events": keyword("none")},
                       [*cells, name, clan, icons, score])
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(100, "%"), "height": length(ROW_MAX),
                         "min-height": length(ROW_MIN), "flex-shrink": number(1)}, [band, *places, content])
    shown(doc, ident, op("<", r, S(f"mp.summary.p{p}.count")))
    return node


def standings(doc: Document, left: float, top: float, width: float, height: float) -> dict:
    """The standings (section 14.14): the headings once, then each panel,
    its band in the team's color with its title and score in team modes,
    over the scoreboard's rows; deathmatch and Tourney list the top ten with
    their places in one panel without a band. Rows share the room, 24 dp
    each down to 14 dp."""
    heading = [1, 1, 1, 0.4]
    place = op("!", S("mp.summary.teams"))
    doc.state["mp.summary.teams"] = {"type": "boolean", "initial": False}
    lead = slot("summary-head-lead", MARK_W + PLACE_W + 6)
    doc.bind("summary-head-lead.width", "summary-head-lead", "width", sel(place, MARK_W + PLACE_W + 6, MARK_W))
    cells = [lead, flow("summary-head-name", NAME_KEY, "lowpixel", 11, HEAD_H, heading, grow=True),
             flow("summary-head-clan", CLAN_KEY, "lowpixel", 11, HEAD_H, heading, width=CLAN_W, **{"margin-left": length(8)}),
             flow("summary-head-accuracy", ACCURACY_KEY, "lowpixel", 11, HEAD_H, heading, width=accuracy_width(),
                  **{"margin-left": length(8)}),
             flow("summary-head-score", SCORE_KEY, "lowpixel", 11, HEAD_H, heading, width=SCORE_W, align="right",
                  **{"margin-right": length(4)})]
    headings = group("summary-headings", {"position": keyword("relative"), "display": keyword("block"), "width": length(width),
                                          "height": length(HEAD_H), "flex-shrink": number(0)},
                     [row_flex("summary-head-row", absolute(left=0, top=0, right=0, height=HEAD_H), cells),
                      rule("summary-head-rule", absolute(left=0, top=HEAD_H - 1, width=width, height=1))])
    children = [headings]
    for p, rows in enumerate(SUMMARY_ROWS):
        key = f"mp.summary.p{p}"
        doc.state.update({f"{key}.shown": {"type": "boolean", "initial": False}, f"{key}.band": {"type": "boolean", "initial": False},
                          f"{key}.title": {"type": "string", "initial": ""}, f"{key}.color": {"type": "number", "initial": 3},
                          f"{key}.score": {"type": "string", "initial": ""}, f"{key}.count": {"type": "number", "initial": 0}})
        variants = []
        for index, tint in enumerate(BAND_TINTS):
            node = f"summary-p{p}-band-{index}"
            variants.append(team_band(node, width, BAND_H, tint, cut=6))
            shown(doc, node, op("==", S(f"{key}.color"), index))
        title = label(f"summary-p{p}-title", PLACEHOLDER, {**absolute(left=16, top=0, width=width * 0.6, height=BAND_H),
                      **typeface("marine", 15, BAND_H, [1, 1, 1, 0.95]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        score = label(f"summary-p{p}-score", PLACEHOLDER, {**absolute(right=4, top=0, width=110, height=BAND_H),
                      **typeface("lowpixel", 16, BAND_H, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
        doc.bind(f"summary-p{p}-title.text", f"summary-p{p}-title", "text", S(f"{key}.title"))
        doc.bind(f"summary-p{p}-score.text", f"summary-p{p}-score", "text", S(f"{key}.score"))
        band = group(f"summary-p{p}-band", {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                                            "height": length(BAND_H), "flex-shrink": number(0),
                                            "margin-top": length(0 if p == 0 else PANEL_GAP)}, [*variants, title, score])
        shown(doc, f"summary-p{p}-band", AND(S(f"{key}.shown"), S(f"{key}.band")))
        children.append(band)
        children += [standing_row(doc, p, r, place=(p == 0)) for r in range(rows)]
    # The other team's panel never shows places; the winners' shows them
    # outside team modes.
    for r in range(SUMMARY_ROWS[0]):
        shown(doc, f"summary-p0r{r}-place", place)
    worst = HEAD_H + 2 * BAND_H + PANEL_GAP + (SUMMARY_ROWS[1] * 2) * ROW_MIN
    if worst > height or HEAD_H + SUMMARY_ROWS[0] * ROW_MIN > height:
        raise SystemExit(f"the standings need {worst:g} dp, past their {height:g} dp")
    return group("summary-standings", {**absolute(left=left, top=top, width=width, height=height), "display": keyword("flex"),
                                       "flex-direction": keyword("column"), "overflow": keyword("hidden")}, children)


# ------------------------------------------------------------------ the chat

def info_symbol(ident: str) -> dict:
    """The information symbol before a server or vote line (section 14.9)."""
    tint = [1, 1, 1, 0.7]
    ring = [(6.5 + 5.2 * x, 7.5 + 5.2 * y) for x, y in ((0, -1), (0.71, -0.71), (1, 0), (0.71, 0.71), (0, 1), (-0.71, 0.71), (-1, 0),
                                                       (-0.71, -0.71))]
    return vector(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(13), "height": length(15),
                          "flex-shrink": number(0), "margin-right": length(5)}, [
        path("ring", ring, stroke=stroke(solid(tint), 1.2)),
        path("dot", [(6.5, 4.4), (6.5, 5.6)], closed=False, stroke=stroke(solid(tint), 1.4)),
        path("bar", [(6.5, 6.8), (6.5, 10.8)], closed=False, stroke=stroke(solid(tint), 1.4)),
    ])


def chat_message(doc: Document, slot_index: int) -> dict:
    """A chat message (section 14.9's lines, on the card): the name, then the
    message, wrapping; all-chat text in #FFFF8D under a white name, a team
    message opening with the TEAM tag in its team's text color, a server or
    vote line after the information symbol in white at 0.70, and your own
    lines after the orange marker."""
    key = f"mp.summary.chat{slot_index}"
    doc.state.update({f"{key}.live": {"type": "boolean", "initial": False}, f"{key}.name": {"type": "string", "initial": ""},
                      f"{key}.text": {"type": "string", "initial": ""}, f"{key}.kind": {"type": "number", "initial": 0},
                      f"{key}.team": {"type": "number", "initial": -1}, f"{key}.you": {"type": "boolean", "initial": False}})
    ident = f"summary-chat{slot_index}"
    kind, team = S(f"{key}.kind"), S(f"{key}.team")
    team_line, server = op("==", kind, 1), op("==", kind, 2)
    strogg = op("==", team, 1)
    team_colour = [sel(strogg, rgb(TEAM_TEXT[1])[c], rgb(TEAM_TEXT[0])[c]) for c in range(4)]
    text_colour = [sel(server, [1, 1, 1, 0.7][c], sel(team_line, team_colour[c], rgb(NOTE)[c])) for c in range(4)]
    name_colour = [sel(AND(team_line, NOT(S(f"{key}.you"))), team_colour[c], [1, 1, 1, 1][c]) for c in range(4)]
    marker = vector(f"{ident}-marker", {"position": keyword("relative"), "display": keyword("none"), "width": length(8),
                                         "height": length(8), "flex-shrink": number(0), "margin-top": length(4), "margin-right": length(5)},
                    [path("m", [(0, 0), (8, 0), (8, 8)], fill=solid(rgb(ORANGE)))])
    shown(doc, f"{ident}-marker", S(f"{key}.you"))
    team_tag = tag(doc, f"{ident}-tag", TEAM_TAG, [("m", rgb(TEAM_TEXT[0]), NOT(strogg)), ("s", rgb(TEAM_TEXT[1]), strogg)],
                   **{"margin-right": length(6), "margin-top": length(1.5)})
    team_tag["properties"]["display"] = keyword("none")
    shown(doc, f"{ident}-tag", team_line)
    info = info_symbol(f"{ident}-info")
    shown(doc, f"{ident}-info", server)
    face = typeface("lowpixel", CHAT_FACE, CHAT_LINE, [1, 1, 1, 1])
    name = label(f"{ident}-name", PLACEHOLDER, {**face, "display": keyword("inline")})
    colon = label(f"{ident}-colon", COLON, {**face, "display": keyword("inline"), "white-space": keyword("pre")})
    body = label(f"{ident}-text", PLACEHOLDER, {**typeface("lowpixel", CHAT_FACE, CHAT_LINE, rgb(NOTE)), "display": keyword("inline")})
    doc.bind(f"{ident}-name.text", f"{ident}-name", "text", S(f"{key}.name"))
    doc.bind(f"{ident}-text.text", f"{ident}-text", "text", S(f"{key}.text"))
    doc.bind(f"{ident}-name.color", f"{ident}-name", "color", name_colour)
    doc.bind(f"{ident}-colon.color", f"{ident}-colon", "color", name_colour)
    doc.bind(f"{ident}-text.color", f"{ident}-text", "color", text_colour)
    shown(doc, f"{ident}-name", NOT(server), "inline")
    shown(doc, f"{ident}-colon", NOT(server), "inline")
    words = group(f"{ident}-words", {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                                     "min-width": length(0), "white-space": keyword("normal"), "word-break": keyword("break-word")},
                  [name, colon, body])
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "flex-direction": keyword("row"),
                         "align-items": keyword("flex-start"), "width": length(100, "%"), "margin-top": length(4)},
                 [marker, team_tag, info, words])
    shown(doc, ident, S(f"{key}.live"), "flex")
    return node


def chat_field(doc: Document, width: float) -> tuple[dict, str]:
    """The entry field under the messages: the stock chat plate (section
    14.9), black easing toward the trailing end with #CCCC51 rails down the
    leading side, along the lower-leading cut and the first 70% of the
    bottom; in it the channel tag, ALL in #FFFF8D or TEAM in your team's text
    color, and the entry in Lowpixel #FFFF8D with the caret blinking once a
    second while you type, steady under reduced motion. At rest an empty
    field invites a message. The field is a control: choosing it starts the
    typing the session owns, and the rails turn orange with focus; from 80%
    of the stock 128-character limit the characters left show."""
    doc.state.update({
        "mp.summary.typing": {"type": "boolean", "initial": False}, "mp.summary.entry_before": {"type": "string", "initial": ""},
        "mp.summary.entry_after": {"type": "string", "initial": ""}, "mp.summary.entry_left": {"type": "number", "initial": CHAT_LIMIT},
        "mp.summary.entry_team": {"type": "boolean", "initial": False}, "mp.summary.my_team": {"type": "number", "initial": 0},
    })
    ident = "summary-chat-field"
    typing = S("mp.summary.typing")
    empty = AND(op("==", S("mp.summary.entry_before"), ""), op("==", S("mp.summary.entry_after"), ""))
    plate = vector(f"{ident}-plate", FULL, [
        path("fill", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (12, {"fraction": 1}), (0, {"fraction": 1, "dp": -12})],
             fill=linear((0, 0), ({"fraction": 1}, 0), [(0, [1, 1, 1, 0.07]), (0.7, [1, 1, 1, 0.07]), (1, [1, 1, 1, 0.02])])),
    ])
    rails = []
    for name, tint, rest in (("rail", rgb(CHAT_RAIL, 0.8), 1), ("rail-hot", rgb(ORANGE), 0)):
        rails.append(vector(f"{ident}-{name}", {**FULL, "opacity": number(rest)}, [
            path("r", [(0.75, 0), (0.75, {"fraction": 1, "dp": -12}), (12, {"fraction": 1, "dp": -0.75}),
                       ({"fraction": 0.7}, {"fraction": 1, "dp": -0.75})], closed=False, stroke=stroke(solid(tint), 1.5))]))
    strogg = op("==", S("mp.summary.my_team"), 1)
    tag_all = tag(doc, f"{ident}-all", ALL_TAG, [("c", rgb(NOTE), None)])
    shown(doc, f"{ident}-all", NOT(S("mp.summary.entry_team")))
    tag_team = tag(doc, f"{ident}-team", TEAM_TAG, [("m", rgb(TEAM_TEXT[0]), NOT(strogg)), ("s", rgb(TEAM_TEXT[1]), strogg)])
    tag_team["properties"]["display"] = keyword("none")
    shown(doc, f"{ident}-team", S("mp.summary.entry_team"))
    entry = {**typeface("lowpixel", 16, FIELD_H, rgb(NOTE)), "white-space": keyword("pre")}
    clear = [0, 0, 0, 0]
    # The text before the caret twice: an invisible spacer in the view's
    # flow, as wide as the text but no wider than the view less 3 dp, which
    # places the caret; and the visible text set flush against the caret in a
    # wide box, so a long entry grows toward the leading edge under the clip.
    spacer = label(f"{ident}-before", PLACEHOLDER, {**entry, "color": colour(clear), "position": keyword("relative"),
                                                     "display": keyword("block"), "flex-shrink": number(1), "min-width": length(0),
                                                     "overflow": keyword("hidden")})
    doc.bind(f"{ident}-before.text", f"{ident}-before", "text", S("mp.summary.entry_before"))
    visible = label(f"{ident}-shown", PLACEHOLDER, {**entry, **absolute(right=0, top=0, width=2000, height=FIELD_H),
                                                     "display": keyword("block"), "text-align": keyword("right")})
    doc.bind(f"{ident}-shown.text", f"{ident}-shown", "text", S("mp.summary.entry_before"))
    after = label(f"{ident}-after", PLACEHOLDER, {**entry, **absolute(left=0, top=0), "display": keyword("block")})
    doc.bind(f"{ident}-after.text", f"{ident}-after", "text", S("mp.summary.entry_after"))
    doc.state["reduced_motion_caret"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"}
    carets = []
    for name, steady in (("caret", False), ("caret-steady", True)):
        caret = group(f"{ident}-{name}", {**absolute(left=0.3, top=7.5, width=1.8, height=15), "display": keyword("none"),
                                          "background-color": colour(rgb(NOTE)), "opacity": number(1)})
        shown(doc, f"{ident}-{name}", AND(typing, S("reduced_motion_caret") if steady else NOT(S("reduced_motion_caret"))))
        carets.append(caret)
    at = group(f"{ident}-at", {"position": keyword("relative"), "display": keyword("block"), "width": length(0),
                               "height": length(FIELD_H), "flex-shrink": number(0)}, [visible, after, *carets])
    line = row_flex(f"{ident}-line", {"position": keyword("relative"), "align-items": keyword("flex-start"), "width": length(100, "%"),
                                      "height": length(FIELD_H), "padding-right": length(3), "box-sizing": keyword("border-box")},
                    [spacer, at])
    hint = label(f"{ident}-hint", CHAT_HINT, {**typeface("lowpixel", 15, FIELD_H, [1, 1, 1, 0.38]), **absolute(left=0, top=0),
                                               "white-space": keyword("nowrap"), "display": keyword("none")})
    shown(doc, f"{ident}-hint", AND(NOT(typing), empty))
    # RmlUi clips an overflow: hidden box only once its content overflows
    # toward its trailing or bottom side; a long entry overflows toward the
    # leading side, so an empty strut always reaches past the trailing edge.
    strut = group(f"{ident}-strut", {**absolute(left=0, top=0, width=4000, height=1), "display": keyword("block"),
                                     "pointer-events": keyword("none")})
    view = group(f"{ident}-view", {"position": keyword("relative"), "display": keyword("block"), "flex-grow": number(1),
                                   "flex-shrink": number(1), "min-width": length(0), "height": length(FIELD_H),
                                   "margin-left": length(8), "margin-right": length(8), "overflow": keyword("hidden")},
                 [strut, line, hint])
    left = S("mp.summary.entry_left")
    counting = op("<=", left, CHAT_LIMIT * 0.2)
    count = label(f"{ident}-count", PLACEHOLDER, {**typeface("lowpixel", 13, FIELD_H, [1, 1, 1, 0.6]), "position": keyword("relative"),
                                                   "display": keyword("none"), "flex-shrink": number(0), "margin-right": length(8)})
    doc.bind(f"{ident}-count.text", f"{ident}-count", "text", op("numberText", op("max", left, 0)))
    doc.bind(f"{ident}-count.color", f"{ident}-count", "color",
             [sel(op("<=", left, 0), rgb(ERROR, 0.8)[c], [1, 1, 1, 0.6][c]) for c in range(4)])
    shown(doc, f"{ident}-count", AND(typing, counting))
    row = row_flex(f"{ident}-row", {**absolute(left=0, top=0, width=length(100, "%"), height=FIELD_H), "padding-left": length(10),
                                    "box-sizing": keyword("border-box"), "pointer-events": keyword("none")},
                   [tag_all, tag_team, view, count])
    ids = doc.states(ident, {
        "default": [(f"{ident}-rail", "opacity", number(1)), (f"{ident}-rail-hot", "opacity", number(0))],
        "hover": [(f"{ident}-rail", "opacity", number(1)), (f"{ident}-rail-hot", "opacity", number(0.6))],
        "focus": [(f"{ident}-rail", "opacity", number(0)), (f"{ident}-rail-hot", "opacity", number(1))],
        "pressed": [(f"{ident}-rail", "opacity", number(0)), (f"{ident}-rail-hot", "opacity", number(1))],
        "disabled": [(f"{ident}-rail", "opacity", number(1)), (f"{ident}-rail-hot", "opacity", number(0))],
    })
    doc.events["summary_chat"] = [{"op": "action", "action": "mpSummaryChat"}]
    doc.session("mpSummaryChat", "mpSummaryChat")
    field = group(ident, {**absolute(left=0, bottom=0, width=width, height=FIELD_H), "display": keyword("block")},
                  [plate, *rails, row], control={"role": "button", "label": CHAT_HINT, "event": "summary_chat", "states": ids})
    # Typing restarts the blink on.
    doc.timelines.add("summary-caret-blink", 1000, [
        track(f"{ident}-caret", "opacity", [(0, number(1)), (1, number(1)), (499, number(1)), (500, number(0)), (999, number(0)),
                                            (1000, number(1))])],
        iterations=0)
    doc.events["chatType"] = [{"op": "playTimeline", "timeline": "summary-caret-blink"}]
    return field, ident


def chat_column(doc: Document, left: float, top: float, width: float, height: float) -> tuple[dict, str]:
    """The chat beside the standings: CHAT HISTORY over the newest messages
    that fit, oldest first and wrapping, a message the room cuts at the top
    fading under the card's body, then the entry field."""
    heading = group("summary-chat-heading", {**absolute(left=0, top=0, width=width, height=HEAD_H), "display": keyword("block")}, [
        flow("summary-chat-title", CHAT_HEADING, "lowpixel", 11, HEAD_H, [1, 1, 1, 0.4], **absolute(left=0, top=0)),
        rule("summary-chat-rule", absolute(left=0, top=HEAD_H - 1, width=width, height=1))])
    messages = [chat_message(doc, slot_index) for slot_index in range(CHAT_SLOTS)]
    # RmlUi clips an overflow: hidden box only once its content overflows
    # toward its trailing or bottom side; the history overflows upward, so an
    # empty strut always reaches past the bottom (as the HUD's chat box).
    strut = group("summary-chat-strut", {**absolute(left=0, top=0, width=1, height=length(200, "%")), "display": keyword("block"),
                                         "pointer-events": keyword("none")})
    log_height = height - HEAD_H - 4 - FIELD_H - 8
    log = group("summary-chat-log", {**absolute(left=0, top=HEAD_H + 4, width=width, height=log_height), "display": keyword("flex"),
                                     "flex-direction": keyword("column-reverse"), "align-items": keyword("flex-start"),
                                     "overflow": keyword("hidden")}, list(reversed(messages)) + [strut])
    fade = vector("summary-chat-fade", absolute(left=0, top=HEAD_H + 4, width=width, height=14), [
        path("f", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             fill=linear((0, 0), (0, {"fraction": 1}), [(0, [0, 0, 0, 0.94]), (1, [0, 0, 0, 0])]))])
    field, control = chat_field(doc, width)
    return group("summary-chat", {**absolute(left=left, top=top, width=width, height=height), "display": keyword("block")},
                 [heading, log, fade, field]), control


def summary_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The SUMMARY page (section 14.14): the result, then the standings
    beside the chat. The chat field is its primary control."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    column_h = page_height - COLUMN_TOP
    stand = standings(doc, 0, COLUMN_TOP, STAND_W, column_h)
    chat_left = STAND_W + COLUMN_GAP
    chat, control = chat_column(doc, chat_left, COLUMN_TOP, page_width - chat_left, column_h)
    return page_group(doc, index, ident, width, height, [result_band(doc, page_width), stand, chat], [control]), control


# ------------------------------------------------------------ the statistics

def match_awards(doc: Document, left: float, width: float) -> list:
    """The selected player's end-of-match awards, each the medal marker and
    its name in Marine in the value color."""
    nodes = []
    for award in range(MATCH_AWARDS):
        key = f"mp.stat.match_award{award}"
        doc.state[key] = {"type": "string", "initial": ""}
        ident = f"stat-match-award-{award}"
        top = AWARD_TOP + award * AWARD_PITCH
        node = group(ident, {**absolute(left=left, top=top, width=width, height=28), "display": keyword("none")}, [
            vector(f"{ident}-plate", absolute(left=0, top=0, width=width, height=28), [
                path("fill", [(0, 0), (width, 0), (width, 28), (10, 28), (0, 18)],
                     fill=linear((0, 0), (width, 0), [(0, rgb(VALUE, 0.18)), (0.5, rgb(VALUE, 0.08)), (1, rgb(VALUE, 0))])),
                path("rail", [(0.75, 0), (0.75, 18), (10, 27.25), (width * 0.6, 27.25)], closed=False,
                     stroke=stroke(linear((0, 0), (width * 0.6, 0), [(0, rgb(VALUE, 0.9)), (1, rgb(VALUE, 0))]), 1.5))]),
            vector(f"{ident}-mark", absolute(left=12, top=10, width=8, height=8), [path("m", [(0, 0), (8, 0), (8, 8)], fill=solid(rgb(VALUE)))]),
            label(f"{ident}-text", PLACEHOLDER, {**absolute(left=28, top=0, width=width - 34, height=28),
                  **typeface("marine", 17, 28, [1, 1, 1, 0.92]), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                  "text-overflow": keyword("ellipsis")}),
        ])
        doc.bind(f"{ident}-text.text", f"{ident}-text", "text", S(key))
        shown(doc, ident, op("!=", S(key), ""))
        nodes.append(node)
    return nodes


def statistics_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The STATISTICS page: the Players page's lists beside the selected
    player's statistics (section 14.18), with the end-of-match awards where
    the Players page has Mute and Friend."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    lists, rows, focus = player_lists(doc, ident, mp.LIST_W, page_height)
    stats_left = mp.LIST_W + 24
    stats, _plates = player_stats(doc, stats_left, page_width - stats_left, page_height, plates=False)
    awards = match_awards(doc, 0, page_width - stats_left)
    stats["children"].extend(awards)
    if AWARD_TOP + MATCH_AWARDS * AWARD_PITCH > page_height:
        raise SystemExit("the match awards leave the STATISTICS page")
    return page_group(doc, index, ident, width, height, [lists, stats], rows), focus


# ------------------------------------------------------------ the scoreboard

def ping_bars(doc: Document, ident: str, ping) -> list:
    """The network-quality bars (section 13.13): four steps by ping."""
    level = sel(op("<=", ping, 60), 4, sel(op("<=", ping, 120), 3, sel(op("<=", ping, 200), 2, 1)))
    nodes = []
    for lit in (1, 2, 3, 4):
        tint = "#B5C784" if lit >= 3 else "#E3AD36" if lit == 2 else "#E46D56"
        paths = []
        for bar in range(4):
            x, h = 2 + bar * 5, 4.5 + bar * 3.3
            paths.append(path(f"b{bar}", [(x, 15 - h), (x + 3.3, 15 - h), (x + 3.3, 15), (x, 15)],
                              fill=solid(rgb(tint, 1 if bar < lit else 0.2))))
        node = vector(f"{ident}-bars{lit}", {**absolute(left=0, top=0, width=BARS_W, height=16), "display": keyword("none")}, paths)
        shown(doc, node["id"], op("==", level, lit))
        nodes.append(node)
    return nodes


def board_row(doc: Document, kind: str, row: int, width: float) -> dict:
    """A row of the SCOREBOARD page, the scoreboard's row (section 14.14):
    your band at 0.29 with the orange marker, the speaker crossed while muted,
    the friend symbol, the name, the clan or a BOT tag, the score, kills in
    flag modes, deaths, minutes, then ping beside the network-quality bars."""
    ident = f"board-{kind}-{row}"
    key = f"mp.players.{kind}{row}"
    for name, kind_name, initial in (("clan", "string", ""), ("kills", "string", ""), ("deaths", "string", ""), ("time", "string", ""),
                                     ("bot", "boolean", False), ("ping_ms", "number", 0)):
        doc.state[f"{key}.{name}"] = {"type": kind_name, "initial": initial}
    whole = [(0, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1})]
    band = vector(f"{ident}-band", {**FULL, "opacity": number(0.08)}, [path("band", whole, fill=solid([1, 1, 1, 1]))])
    doc.bind(f"{ident}-band.opacity", f"{ident}-band", "opacity", sel(S(f"{key}.local"), 0.29, 0.08))
    marker = you_marker(f"{ident}-marker")
    shown(doc, f"{ident}-marker", S(f"{key}.local"))
    speaker = mp.speaker_icon(f"{ident}-speaker")
    shown(doc, f"{ident}-speaker-slash", S(f"{key}.muted"))
    friend = mp.friend_icon(f"{ident}-friend")
    doc.bind(f"{ident}-friend.opacity", f"{ident}-friend", "opacity", sel(S(f"{key}.friend"), 1, 0.125))
    white = [1, 1, 1, 0.85]
    name = flow(f"{ident}-name", PLACEHOLDER, "lowpixel", ROW_FACE, 16, white, grow=True)
    clan = flow(f"{ident}-clan", PLACEHOLDER, "lowpixel", ROW_FACE, 16, [1, 1, 1, 0.6], width=BOARD_CLAN_W, **{"margin-left": length(8)})
    for part in ("name", "clan"):
        doc.bind(f"{ident}-{part}.text", f"{ident}-{part}", "text", S(f"{key}.{part}"))
    bot = S(f"{key}.bot")
    shown(doc, f"{ident}-clan", NOT(bot))
    bot_tag = group(f"{ident}-bot", {"position": keyword("relative"), "display": keyword("none"), "width": length(BOARD_CLAN_W),
                                     "height": length(16), "flex-shrink": number(0), "margin-left": length(8)}, [
        tag(doc, f"{ident}-bot-tag", BOT_TAG, [("c", rgb("#8B964B"), None)], **absolute(left=0, top=0.5))])
    shown(doc, f"{ident}-bot", bot)
    # Spectators have no numbers; their rows keep only the name, clan and ping.
    spectators = kind == "s"
    numbers = []
    for column in () if spectators else ("score", "kills", "deaths", "time"):
        node = flow(f"{ident}-{column}", PLACEHOLDER, "lowpixel", ROW_FACE, 16, white, width=NUMBER_W, align="right")
        doc.bind(f"{ident}-{column}.text", f"{ident}-{column}", "text", S(f"{key}.{column}"))
        if column == "kills":
            shown(doc, f"{ident}-kills", S("mp.players.kills_shown"))
        numbers.append(node)
    meter = group(f"{ident}-meter", {**FULL, "display": keyword("block")}, ping_bars(doc, ident, S(f"{key}.ping_ms")))
    shown(doc, f"{ident}-meter", AND(NOT(bot), op("!=", S(f"{key}.ping"), "")))
    bars = slot(f"{ident}-bars", BARS_W, [meter], **{"margin-left": length(10)})
    ping = flow(f"{ident}-ping", PLACEHOLDER, "lowpixel", ROW_FACE, 16, [1, 1, 1, 0.6], width=BOARD_PING_W, align="right",
                **{"margin-right": length(4)})
    doc.bind(f"{ident}-ping.text", f"{ident}-ping", "text", S(f"{key}.ping"))
    cells = [slot(f"{ident}-mark", MARK_W, [marker]), speaker, friend, name, clan, bot_tag, *numbers, bars, ping]
    content = row_flex(f"{ident}-content", {**absolute(left=2, top=0, right=0, bottom=0), "pointer-events": keyword("none")}, cells)
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(ROW_MAX),
                         "flex-shrink": number(0)}, [band, content])
    shown(doc, ident, op("<", row, S(f"mp.players.{kind}.count")))
    return node


def scoreboard_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The SCOREBOARD page: the headings once, then each list's band in its
    team's color with its title and score over the scoreboard's rows; the
    spectators' band only while there are spectators. Rows keep a readable
    24 dp pitch and scroll below fixed headings, as the Players page's lists do."""
    outer_width = width - 2 * INSET
    page_width = outer_width - mp.ROSTER_GUTTER
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    heading = [1, 1, 1, 0.4]
    cells = [slot("board-head-lead", 2 + MARK_W + 18 + 20),
             flow("board-head-name", NAME_KEY, "lowpixel", 11, HEAD_H, heading, grow=True),
             flow("board-head-clan", CLAN_KEY, "lowpixel", 11, HEAD_H, heading, width=BOARD_CLAN_W, **{"margin-left": length(8)})]
    for column, key in (("score", SCORE_KEY), ("kills", KILLS_KEY), ("deaths", DEATHS_KEY), ("time", TIME_KEY)):
        cells.append(flow(f"board-head-{column}", key, "lowpixel", 11, HEAD_H, heading, width=NUMBER_W, align="right"))
    shown(doc, "board-head-kills", S("mp.players.kills_shown"))
    cells += [slot("board-head-bars", BARS_W, **{"margin-left": length(10)}),
              flow("board-head-ping", PING_KEY, "lowpixel", 11, HEAD_H, heading, width=BOARD_PING_W, align="right",
                   **{"margin-right": length(4)})]
    doc.state["mp.players.kills_shown"] = {"type": "boolean", "initial": False}
    headings = group("board-headings", {"position": keyword("relative"), "display": keyword("block"), "width": length(page_width),
                                        "height": length(HEAD_H), "flex-shrink": number(0)},
                     [row_flex("board-head-row", absolute(left=0, top=0, right=0, height=HEAD_H), cells),
                      rule("board-head-rule", absolute(left=0, top=HEAD_H - 1, width=page_width, height=1))])
    children = []
    for kind, tints in mp.PLAYER_LISTS:
        band = f"board-{kind}-band"
        variants = []
        for tint in tints:
            node = f"{band}-{tint}"
            variants.append(team_band(node, page_width, BAND_H, BAND_TINTS[tint], cut=6))
            shown(doc, node, op("==", S(f"mp.players.{kind}.color"), tint))
        title = label(f"{band}-title", PLACEHOLDER, {**absolute(left=16, top=0, width=page_width * 0.6, height=BAND_H),
                      **typeface("marine", 15, BAND_H, [1, 1, 1, 0.95]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
        score = label(f"{band}-score", PLACEHOLDER, {**absolute(right=4, top=0, width=120, height=BAND_H),
                      **typeface("lowpixel", 16, BAND_H, rgb(VALUE)), "text-align": keyword("right"), "white-space": keyword("nowrap")})
        doc.bind(f"{band}-title.text", f"{band}-title", "text", S(f"mp.players.{kind}.title"))
        doc.bind(f"{band}-score.text", f"{band}-score", "text", S(f"mp.players.{kind}.score"))
        holder = group(band, {"position": keyword("relative"), "display": keyword("none"), "width": length(page_width),
                              "height": length(BAND_H), "flex-shrink": number(0), "margin-top": length(0 if kind == "a" else PANEL_GAP)},
                       [*variants, title, score])
        listed = S(f"mp.players.{kind}.shown") if kind != "s" else op(">", S("mp.players.s.count"), 0)
        shown(doc, band, listed)
        children.append(holder)
        children += [board_row(doc, kind, row, page_width) for row in range(mp.PLAYER_ROWS)]
    lists, bar = mp.roster_frame(doc, "board", outer_width, page_height, headings, children, key="#str_200284")
    return page_group(doc, index, ident, width, height, [lists], [bar]), mp.roster_focus("board", page_height, ident)


# ------------------------------------------------------------- the prompts

def prompt_bars(doc: Document, width: float, height: float) -> list:
    """The prompt bar (section 14.18): Return to game on the back key, Tabs
    and Select; while you type, Send, Channel where team chat exists, and
    Cancel instead."""
    # The card's own prompts answer the mouse as their keys do; while you
    # type, the chat field owns Enter and Escape, so those are legends.
    def prompt(ident: str, keys: list, verb: str, clickable: bool = True) -> list:
        entry = mp.prompt_entry(doc, ident, keys, verb, glyphs=False, clickable=clickable)
        for item in entry:
            item["properties"]["flex-shrink"] = number(0)
        return entry
    box = absolute(left=INSET, top=height - PROMPT_BOTTOM - PROMPT_H, width=width - 2 * INSET, height=PROMPT_H)
    rest = row_flex("prompts", {**box, "display": keyword("flex")},
                    prompt("prompt-back", [KEY_ESCAPE], RETURN_TO_GAME) + prompt("prompt-tabs", [KEY_PREVIOUS, KEY_NEXT], VERB_TABS) +
                    prompt("prompt-select", [KEY_ENTER], VERB_SELECT))
    channel = group("prompt-channel", {"position": keyword("relative"), "display": keyword("none"), "flex-direction": keyword("row"),
                                       "align-items": keyword("center")}, prompt("prompt-channel-k", [KEY_TAB], CHANNEL_WORD, False))
    doc.state["mp.summary.channels"] = {"type": "boolean", "initial": False}
    shown(doc, "prompt-channel", S("mp.summary.channels"), "flex")
    typing = row_flex("prompts-typing", {**box, "display": keyword("none")},
                      prompt("prompt-send", [KEY_ENTER], SEND_WORD, False) + [channel] + prompt("prompt-cancel", [KEY_ESCAPE], CANCEL_WORD, False))
    shown(doc, "prompts", NOT(S("mp.summary.typing")), "flex")
    shown(doc, "prompts-typing", S("mp.summary.typing"), "flex")
    return [rest, typing]


PAGE_BUILDERS = {"summary": summary_page, "statistics": statistics_page, "scoreboard": scoreboard_page}


def check_labels(width: float) -> None:
    """Fail generation when a label leaves its room in any language."""
    over = {}
    for key, face, size, room in ((CHAT_HINT, "lowpixel", 15, width - STAND_W - COLUMN_GAP - 70),
                                  (ACCURACY_KEY, "lowpixel", 11, accuracy_width()), (CLAN_KEY, "lowpixel", 11, CLAN_W),
                                  (SCORE_KEY, "lowpixel", 11, SCORE_W), (CHAT_HEADING, "lowpixel", 11, width - STAND_W - COLUMN_GAP)):
        for language, text in any_text(key).items():
            if b.text_width(face, text, size) > room:
                over[(key, language)] = round(b.text_width(face, text, size))
    if over:
        raise SystemExit(f"summary labels past their room: {over}")


def summary_document() -> dict:
    """The summary card (section 14.14), opened at the end of a match with the
    chat field focused; Esc returns to the game, as the stock summary's."""
    doc = Document("openq4.mp_summary")
    width, height = card_width(), ESCAPE_H
    check_labels(width - 2 * INSET)
    doc.state.update({
        "card.tab": {"type": "number", "initial": 0},
        "card.leaving": {"type": "number", "initial": -1},
        "card.released": {"type": "boolean", "initial": False},
        "reduced_motion": {"type": "boolean", "initial": False, "cvar": "ui_retainedReducedMotion"},
        "mp.title": {"type": "string", "initial": ""},
        "mp.mode": {"type": "string", "initial": ""},
        "mp.clock": {"type": "string", "initial": ""},
        "mp.score": {"type": "string", "initial": ""},
    })
    doc.session("mpClose", "mpClose")
    trailing = [bound_text("header-mode", doc, "mp.mode", "lowpixel", 15, [1, 1, 1, 0.7]),
                bound_text("header-clock", doc, "mp.clock", "lowpixel", 17, rgb(VALUE)),
                bound_text("header-score", doc, "mp.score", "lowpixel", 15, [1, 1, 1, 0.85])]
    pages, focus = [], []
    for index, (ident, _key, _window) in enumerate(SUMMARY_TABS):
        page, primary = PAGE_BUILDERS[ident](doc, index, ident, width, height)
        pages.append(page)
        focus.append([{"op": "focus", "control": primary}] if isinstance(primary, str) else primary)
    card = group("card", {"position": keyword("absolute"), "left": length(50, "%"), "top": length(50, "%"),
                          "width": length(width), "height": length(height), "margin-left": length(-width / 2),
                          "margin-top": length(-height / 2), "opacity": number(0), "transform": transform()},
                 [card_frame(width, height), *card_header(doc, width, "mp.title", trailing), *tab_strip(doc, width, SUMMARY_TABS),
                  *pages, *prompt_bars(doc, width, height)])
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [card])
    doc.bind("chrome.display", "chrome", "display", sel(S("card.released"), "none", "block"))
    root = group("screen", {**FULL, "font-family": b.font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [*backdrop(doc), chrome])
    card_motion(doc, SUMMARY_TABS, focus)
    return fit_card(doc.build(root), width)
