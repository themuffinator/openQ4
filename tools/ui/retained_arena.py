"""The retained Arena sub-page (docs/dev/ui-visual-design.md section 9, "Menu
levels" and "Arena Campaign", with sections 6 and 8; FLOW-011, FLOW-026 and
FLOW-050).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. ARENA on the Single Player page opens it as the
page's second sub-page through the change Campaign takes: the page sweeps out
as its title becomes the crumb and the ARENA label carries to the step's foot,
and at 350 ms this document presents (idSessionLocal::BeginRetainedSubpage).
Back plays its half and Single Player returns with ARENA focused. After the
engine reloads into the campaign's game directory, and back from a match, it
presents at once.

The page is the Arena campaign's browser. idArenaCampaign publishes into it
the state it publishes to the stock guis/arena_menu.gui (PublishGui), its
words already in the player's language, and the page's verbs reach the
campaign's own commands (idSessionLocal::HandleRetainedArenaRequest), so the
campaign keeps every rule: which tiers and matches are open, the entrance,
the authoritative result and the progress.

- **Ladder.** The five tiers form a vertical ladder of plates in the section
  navigation, the matches of the chosen tier a second column beside it: three
  qualifiers, then the boss match on a larger plate with an orange edge while
  it waits. A locked tier or match is dimmed with the lock symbol and cannot
  be chosen; a completed one carries the check and a green edge; the chosen
  one is lit, its label orange over an orange wash. Each plate says LOCKED,
  AVAILABLE, BOSS READY or COMPLETE under its label. A plate's first Accept
  chooses it; Accept on the chosen tier moves to its chosen match, and on the
  chosen match starts it. The bumpers (and Q and E) switch tier.
- **Briefing and detail.** Under the matches the chosen tier's briefing: its
  name, description and path to the next tier. The detail column shows the
  chosen match's levelshot with its best result across the foot, name,
  description, map, game type, roster, limits and skill, the difficulty
  between its - and + steps, and START MATCH, the
  enlarged primary plate, live only while the campaign can start that match.
- **Progress.** Under the ladder, CAMPAIGN PROGRESS with its count and bar,
  CAMPAIGN COMPLETE once the last boss falls, and RESET PROGRESS, which asks
  with the stock modal first.
- **Entrance.** Starting a match plays the stock entrance on the page: the
  screen darkens to 0.72 over 320 ms as the card with ENTERING THE ARENA, the
  match, its game type and map and PREPARE FOR COMBAT scales in, and from
  760 ms everything goes to black for the hand-over at 1250 ms. While the
  campaign holds the entrance every control is out.
- **Result.** Back from a match the page reveals the stock ledger as a modal
  from the black the match ended on: MATCH REVIEW, the result over its
  green, red or amber accent, the match, then the message and score, NEW
  BEST where a replay beat the record (610 ms), then what it unlocked and
  the awards, or CAMPAIGN COMPLETE
  (930 ms), and at 1260 ms the actions: RETRY or REPLAY where the match can be
  played again, and NEXT, RETURN or, after an aborted match that cannot be,
  CONTINUE.
"""
from __future__ import annotations

from build_retained_screens import fit_narrow
from build_retained_screens import (ACCEL, BOTTOM_TO_PAGE, CANVAS_W, CRUMB_SCALE, CRUMB_SLOT, CRUMB_SPAN, EASE_OUT, FULL,
                                    MARKER, OLIVE, ORANGE, PAGE_TITLE_H, PAGE_TITLE_W, RAIL_DARK, SUBPAGE_SLOT, TITLE_SLOT,
                                    TOP_TO_PAGE, TOP_TO_SUBPAGE, U, VALUE, Document, absolute, action_plate, by_language,
                                    colour, confirmation, crumb_fits, crumb_transform, font, framing_bands, group, keyword,
                                    label, length, linear, link, lit_field, marker_path, number, page_title, path, rgb, solid,
                                    stroke, text_width, track, transform, typeface, vector, LANGUAGES)
from retained_difficulty import check_fits, primary_plate
from retained_saves import PLACEHOLDER, backing, picture_frame, table_text

TIERS, MATCHES, BOSS = 5, 4, 3
TIER_KEYS = ("#str_42091", "#str_42092", "#str_42093", "#str_42094", "#str_42095")   # TIER 1-5
MATCH_KEYS = ("#str_42088", "#str_42089", "#str_42090", "#str_42024")                # QUALIFIER 1-3, BOSS MATCH
DONE = "#A3EB61"                   # a completed tier or match: the stock check (0.64, 0.92, 0.38)
LOCK = [0.78, 0.8, 0.7, 0.85]      # the stock lock
WIN, LOSS, DRAW = "#80CC47", "#D13312", "#EB9E1F"   # the result accents (arena_result_*_accent)

# The page on the 4:3 canvas, in source units.
HEAD_TOP = 58.0                                 # the column headings
LADDER = (16.0, 72.0, 170.0, 40.0, 44.0)        # the tiers: left, top, width, height, pitch
COLUMN = (196.0, 72.0, 202.0, 36.0, 40.0)       # QUALIFIER 1-3
BOSS_TOP, BOSS_H = 194.0, 50.0                  # the boss match's larger plate
BRIEFING_TOP = 254.0
PROGRESS_TOP = 298.0
DETAIL_X, DETAIL_W = 404.0, 226.0
SHOT_TOP, SHOT_H = 58.0, 112.0
TITLE_TOP = 176.0
INFO = (("map", "#str_42013", "arena_selectedMap", 236.0, 1), ("gametype", "#str_42014", "arena_selectedGameType", 249.0, 1),
        ("roster", "#str_42015", "arena_selectedRoster", 262.0, 2), ("limits", "#str_42016", "arena_selectedLimits", 288.0, 1),
        ("skill", "#str_42035", "arena_selectedSkill", 301.0, 1))
INFO_VALUE_X = 76.0                             # the values, from the column's edge
DIFFICULTY_TOP = 317.0                          # DIFFICULTY, its steps 13 u under it
START_TOP = 354.0
TIER_TEXT, MATCH_TEXT, BOSS_TEXT, STATUS_TEXT = 20.0, 18.0, 22.0, 13.0   # dp
TITLE_TEXT = 17.0                               # dp: the tier's and the match's names
BODY_TEXT, BODY_LINE = 14.0, 18.0               # dp: descriptions
BRIEFING_TEXT = 13.0                            # dp: the tier's, in the narrower column
HEAD = [1, 1, 1, 0.4]                           # headings and row labels
RESET_TEXT = 15.0                               # dp: RESET PROGRESS

# The result ledger: the stock report's 396 u width (594 dp), the modal grown
# to 250 u with its centre where the stock modal's is; its actions are wide
# enough for every language's RETRY.
RESULT_TOP, RESULT_HEIGHT = 98.0, 250.0
RESULT_WIDTH = U * 396
RESULT_ACTION_W, RESULT_ACTION_TEXT = 260.0, 18.0
RESET_ACTION_W, RESET_ACTION_TEXT = 205.0, 18.0
RESET_W, RESET_TOP, RESET_HEIGHT = 480.0, 148.0, 150.0   # the stock modal grown 14 u, its centre held
BEST_H = 23.0                      # u: the best-result band at the levelshot's foot, its label over the record
NEW_BEST_W = 156.0                 # dp: the stock tag's 104 u


def plate_shape(w: float, h: float) -> tuple:
    """The plate's band (2 u in from its top and foot) and its 9 u
    lower-leading cut."""
    return U * 2, h - U * 2, U * 9


def fade(tint: list[float]) -> list:
    """A section plate's fill and rail falloff (b2_dark, Appendix B.1): full to
    57 % of the width, half at 72 % and gone by 94 %."""
    return [(0, tint), (0.57, tint), (0.72, tint[:3] + [tint[3] * 0.5]), (0.94, tint[:3] + [0]), (1, tint[:3] + [0])]


def plate_paths(w: float, h: float) -> list:
    top, bottom, cut = plate_shape(w, h)
    return [path("fill", [(0, top), (w, top), (w, bottom), (cut, bottom), (0, bottom - cut)],
                 fill=linear((0, 0), (w, 0), fade([0, 0, 0, 1]))),
            path("rail", [(0.75, top), (0.75, bottom - cut), (cut, bottom - 0.75), (w, bottom - 0.75)], closed=False,
                 stroke=stroke(linear((0, 0), (w, 0), fade(rgb(RAIL_DARK))), 1.5))]


def padlock(ident: str, left: float, top: float, tint: list) -> dict:
    """The lock symbol: a shackle over a body, 12x14 dp."""
    return vector(ident, {**absolute(left=left, top=top, width=12, height=14), "display": keyword("none"),
                          "pointer-events": keyword("none")}, [
        path("shackle", [(3, 7), (3, 3.5), (4.5, 1.5), (7.5, 1.5), (9, 3.5), (9, 7)], closed=False, stroke=stroke(solid(tint), 1.5)),
        path("body", [(1, 6.5), (11, 6.5), (11, 13.5), (1, 13.5)], fill=solid(tint)),
    ])


def check(ident: str, left: float, top: float, tint: list) -> dict:
    """The completed check, 12x11 dp."""
    return vector(ident, {**absolute(left=left, top=top, width=12, height=11), "display": keyword("none"),
                          "pointer-events": keyword("none")}, [
        path("check", [(0, 6), (3.6, 9.9), (10.5, 1.8)], closed=False, stroke=stroke(solid(tint), 2.1, join="round", cap="round"))])


def shown(condition) -> dict:
    return {"op": "select", "args": [condition, "block", "none"]}


def all_of(*conditions) -> dict:
    """The conjunction of `conditions` (the expressions' && is binary)."""
    result = conditions[-1]
    for condition in reversed(conditions[:-1]):
        result = {"op": "&&", "args": [condition, result]}
    return result


def any_of(*conditions) -> dict:
    result = conditions[-1]
    for condition in reversed(conditions[:-1]):
        result = {"op": "||", "args": [condition, result]}
    return result


def arena_plate(doc: Document, ident: str, key: str, left: float, top: float, width: float, height: float, prefix: str,
                event: str, *, size: float, boss: bool = False) -> dict:
    """A tier's or a match's plate in the ladder: the section plate's dark band
    with its #CCCC51 rail, corner marker and Marine label, the published
    status under the label, the lock or the check at its trailing end, and
    the published choice lit over an orange wash. `prefix` names the plate's
    published state (`prefix`_status, _locked, _selected and _completed). The
    boss's larger plate runs its label the full width and carries the lock
    or the check at the end of its status line."""
    w, h = U * width, U * height
    top_y, bottom_y, cut = plate_shape(w, h)
    locked, selected, completed = ({"state": f"{prefix}_{name}"} for name in ("locked", "selected", "completed"))
    whole = absolute(left=0, top=0, width=w, height=h)
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.4)}, plate_paths(w, h))
    lit = vector(f"{ident}-plate-lit", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, plate_paths(w, h))
    band = [(0, top_y), (w, top_y), (w, bottom_y), (cut, bottom_y), (0, bottom_y - cut)]
    wash = vector(f"{ident}-wash", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
        path("wash", band, fill=linear((0, 0), (w, 0), fade(rgb(ORANGE, 0.23))))])
    edge = [(0, top_y), (U * 2.5, top_y), (U * 2.5, bottom_y - cut), (0, bottom_y - cut)]
    done = vector(f"{ident}-done", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
        path("edge", edge, fill=solid(rgb(DONE, 0.95)))])
    parts = [plate, lit, wash, done]
    doc.bind(f"{ident}-plate-lit.display", f"{ident}-plate-lit", "display", shown(selected))
    doc.bind(f"{ident}-wash.display", f"{ident}-wash", "display", shown(selected))
    doc.bind(f"{ident}-done.display", f"{ident}-done", "display", shown(completed))
    if boss:
        # The boss waits behind an orange edge once the qualifiers open it.
        ready = vector(f"{ident}-ready", {**whole, "display": keyword("none"), "pointer-events": keyword("none")}, [
            path("edge", edge, fill=solid(rgb(ORANGE, 0.95)))])
        parts.append(ready)
        doc.bind(f"{ident}-ready.display", f"{ident}-ready", "display",
                 shown({"op": "&&", "args": [{"op": "!", "args": [locked]}, {"op": "!", "args": [completed]}]}))
    focus = vector(f"{ident}-focus", {**whole, "opacity": number(0)}, [
        path("inset", [(cut + 2, bottom_y - 3.2), (round(w * 0.72, 3), bottom_y - 3.2)], closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    mark = (U * 7, top_y + U * 4, U * (6 if boss else 5))
    marker_rest = vector(f"{ident}-marker", {**whole, "opacity": number(0.4)}, [marker_path("mark", *mark, rgb(MARKER))])
    marker_hot = vector(f"{ident}-marker-hot", {**whole, "opacity": number(0)}, [marker_path("mark", *mark, rgb(ORANGE))])
    text_box = absolute(left=U * 16, top=top_y + U * (4 if boss else 3), width=w - U * (22 if boss else 40), height=round(size * 1.1, 3))
    text_node = label(f"{ident}-label", key, {**text_box, **typeface("marine", size, round(size * 1.1, 3), [1, 1, 1, 0.8]),
                                              "white-space": keyword("nowrap"), "transform": transform(), "display": keyword("block")})
    text_lit = label(f"{ident}-label-lit", key, {**text_box, **typeface("marine", size, round(size * 1.1, 3), rgb(ORANGE)),
                                                 "white-space": keyword("nowrap"), "transform": transform(), "display": keyword("none")})
    doc.bind(f"{ident}-label.display", f"{ident}-label", "display", {"op": "select", "args": [selected, "none", "block"]})
    doc.bind(f"{ident}-label-lit.display", f"{ident}-label-lit", "display", shown(selected))
    status_top = bottom_y - U * (14 if boss else 13)
    status = label(f"{ident}-status", PLACEHOLDER, {**absolute(left=U * 16, top=status_top, width=w - U * 40, height=U * 10),
                   **typeface("lowpixel", STATUS_TEXT, U * 10, [1, 1, 1, 0.55]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-status.text", f"{ident}-status", "text", {"state": f"{prefix}_status"})
    middle = status_top + U * 5 if boss else h / 2
    lock = padlock(f"{ident}-lock", w - U * 20, round(middle - 7, 3), LOCK)
    tick = check(f"{ident}-check", w - U * 20, round(middle - 5.5, 3), rgb(DONE))
    doc.bind(f"{ident}-lock.display", f"{ident}-lock", "display", shown(locked))
    doc.bind(f"{ident}-check.display", f"{ident}-check", "display",
             shown({"op": "&&", "args": [completed, {"op": "!", "args": [locked]}]}))
    parts += [focus, marker_rest, marker_hot, text_node, text_lit, status, lock, tick]

    def state(plate_alpha, marker, hot, inset, tint, moved, status_alpha):
        return [(f"{ident}-plate", "opacity", number(plate_alpha)), (f"{ident}-marker", "opacity", number(marker)),
                (f"{ident}-marker-hot", "opacity", number(hot)), (f"{ident}-focus", "opacity", number(inset)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved),
                (f"{ident}-label-lit", "transform", moved), (f"{ident}-status", "color", colour([1, 1, 1, status_alpha]))]
    ids = doc.states(ident, {
        "default": state(0.4, 0.4, 0, 0, [1, 1, 1, 0.8], transform(), 0.55),
        "hover": state(0.8, 0, 1, 0, rgb(ORANGE), transform(sx=1.03, sy=1.03), 0.75),
        "focus": state(0.8, 0, 1, 1, rgb(ORANGE), transform(sx=1.03, sy=1.03), 0.75),
        "pressed": state(1, 0, 1, 1, rgb(ORANGE), transform(tx=1, ty=1, sx=1.03, sy=1.03), 0.75),
        "disabled": state(0.25, 0.25, 0, 0, [1, 1, 1, 0.3], transform(), 0.3),
    })
    doc.bind(f"{ident}.enabled", ident, "enabled", {"op": "&&", "args": [{"op": "!", "args": [locked]}, OPEN]})
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=h), parts,
                 control={"role": "button", "label": key, "event": event, "states": ids})


# Every control is out while the campaign holds its entrance or return.
OPEN = {"op": "!", "args": [{"state": "arena_transitioning"}]}


def stepper(doc: Document, ident: str, key: str, left: float, top: float, action: str, live: dict) -> dict:
    """A difficulty step: a 20x18 u light plate with a 3 u lower-leading cut
    and its rail, the Marine - or + on it; the pointer and focus light it and
    turn its sign orange, as the action plates do."""
    w, h, cut = U * 20, U * 18, U * 3
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=w, height=h), "opacity": number(0.4)}, [
        path("fill", [(0, 0), (w, 0), (w, h), (cut, h), (0, h - cut)], fill=solid(rgb(OLIVE, 0.49))),
        path("rail", [(0.75, 0), (0.75, h - cut), (cut, h - 0.75), (w, h - 0.75)], closed=False, stroke=stroke(solid(rgb(OLIVE)), 1.5))])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=w, height=h), "opacity": number(0)}, [
        path("inset", [(3.5, 3), (3.5, h - cut + 1.5), (cut + 1.5, h - 3.5), (w - 3, h - 3.5)], closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.2))])
    sign = label(f"{ident}-label", key, {**absolute(left=0, top=0, width=w, height=h), **typeface("marine", 22, h, [1, 1, 1, 0.8]),
                                         "text-align": keyword("center"), "white-space": keyword("nowrap")})
    def state(alpha, inset, tint):
        return [(f"{ident}-plate", "opacity", number(alpha)), (f"{ident}-focus", "opacity", number(inset)), (f"{ident}-label", "color", colour(tint))]
    ids = doc.states(ident, {"default": state(0.4, 0, [1, 1, 1, 0.8]), "hover": state(0.8, 0, rgb(ORANGE)), "focus": state(0.8, 1, rgb(ORANGE)),
                             "pressed": state(1, 1, rgb(ORANGE)), "disabled": state(0.2, 0, [1, 1, 1, 0.25])})
    doc.bind(f"{ident}.enabled", ident, "enabled", {"op": "&&", "args": [live, OPEN]})
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=h), [plate, focus, sign],
                 control={"role": "button", "label": key, "action": action, "states": ids})


def text_line(ident: str, key: str, left: float, top: float, width: float, height: float, face: str, size: float,
              tint: list, *, state: str | None = None, doc: Document | None = None, lines: int = 1, align: str = "left",
              line: float | None = None) -> dict:
    """A label in source units; with `state`, bound to the published text."""
    props = {**absolute(left=U * left, top=U * top, width=U * width, height=U * height),
             **typeface(face, size, line or U * height / lines, tint), "overflow": keyword("hidden"), "pointer-events": keyword("none")}
    props["white-space"] = keyword("nowrap" if lines == 1 else "normal")
    if align != "left":
        props["text-align"] = keyword(align)
    node = label(ident, PLACEHOLDER if state else key, props)
    if state:
        doc.bind(f"{ident}.text", ident, "text", {"state": state})
    return node


def published_states(doc: Document) -> None:
    """The campaign's published state the page reads (idArenaCampaign::
    PublishGui): its words come already in the player's language."""
    strings = ["arena_selectedTierName", "arena_selectedTierDescription", "arena_selectedTitle", "arena_selectedDescription",
               "arena_selectedMap", "arena_selectedGameType", "arena_selectedRoster", "arena_selectedLimits", "arena_selectedSkill",
               "arena_levelshot", "arena_difficultyName", "arena_progress", "arena_resultTitle", "arena_resultMatchName",
               "arena_resultMessage", "arena_resultScore", "arena_resultUnlock", "arena_resultAwardLine", "arena_selectedBest"]
    flags = ["arena_loaded", "arena_transitioning", "arena_canStart", "arena_campaignComplete", "arena_resultHasScore",
             "arena_resultHasUnlock", "arena_resultHasAwards", "arena_resultCanRetry", "arena_resultUnlockedCampaign",
             "arena_selectedBestVisible", "arena_selectedHasBest", "arena_resultNewBest", "arena_canReset"]
    numbers = ["arena_difficulty", "arenaCampaignProgress", "arena_result"]
    for kind, count in (("tier", TIERS), ("match", MATCHES)):
        for index in range(count):
            strings.append(f"arena_{kind}{index}_status")
            flags += [f"arena_{kind}{index}_{name}" for name in ("locked", "selected", "completed")]
    for name in strings:
        doc.state[name] = {"type": "string", "initial": ""}
    for name in flags:
        doc.state[name] = {"type": "boolean", "initial": False}
    for name in numbers:
        doc.state[name] = {"type": "number", "initial": 0}


def focus_chosen(kind: str, count: int) -> list:
    """Steps that focus the plate holding the published choice; the last
    request wins."""
    steps = [{"op": "focus", "control": f"{kind}-0"}]
    for index in range(1, count):
        steps.append({"op": "if", "condition": {"state": f"arena_{kind}{index}_selected"},
                      "then": [{"op": "focus", "control": f"{kind}-{index}"}]})
    return steps


def focus_page() -> list:
    """The page's focus: the chosen match, or BACK while no campaign loaded."""
    return [{"op": "focus", "control": "arena_back"},
            {"op": "if", "condition": {"state": "arena_loaded"}, "then": focus_chosen("match", MATCHES)}]


def ladder(doc: Document) -> list:
    """The tiers and the chosen tier's matches."""
    left, top, width, height, pitch = LADDER
    nodes = [text_line("arena-tiers-head", "#str_42007", left + 4, HEAD_TOP, width - 4, 12, "lowpixel", 13, HEAD)]
    for index, key in enumerate(TIER_KEYS):
        event = f"tierPick{index}"
        doc.session(f"tier{index}", f"arenaTier{index}")
        # A tier's first Accept chooses it; Accept on the chosen tier moves to
        # its chosen match.
        doc.events[event] = [{"op": "if", "condition": {"state": f"arena_tier{index}_selected"},
                              "then": focus_chosen("match", MATCHES), "else": [{"op": "action", "action": f"tier{index}"}]}]
        nodes.append(arena_plate(doc, f"tier-{index}", key, left, top + pitch * index, width, height, f"arena_tier{index}", event,
                                 size=TIER_TEXT))
    left, top, width, height, pitch = COLUMN
    nodes.append(text_line("arena-matches-head", "#str_42008", left + 4, HEAD_TOP, width - 4, 12, "lowpixel", 13, HEAD))
    doc.session("start", "arenaStart")
    for index, key in enumerate(MATCH_KEYS):
        event = f"matchPick{index}"
        doc.session(f"match{index}", f"arenaMatch{index}")
        # A match's first Accept chooses it; Accept on the chosen match starts it.
        doc.events[event] = [{"op": "if", "condition": {"state": f"arena_match{index}_selected"},
                              "then": [{"op": "if", "condition": {"state": "arena_canStart"}, "then": [{"op": "action", "action": "start"}]}],
                              "else": [{"op": "action", "action": f"match{index}"}]}]
        boss = index == BOSS
        plate = arena_plate(doc, f"match-{index}", key, left, BOSS_TOP if boss else top + pitch * index, width,
                            BOSS_H if boss else height, f"arena_match{index}", event, size=BOSS_TEXT if boss else MATCH_TEXT, boss=boss)
        # Down runs on through the matches to the difficulty's - step, past a
        # locked one, as the landscape geometry does; a tall view stands the
        # step off the matches' line, under them (tall_arena_page).
        plate["control"]["navigation"] = {"down": "arena_easier" if boss else f"match-{index + 1}"}
        nodes.append(plate)
    return nodes


def briefing(doc: Document) -> list:
    """The chosen tier's briefing under its matches: the stock heading, the
    tier's name and description, the path to the next tier and the rule."""
    left, width = COLUMN[0] + 4, COLUMN[2] - 4
    return [
        text_line("arena-briefing-head", "#str_42070", left, BRIEFING_TOP, width, 12, "lowpixel", 13, HEAD),
        text_line("arena-tier-name", "", left, BRIEFING_TOP + 12, width, 15, "marine", TITLE_TEXT, [1, 1, 1, 0.9],
                  state="arena_selectedTierName", doc=doc),
        text_line("arena-tier-description", "", left, BRIEFING_TOP + 29, width, 36, "lowpixel", BRIEFING_TEXT, [1, 1, 1, 0.7],
                  state="arena_selectedTierDescription", doc=doc, lines=3, line=BODY_LINE),
        text_line("arena-tier-path", "#str_42071", left, BRIEFING_TOP + 68, width, 22, "lowpixel", 13, rgb(ORANGE), lines=2, line=U * 11),
        text_line("arena-tier-rule", "#str_42049", left, BRIEFING_TOP + 92, width, 36, "lowpixel", 13, [1, 1, 1, 0.5],
                  lines=3, line=U * 12),
    ]


def progress(doc: Document) -> list:
    """CAMPAIGN PROGRESS under the ladder: the published line, the bar filling
    by a twentieth a match, CAMPAIGN COMPLETE once the last boss falls, and
    RESET PROGRESS, which asks first. A lost first match leaves a best result
    but no win, so the campaign says when there is anything to reset
    (arena_canReset)."""
    left, width = LADDER[0] + 4, LADDER[2] - 4
    bar_w = U * width
    track_node = vector("arena-progress-track", absolute(left=U * left, top=U * (PROGRESS_TOP + 15), width=bar_w, height=U * 3), [
        path("track", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid([1, 1, 1, 0.12]))])
    fill = group("arena-progress-fill", {**absolute(left=U * left, top=U * (PROGRESS_TOP + 15), width=0, height=U * 3),
                                         "background-color": colour(rgb(VALUE, 0.9))})
    doc.bind("arena-progress-fill.width", "arena-progress-fill", "width",
             {"op": "*", "args": [{"op": "clamp", "args": [{"state": "arenaCampaignProgress"}, 0, 20]}, round(bar_w / 20, 4)]})
    complete = text_line("arena-complete", "#str_42026", left, PROGRESS_TOP + 22, width, 12, "lowpixel", 13, rgb(DONE))
    complete["properties"]["display"] = keyword("none")
    doc.bind("arena-complete.display", "arena-complete", "display", shown({"state": "arena_campaignComplete"}))
    doc.session("reset", "arenaReset")
    reset = link(doc, "arena_reset", "#str_42011", action="reset", size=RESET_TEXT)
    doc.bind("arena_reset.enabled", "arena_reset", "enabled",
             {"op": "&&", "args": [{"state": "arena_canReset"}, OPEN]})
    reset_row = group("arena-reset-row", {**absolute(left=U * left, top=U * (PROGRESS_TOP + 40), width=U * width, height=U * 17)}, [reset])
    return [text_line("arena-progress", "", left, PROGRESS_TOP, width, 13, "lowpixel", 15, rgb(VALUE), state="arena_progress", doc=doc),
            track_node, fill, complete, reset_row]


def detail(doc: Document) -> list:
    """The chosen match: its levelshot, name, description and rules, the
    difficulty and START MATCH."""
    x, width = DETAIL_X, DETAIL_W
    shot = picture_frame("arena-shot", x, SHOT_TOP, width, SHOT_H)
    # The levelshot fills its frame, as the stock browser's stretched one did.
    shot[1]["properties"]["image-fit"] = keyword("cover")
    doc.bind("arena-shot.image", "arena-shot", "image", {"state": "arena_levelshot"})
    # The chosen match's best result across the foot of its levelshot, inside
    # the border, as the stock browser draws it: in amber where there is a
    # record, dim while it reads No result yet; a locked match has none. The
    # narrower column puts the label over the record.
    band = vector("arena-best-band", absolute(left=0, top=0, width=U * (width - 2), height=U * BEST_H), [
        path("band", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             fill=solid([0.015, 0.018, 0.015, 0.84])),
        path("rule", [(0, 0.5), ({"fraction": 1}, 0.5)], closed=False, stroke=stroke(solid([0.55, 0.61, 0.34, 0.7]), 1))])
    recorded = text_line("arena-best-value", "", 6, 10.5, width - 14, 12, "lowpixel", 14, [0.95, 0.72, 0.25, 1],
                         state="arena_selectedBest", doc=doc)
    unplayed = text_line("arena-best-none", "", 6, 10.5, width - 14, 12, "lowpixel", 14, [0.61, 0.67, 0.5, 1],
                         state="arena_selectedBest", doc=doc)
    for line_node in (recorded, unplayed):
        line_node["properties"]["display"] = keyword("none")    # the bindings below choose one
    best = group("arena-best", {**absolute(left=U * (x + 1), top=U * (SHOT_TOP + SHOT_H - 1 - BEST_H), width=U * (width - 2),
                                           height=U * BEST_H), "display": keyword("none"), "pointer-events": keyword("none")}, [
        band, text_line("arena-best-label", "#str_42040", 6, 2, width - 14, 9, "lowpixel", 13, [0.57, 0.63, 0.45, 1]),
        recorded, unplayed])
    doc.bind("arena-best.display", "arena-best", "display", shown({"state": "arena_selectedBestVisible"}))
    doc.bind("arena-best-value.display", "arena-best-value", "display", shown({"state": "arena_selectedHasBest"}))
    doc.bind("arena-best-none.display", "arena-best-none", "display", shown({"op": "!", "args": [{"state": "arena_selectedHasBest"}]}))
    rule = vector("arena-title-rule", absolute(left=U * x, top=U * (TITLE_TOP + 16), width=U * width, height=1.5), [
        path("rule", [(0, 0.75), ({"fraction": 1}, 0.75)], closed=False, stroke=stroke(linear((0, 0), ({"fraction": 1}, 0), [
            (0, rgb(ORANGE, 0.9)), (0.7, rgb(ORANGE, 0.6)), (1, rgb(ORANGE, 0))]), 1.5))])
    nodes = [shot[0], shot[1], best, shot[2],
             text_line("arena-match-title", "", x, TITLE_TOP, width, 14, "marine", TITLE_TEXT, [0.98, 0.94, 0.8, 1], state="arena_selectedTitle", doc=doc),
             rule,
             text_line("arena-match-description", "", x, TITLE_TOP + 20, width, 36, "lowpixel", BODY_TEXT, [1, 1, 1, 0.7],
                       state="arena_selectedDescription", doc=doc, lines=3, line=BODY_LINE)]
    for ident, key, state, top, lines in INFO:
        nodes.append(text_line(f"arena-{ident}-label", key, x, top + 1, INFO_VALUE_X - 4, 11, "lowpixel", 13, HEAD))
        nodes.append(text_line(f"arena-{ident}", "", x + INFO_VALUE_X, top, width - INFO_VALUE_X, 12 * lines, "lowpixel", BODY_TEXT,
                               [1, 1, 1, 0.85], state=state, doc=doc, lines=lines, line=U * 12))
    # DIFFICULTY between its steps, as the stock spinner.
    doc.session("easier", "arenaEasier")
    doc.session("harder", "arenaHarder")
    steps_top = DIFFICULTY_TOP + 13
    nodes += [
        text_line("arena-difficulty-label", "#str_42009", x, DIFFICULTY_TOP, width, 11, "lowpixel", 13, HEAD),
        stepper(doc, "arena_easier", "#str_42020", x, steps_top, "easier", {"op": ">", "args": [{"state": "arena_difficulty"}, 1]}),
        text_line("arena-difficulty", "", x + 22, steps_top + 2, width - 44, 14, "lowpixel", 16, rgb(VALUE), state="arena_difficultyName",
                  doc=doc, align="center"),
        stepper(doc, "arena_harder", "#str_42021", x + width - 20, steps_top, "harder", {"op": "<", "args": [{"state": "arena_difficulty"}, 5]}),
    ]
    start = primary_plate(doc, "arena_start", "#str_42010", x, START_TOP, width, 43.0, action="start", label_box=(28.0, 6.0, 194.0, 25.0))
    start["control"]["navigation"] = {"up": "arena_easier"}
    doc.bind("arena_start.enabled", "arena_start", "enabled", {"op": "&&", "args": [{"state": "arena_canStart"}, OPEN]})
    nodes.append(start)
    return nodes


LAUNCH_MOST = 1.25      # the entrance card at the largest the 4:3 canvas holds


def grow_launch(doc: Document, card: dict) -> None:
    """The entrance card above 100 % text: a card of lines over a picture,
    shown for a moment, so it grows as one, its lines, letters and art, by
    the text size up to the largest the 4:3 canvas holds, about its middle.
    A tall view scales its stage instead (tall_arena_page), so there it
    keeps its size. Every binding gives the authored value at 100 %
    (check_text_bindings)."""
    from build_retained_screens import TALL, TEXT, TEXT_LENGTHS, in_fractions, text_source, text_sum, walk_nodes
    text_source(doc)
    grown = {"op": "select", "args": [TALL, 1, {"op": "min", "args": [TEXT, LAUNCH_MOST]}]}
    width, height = card["properties"]["width"]["value"], card["properties"]["height"]["value"]
    # The page's narrow-view fit places the card by its offset from the
    # canvas's middle (fit_narrow), as it places the page's other parts.
    for key, place, extent in (("left", card["properties"]["left"]["value"], width),
                               ("margin-top", card["properties"]["top"]["value"] - 360, height)):
        doc.bind(f"text.launch-card.{key}", "launch-card", key,
                 text_sum(place + extent / 2, {"op": "*", "args": [-extent / 2, grown]}))
    for key, extent in (("width", width), ("height", height)):
        doc.bind(f"text.launch-card.{key}", "launch-card", key, {"op": "*", "args": [extent, grown]})
    for node in walk_nodes(card):
        if node is card:
            continue
        props = node.get("properties", {})
        if node.get("type") == "vector" and props.get("width", {}).get("unit") == "dp":
            in_fractions(node, props["width"]["value"], props["height"]["value"])
        for key, found in props.items():
            if not isinstance(found, dict) or found.get("type") != "length" or found.get("unit") != "dp" or not found["value"]:
                continue
            # A font length grows by the card's own scale, not the text size.
            value = {"op": "*", "args": [{"op": "/", "args": [found["value"], TEXT]} if key in TEXT_LENGTHS else found["value"], grown]}
            doc.bind(f"text.{node['id']}.{key}", node["id"], key, value)


def launch_layer(doc: Document) -> dict:
    """The entrance (the stock arena_launch_timeline): the screen darkens to
    0.72 over 320 ms as the card scales in from 94 % over 480 ms; from 760 ms
    the card fades over 380 ms and the screen goes to black by 1260 ms, past
    the campaign's hand-over at 1250 ms. The card is the stock 496x220 u
    panel as a section 6 card (12 and 3 dp cuts) in the stock panel's orange
    rail: the match's levelshot dimmed under a 0.68 shade, ENTERING THE
    ARENA, the match in Marine, an orange rule, its game type and map, and
    PREPARE FOR COMBAT."""
    doc.state["arena.launch"] = {"type": "boolean", "initial": False}
    width, height = U * 496, U * 220
    major, minor = 12.0, 3.0
    card = [(minor, 0), (width - major, 0), (width, major), (width, height - minor), (width - minor, height), (major, height),
            (0, height - major), (0, minor)]
    inner = absolute(left=major, top=major, width=width - 2 * major, height=height - 2 * major)
    shot = {"id": "launch-shot", "type": "image", "properties": {
        "image": {"type": "image", "value": ""}, "image-fit": keyword("cover"), **inner,
        "image-color": colour([0.3, 0.34, 0.24, 0.52])}}
    doc.bind("launch-shot.image", "launch-shot", "image", {"state": "arena_levelshot"})
    def centred(ident, key, top, h, face, size, tint, state=None):
        props = {**absolute(left=U * 24, top=U * top, width=U * 448, height=U * h), **typeface(face, size, U * h, tint),
                 "text-align": keyword("center"), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
        node = label(ident, PLACEHOLDER if state else key, props)
        if state:
            doc.bind(f"{ident}.text", ident, "text", {"state": state})
        return node
    card = group("launch-card", {**absolute(left=U * 72, top=U * 120, width=width, height=height), "opacity": number(0), "transform": transform()}, [
        vector("launch-card-back", absolute(left=0, top=0, width=width, height=height), [path("back", card, fill=solid([0.018, 0.024, 0.017, 0.98]))]),
        shot,
        vector("launch-card-shade", absolute(left=0, top=0, width=width, height=height), [
            path("shade", [(major, major), (width - major, major), (width - major, height - major), (major, height - major)],
                 fill=solid([0.01, 0.015, 0.01, 0.68])),
            path("rail", card, stroke=stroke(solid(rgb(ORANGE, 0.9)), 1.5))]),
        centred("launch-kicker", "#str_42079", 24, 14, "lowpixel", 18, [0.95, 0.72, 0.25, 1]),
        centred("launch-title", "", 54, 30, "marine", 32, [1, 0.96, 0.82, 1], state="arena_selectedTitle"),
        vector("launch-rule", absolute(left=U * 72, top=U * 111, width=U * 350, height=2), [
            path("rule", [(0, 1), ({"fraction": 1}, 1)], closed=False, stroke=stroke(solid(rgb(ORANGE, 0.9)), 2))]),
        centred("launch-gametype", "", 127, 15, "lowpixel", 18, [0.86, 0.9, 0.76, 1], state="arena_selectedGameType"),
        centred("launch-map", "", 153, 13, "lowpixel", 15, [0.63, 0.69, 0.52, 1], state="arena_selectedMap"),
        centred("launch-ready", "#str_42086", 188, 13, "lowpixel", 15, rgb(ORANGE)),
    ])
    grow_launch(doc, card)
    stage = group("launch-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}, [card])
    layer = group("launch", {**FULL, "display": keyword("none"), "pointer-events": keyword("none")}, [
        group("launch-shade", {**FULL, "background-color": colour([0, 0, 0, 1]), "opacity": number(0)}), stage])
    doc.bind("launch.display", "launch", "display", shown({"state": "arena.launch"}))
    doc.timelines.add("launch", 1260, [
        track("launch-shade", "opacity", [(0, number(0)), (1, number(0)), (320, number(0.72)), (760, number(0.72)), (1260, number(1))]),
        track("launch-card", "opacity", [(0, number(0)), (1, number(0)), (360, number(1)), (760, number(1)), (1140, number(0)), (1260, number(0))]),
        track("launch-card", "transform", [(0, transform(sx=0.94, sy=0.94)), (1, transform(sx=0.94, sy=0.94), EASE_OUT), (480, transform())]),
    ])
    doc.events["arenaMatchLaunch"] = [{"op": "setState", "values": {"arena.launch": True}}, {"op": "playTimeline", "timeline": "launch"}]
    return layer


def grow_ledger(doc: Document, phases: list):
    """The result ledger above 100 % text: each line (the result, the match,
    the message, the score, unlock and awards rows) is its 100 % height times
    the text size, standing as far below its place as the lines above it
    grew; the rows' labels and the NEW BEST tag keep their size, a label
    centred in its grown line. Returns what the lines grew, which the dialog
    grows by (confirmation's contents_grow)."""
    from build_retained_screens import LARGER, TEXT, fixed_text, text_sum, text_source, walk_nodes
    text_source(doc)
    nodes = {node["id"]: node for phase in phases for node in walk_nodes(phase)}

    def props(ident: str) -> dict:
        return nodes[ident]["properties"]
    # (the lines a row holds, by their tops) in order down the ledger.
    lines = [("result-title",), ("result-match",), ("result-message",), ("result-score-label", "result-score"),
             ("result-unlock-label", "result-unlock"), ("result-awards-label", "result-awards", "result-complete")]
    above = 0
    for line in lines:
        grew = 0
        for ident in line:
            top, height = props(ident)["top"]["value"], props(ident)["height"]["value"]
            if above:
                doc.bind(f"text.{ident}.top", ident, "top",
                         {"op": "select", "args": [LARGER, text_sum(top, {"op": "*", "args": [above, {"op": "-", "args": [TEXT, 1]}]}), top]})
            doc.bind(f"text.{ident}.height", ident, "height", {"op": "*", "args": [height, TEXT]})
            if ident.endswith("-label"):
                doc.bind(f"text.{ident}.font-size", ident, "font-size", {"op": "/", "args": [props(ident)["font-size"]["value"], TEXT]})
            elif props(ident).get("white-space", {}).get("value") == "nowrap":
                props(ident)["text-overflow"] = keyword("clip")
                doc.bind(f"text.{ident}.text-overflow", ident, "text-overflow", {"op": "select", "args": [LARGER, "ellipsis", "clip"]})
            grew = max(grew, height)
        if line == ("result-match",):
            # The rule under the match moves with it.
            top = props("result-rule")["top"]["value"]
            doc.bind("text.result-rule.top", "result-rule", "top",
                     {"op": "select", "args": [LARGER, text_sum(top, {"op": "*", "args": [above + grew, {"op": "-", "args": [TEXT, 1]}]}), top]})
        if line[-1] == "result-score":
            top = props("result-new-best")["top"]["value"]
            doc.bind("text.result-new-best.top", "result-new-best", "top",
                     {"op": "select", "args": [LARGER, text_sum(top, {"op": "*", "args": [above, {"op": "-", "args": [TEXT, 1]}]}), top]})
            fixed_text(doc, nodes["result-new-best"])
        above += grew
    return {"op": "*", "args": [above, {"op": "-", "args": [TEXT, 1]}]}


def result_modal(doc: Document) -> dict:
    """The result ledger: the stock modal grown to 250 u, titled MATCH REVIEW,
    revealed in the stock phases (arena_result_reveal_timeline) from the black
    the match ended on, which lifts over 720 ms."""
    doc.state["result.ready"] = {"type": "boolean", "initial": False}
    result = {"state": "arena_result"}
    won, lost, drew, aborted = ({"op": "==", "args": [result, value]} for value in (1, 2, 4, 3))
    complete = {"state": "arena_campaignComplete"}
    width = RESULT_WIDTH
    centred = {"text-align": keyword("center"), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}

    def text_at(ident, left, top, w, h, face, size, tint, state, extra=None, key=PLACEHOLDER, line=None):
        node = label(ident, key if state is None else PLACEHOLDER, {**absolute(left=left, top=top, width=w, height=h),
                     **typeface(face, size, line or h, tint), **(extra or {}), "pointer-events": keyword("none")})
        if state:
            doc.bind(f"{ident}.text", ident, "text", {"state": state})
        return node
    accents = []
    for ident, tint, condition in (("win", WIN, won), ("loss", LOSS, {"op": "||", "args": [lost, aborted]}), ("draw", DRAW, drew)):
        node = vector(f"result-accent-{ident}", {**absolute(left=33, top=40, width=width - 66, height=4), "display": keyword("none")}, [
            path("accent", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
                 fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(tint, 0)), (0.2, rgb(tint, 1)), (0.8, rgb(tint, 1)), (1, rgb(tint, 0))]))])
        doc.bind(f"result-accent-{ident}.display", f"result-accent-{ident}", "display", shown(condition))
        accents.append(node)
    rule = vector("result-rule", absolute(left=60, top=124, width=width - 120, height=1.5), [
        path("rule", [(0, 0.75), ({"fraction": 1}, 0.75)], closed=False, stroke=stroke(solid(rgb(ORANGE, 0.9)), 1.5))])
    phase1 = group("result-phase1", {**FULL, "opacity": number(0)}, [
        *accents,
        text_at("result-title", 33, 54, width - 66, 36, "marine", 28, [0.98, 0.95, 0.82, 1], "arena_resultTitle", centred),
        text_at("result-match", 33, 96, width - 66, 22, "lowpixel", 18, [1, 1, 1, 0.75], "arena_resultMatchName", centred),
        rule])
    row_label = {"white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    value_right = {"text-align": keyword("right"), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}

    wrap_right = {"text-align": keyword("right"), "white-space": keyword("normal"), "overflow": keyword("hidden")}

    def row(ident, key, top, state, flag, tint, size=17, lines=1):
        label_node = text_at(f"result-{ident}-label", 60, top + 2, 150, 20, "lowpixel", 15, [1, 1, 1, 0.5], None, row_label, key=key)
        if lines > 1:
            value = text_at(f"result-{ident}", 210, top, width - 270, 19 * lines, "lowpixel", size, tint, state, wrap_right, line=19)
        else:
            value = text_at(f"result-{ident}", 210, top, width - 270, 22, "lowpixel", size, tint, state, value_right)
        holder = group(f"result-{ident}-row", {**FULL, "display": keyword("none")}, [label_node, value])
        doc.bind(f"result-{ident}-row.display", f"result-{ident}-row", "display", shown(flag))
        return holder
    # The win that completed the campaign earns the banner in the awards'
    # place; live progress (arena_campaignComplete) stays set on every later
    # replay and would bury their awards.
    unlocked = {"state": "arena_resultUnlockedCampaign"}
    awards_shown = {"op": "&&", "args": [{"state": "arena_resultHasAwards"}, {"op": "!", "args": [unlocked]}]}
    champion = text_at("result-complete", 60, 254, width - 120, 22, "lowpixel", 17, rgb(DONE), None, centred, key="#str_42026")
    champion["properties"]["display"] = keyword("none")
    doc.bind("result-complete.display", "result-complete", "display", shown({"op": "&&", "args": [unlocked, won]}))
    # NEW BEST, where a replay beat the match's recorded best, rides the score
    # row left of the right-aligned score, as the stock tag does.
    new_best = group("result-new-best", {**absolute(left=210, top=207, width=NEW_BEST_W, height=20), "display": keyword("none"),
                                           "pointer-events": keyword("none")}, [
        vector("result-new-best-tag", absolute(left=0, top=0, width=NEW_BEST_W, height=20), [
            path("tag", [(0.5, 0.5), ({"fraction": 1, "dp": -0.5}, 0.5), ({"fraction": 1, "dp": -0.5}, {"fraction": 1, "dp": -0.5}),
                          (0.5, {"fraction": 1, "dp": -0.5})], fill=solid([0.12, 0.24, 0.08, 0.95]), stroke=stroke(solid([0.5, 0.74, 0.28, 1]), 1))]),
        text_at("result-new-best-label", 0, 0, NEW_BEST_W, 20, "lowpixel", 13, [0.88, 1, 0.7, 1], None, centred, key="#str_42165")])
    doc.bind("result-new-best.display", "result-new-best", "display", shown({"state": "arena_resultNewBest"}))
    phase2 = group("result-phase2", {**FULL, "opacity": number(0)}, [
        text_at("result-message", 40, 134, width - 80, 66, "lowpixel", 17, [1, 1, 1, 0.75], "arena_resultMessage",
                {"text-align": keyword("center"), "white-space": keyword("normal"), "overflow": keyword("hidden")}, line=22),
        row("score", "#str_42018", 206, "arena_resultScore", {"state": "arena_resultHasScore"}, rgb(VALUE)), new_best])
    phase3 = group("result-phase3", {**FULL, "opacity": number(0)}, [
        row("unlock", "#str_42019", 230, "arena_resultUnlock", {"state": "arena_resultHasUnlock"}, rgb(DONE)),
        row("awards", "#str_42154", 254, "arena_resultAwardLine", awards_shown, [0.95, 0.87, 0.6, 1], size=15, lines=2),
        champion])
    actions_u = RESULT_HEIGHT - 40
    grown = grow_ledger(doc, [phase1, phase2, phase3])
    modal = confirmation(doc, "resultModal", "#str_42080", PLACEHOLDER, None, yes_event="resultRetry", no_event="resultDismiss",
                         height_u=RESULT_HEIGHT, top_u=RESULT_TOP, actions_u=actions_u, contents=[phase1, phase2, phase3],
                         yes_key="#str_42033", no_key="#str_42034", initial="resultModal_no", width_dp=RESULT_WIDTH,
                         action_w=RESULT_ACTION_W, action_text=RESULT_ACTION_TEXT, contents_grow=grown)
    # The actions wait for the reveal: RETRY after a defeat or an aborted
    # match, REPLAY after a win or a draw, where the match can be played
    # again; then NEXT after a win short of the last, RETURN, or CONTINUE
    # after an aborted match that cannot be played again.
    contents = find(modal, "resultModal-contents")
    plates = [child for child in contents["children"] if child["id"] in ("resultModal_yes", "resultModal_no")]
    for plate in plates:
        contents["children"].remove(plate)
        plate["properties"]["display"] = keyword("block")
    contents["children"].append(group("result-actions", {**FULL, "display": keyword("none")}, plates))
    doc.bind("result-actions.display", "result-actions", "display", shown({"state": "result.ready"}))
    can_retry = {"state": "arena_resultCanRetry"}
    doc.bind("resultModal_yes.display", "resultModal_yes", "display", shown({"op": "&&", "args": [can_retry, any_of(won, lost, drew, aborted)]}))
    doc.bind("resultModal_yes-label.text", "resultModal_yes-label", "text", {"op": "select", "args": [any_of(lost, aborted), "#str_42033", "#str_42076"]})
    doc.bind("resultModal_no-label.text", "resultModal_no-label", "text", {"op": "select", "args": [
        {"op": "&&", "args": [aborted, {"op": "!", "args": [can_retry]}]}, "#str_42025",
        {"op": "select", "args": [{"op": "&&", "args": [won, {"op": "!", "args": [complete]}]}, "#str_42032", "#str_42034"]}]})
    doc.session("retry", "arenaRetry")
    doc.session("dismiss", "arenaDismiss")
    hide = [{"op": "setState", "values": {"result.ready": False}}, {"op": "call", "event": "resultModalHide"}]
    doc.events["resultRetry"] = [*hide, {"op": "action", "action": "retry"}]
    doc.events["resultDismiss"] = [*hide, {"op": "action", "action": "dismiss"}]
    doc.timelines.add("resultReveal", 1260, [
        track("arena-curtain", "opacity", [(0, number(1)), (1, number(1)), (720, number(0)), (1260, number(0))]),
        *[track(f"result-phase{index}", "opacity", [(0, number(0)), (1, number(0)), (at, number(0)), (at + 150, number(1)), (1260, number(1))])
          for index, at in ((1, 260), (2, 610), (3, 930))],
    ], complete="resultReady")
    doc.events["resultReady"] = [{"op": "setState", "values": {"result.ready": True}}]
    doc.events["arenaResultReveal"] = [{"op": "setState", "values": {"result.ready": False}}, {"op": "call", "event": "resultModalShow"},
                                       {"op": "playTimeline", "timeline": "resultReveal"}]
    return modal


def reset_modal(doc: Document) -> dict:
    """RESET PROGRESS asks with the stock modal, titled with the action and
    saying what it clears, grown to 150 u for four lines of it: the campaign
    raises arenaResetReveal once it holds the request, RESET clears the
    progress and CANCEL (or Back) keeps it."""
    doc.session("resetConfirm", "arenaResetConfirm")
    doc.session("resetCancel", "arenaResetCancel")
    body = label("resetModal-body", "#str_42073", {**absolute(left=33, top=71, width=RESET_W - 66, height=88),
                                                   **typeface("lowpixel", 17, 22, [1, 1, 1, 0.8])})
    # Above 100 % text the question takes the lines the running language
    # needs and the dialog grows with it, as a confirmation's body does.
    from build_retained_screens import LARGER, per_language, text_buckets, text_source, text_sum, text_times, wrapped_lines
    text_source(doc)
    counts = wrapped_lines(("#str_42073",), "lowpixel", 17, RESET_W - 66)
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    needed = text_times({"op": "*", "args": [lines, 22]})
    doc.bind("text.resetModal-body.height", "resetModal-body", "height",
             {"op": "select", "args": [LARGER, {"op": "max", "args": [88, needed]}, 88]})
    modal = confirmation(doc, "resetModal", "#str_42011", PLACEHOLDER, None, yes_event="resetConfirmed", no_event="resetCancelled",
                         yes_key="#str_42075", no_key="#str_42074", action_w=RESET_ACTION_W, action_text=RESET_ACTION_TEXT,
                         height_u=RESET_HEIGHT, top_u=RESET_TOP, actions_u=RESET_HEIGHT - 40, contents=[body],
                         contents_grow={"op": "max", "args": [0, text_sum(-88, needed)]})
    doc.events["resetConfirmed"] = [{"op": "call", "event": "resetModalHide"}, {"op": "action", "action": "resetConfirm"}]
    doc.events["resetCancelled"] = [{"op": "call", "event": "resetModalHide"}, {"op": "action", "action": "resetCancel"}]
    doc.events["arenaResetReveal"] = [{"op": "call", "event": "resetModalShow"}]
    return modal


def find(node: dict, ident: str) -> dict:
    if node.get("id") == ident:
        return node
    for child in node.get("children", []):
        found = find(child, ident)
        if found:
            return found
    return {}


def check_arena_fits() -> None:
    """Every shipped language's static text fits its place, and so does every
    text the campaign publishes from its own tables; generation fails
    otherwise."""
    plate_label = lambda width: U * width - U * 40
    head = lambda width: U * (width - 4)
    reset_w = RESET_W
    one_line = tuple((key, "marine", TIER_TEXT, plate_label(LADDER[2])) for key in TIER_KEYS)
    one_line += tuple((key, "marine", MATCH_TEXT, plate_label(COLUMN[2])) for key in MATCH_KEYS[:BOSS])
    one_line += (("#str_42024", "marine", BOSS_TEXT, U * COLUMN[2] - U * 22),)
    one_line += tuple((key, "lowpixel", STATUS_TEXT, plate_label(LADDER[2])) for key in ("#str_42022", "#str_42023", "#str_42045", "#str_42065",
                                                                                       "#str_42039"))
    one_line += (("#str_42007", "lowpixel", 13, head(LADDER[2])), ("#str_42008", "lowpixel", 13, head(COLUMN[2])),
                 ("#str_42070", "lowpixel", 13, head(COLUMN[2])), ("#str_42026", "lowpixel", 13, head(LADDER[2])),
                 ("#str_42011", "marine", RESET_TEXT, head(LADDER[2]) - U * 7), ("#str_42009", "lowpixel", 13, U * DETAIL_W),
                 ("#str_42010", "marine", 23.7, U * 194), ("#str_200018", "marine", 20, U * 109 - 30),
                 ("#str_42079", "lowpixel", 18, U * 448), ("#str_42086", "lowpixel", 15, U * 448),
                 ("#str_42080", "marine", 20, RESULT_WIDTH * 374 / 512 - 34), ("#str_42018", "lowpixel", 15, 150),
                 ("#str_42019", "lowpixel", 15, 150), ("#str_42154", "lowpixel", 15, 150), ("#str_42026", "lowpixel", 17, RESULT_WIDTH - 120),
                 ("#str_42011", "marine", 20, reset_w * 374 / 512 - 34))
    one_line += tuple((key, "marine", RESET_ACTION_TEXT, RESET_ACTION_W - 30) for key in ("#str_42075", "#str_42074"))
    best_w = U * (DETAIL_W - 14)
    one_line += (("#str_42040", "lowpixel", 13, best_w), ("#str_42166", "lowpixel", 14, best_w), ("#str_42165", "lowpixel", 13, NEW_BEST_W - 8))
    # The record as ArenaDescribeBest joins it: the widest result, a two-digit
    # score and the widest difficulty.
    for language in LANGUAGES:
        def widest(keys: tuple) -> float:
            return max(text_width("lowpixel", table_text(key)[language], 14) for key in keys)
        need = (widest(("#str_42030", "#str_42031", "#str_42077")) + text_width("lowpixel", "   99 - 99   ", 14) +
                widest(tuple(f"#str_{number_}" for number_ in range(42060, 42065))))
        if need > best_w:
            raise SystemExit(f"the best result in {language} needs {need:.0f} dp of {best_w:.0f}")
    one_line += tuple((key, "lowpixel", 13, U * (INFO_VALUE_X - 4)) for _, key, _, _, _ in INFO)
    one_line += tuple((key, "marine", RESULT_ACTION_TEXT, RESULT_ACTION_W - 30)
                      for key in ("#str_42033", "#str_42076", "#str_42032", "#str_42034", "#str_42025"))
    # What the campaign publishes from its own tables: tier and match names,
    # the game types, the difficulties, the results and the unlocks.
    one_line += tuple((f"#str_{number_}", "marine", TITLE_TEXT, head(COLUMN[2])) for number_ in range(42050, 42055))
    one_line += tuple((f"#str_{number_}", "marine", TITLE_TEXT, U * DETAIL_W) for number_ in range(42100, 42140, 2))
    one_line += tuple((f"#str_{number_}", "marine", 32, U * 448) for number_ in range(42100, 42140, 2))
    one_line += tuple((f"#str_{number_}", "lowpixel", BODY_TEXT, U * (DETAIL_W - INFO_VALUE_X)) for number_ in range(42200, 42205))
    one_line += tuple((f"#str_{number_}", "lowpixel", 16, U * (DETAIL_W - 44)) for number_ in range(42060, 42065))
    one_line += tuple((key, "marine", 28, RESULT_WIDTH - 66) for key in ("#str_42030", "#str_42031", "#str_42068", "#str_42077", "#str_42163"))
    one_line += tuple((key, "lowpixel", 17, RESULT_WIDTH - 270) for key in ("#str_42045", "#str_42046", "#str_42047"))
    wrapped = (("#str_42049", "lowpixel", 13, head(COLUMN[2]), 3), ("#str_42071", "lowpixel", 13, head(COLUMN[2]), 2),
               ("#str_42073", "lowpixel", 17, reset_w - 66, 4))
    wrapped += tuple((f"#str_{number_}", "lowpixel", BRIEFING_TEXT, head(COLUMN[2]), 3) for number_ in range(42055, 42060))
    wrapped += tuple((f"#str_{number_}", "lowpixel", BODY_TEXT, U * DETAIL_W, 3) for number_ in range(42101, 42140, 2))
    wrapped += tuple((key, "lowpixel", 17, RESULT_WIDTH - 80, 3)
                     for key in ("#str_42066", "#str_42067", "#str_42069", "#str_42078", "#str_42150", "#str_42151", "#str_42161",
                                 "#str_42162", "#str_42164"))
    check_fits(one_line, wrapped)


def arena_document() -> dict:
    """Single Player's Arena sub-page (section 9 "Menu levels" and "Arena
    Campaign", FLOW-011): the Arena campaign's browser in the sub-page state
    Campaign holds, the top band one notch on, the bottom band docked, SINGLE
    PLAYER as the crumb and ARENA as the title at the step's foot, over the
    stock page's backing. The session plays the change between Single Player
    and this document (section 8, FLOW-050):
    onActivate    every arrival: the content at once, its backing fading in
                  over 150 ms (content.in), the focus on the chosen match;
    subpageLeave  Back, 300 ms: its title drops at once, it sweeps 640 u
                  toward the leading edge over 300 ms (screen.return,
                  linear), its backing fades over 250 ms (content.out), the
                  top band steps back and the crumb returns to the title slot
                  over the last 150 ms.
    Leaving the screen (onDeactivate) places it at rest, its entrance, ledger
    and modal closed, so it is never seen moved or covered when it next
    presents."""
    check_arena_fits()
    doc = Document("openq4.arena")
    published_states(doc)
    doc.session("back", "arenaBack")
    doc.session("tierPrevious", "arenaTierPrevious")
    doc.session("tierNext", "arenaTierNext")
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    # The bumpers (and Q and E) switch tier, unless a modal or the entrance
    # holds the screen.
    free = all_of(OPEN, *({"op": "!", "args": [{"state": name}]} for name in ("arena.launch", "resultModal.visible", "resetModal.visible")))
    for event, action in (("onTabPrevious", "tierPrevious"), ("onTabNext", "tierNext")):
        doc.events[event] = [{"op": "if", "condition": free, "then": [{"op": "action", "action": action}]}]
    canvas = {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}
    back = action_plate(doc, "arena_back", "#str_200018", U * 532, U * 441, action="back", width=U * 109)
    doc.bind("arena_back.enabled", "arena_back", "enabled", OPEN)
    launch = launch_layer(doc)
    result = result_modal(doc)
    reset = reset_modal(doc)
    content = group("content", {**canvas, "transform": transform(), "opacity": number(1)},
                    [*ladder(doc), *briefing(doc), *progress(doc), *detail(doc), back])
    bands = framing_bands("band")
    bands[0]["properties"]["transform"] = transform(*TOP_TO_SUBPAGE)
    bands[1]["properties"]["transform"] = transform(*BOTTOM_TO_PAGE)
    crumb_key = "#str_42000"
    fits = crumb_fits(crumb_key)
    titles = group("titles", {**canvas, "pointer-events": keyword("none")}, [
        group("crumb-clip", {**absolute(left=U * CRUMB_SLOT[0], top=0, width=U * CRUMB_SPAN, height=U * 40), "overflow": keyword("hidden")}, [
            label("crumb", crumb_key, {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                       **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"),
                                       "transform": crumb_transform()})]),
        page_title("title", "#str_42002", *SUBPAGE_SLOT),
    ])
    backing_node = backing("arena-backing")
    backing_node["properties"]["opacity"] = number(0)
    curtain = group("arena-curtain", {**FULL, "background-color": colour([0, 0, 0, 1]), "opacity": number(0), "pointer-events": keyword("none")})

    page_top, sub_top = transform(*TOP_TO_PAGE), transform(*TOP_TO_SUBPAGE)
    identity, swept_left = transform(), transform(-640 * U, 0)
    white = lambda alpha: colour([1, 1, 1, alpha])
    doc.timelines.add("arrive", 150, [
        track("content", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
        track("arena-backing", "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5)), (150, white(0.5))]),
    ])
    # Hidden, the page waits in the sub-page state with its backing out.
    doc.timelines.add("rest", 1, [
        track("band-top", "transform", [(0, sub_top), (1, sub_top)]),
        track("content", "transform", [(0, identity), (1, identity)]),
        track("content", "opacity", [(0, number(1)), (1, number(1))]),
        track("arena-backing", "opacity", [(0, number(0)), (1, number(0))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
        track("crumb", "color", [(0, white(0.4)), (1, white(0.4))]),
        track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (1, length(U * CRUMB_SPAN))]),
        track("arena-curtain", "opacity", [(0, number(0)), (1, number(0))]),
    ])
    doc.timelines.add("subpageLeave", 300, [
        track("title", "color", [(0, white(0)), (1, white(0)), (300, white(0))]),
        track("content", "transform", [(0, identity), (300, swept_left)]),
        track("arena-backing", "opacity", [(0, number(1)), (250, number(0)), (300, number(0))]),
        track("band-top", "transform", [(0, sub_top, ACCEL), (300, page_top)]),
        track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (150, length(U * CRUMB_SPAN)), (151, length(U * PAGE_TITLE_W)),
                                      (300, length(U * PAGE_TITLE_W))]),
        track("crumb", "color", [(0, white(0.4)), (150, white(0.4)), (300, white(0.5))]),
    ])
    for suffix, scale in [("", CRUMB_SCALE)] + [(f"-{language}", fit) for language, fit in fits.items()]:
        fitted = crumb_transform(scale)
        doc.timelines.add(f"crumbRest{suffix}", 1, [track("crumb", "transform", [(0, fitted), (1, fitted)])])
        doc.timelines.add(f"crumbReturn{suffix}", 300, [track("crumb", "transform", [(0, fitted), (150, fitted), (300, identity)])])
    closed = {"arena.launch": False, "result.ready": False, "resultModal.visible": False, "resultModal.contents": False,
              "resetModal.visible": False, "resetModal.contents": False}
    doc.events["onDeactivate"] = [{"op": "setState", "values": closed}, {"op": "playTimeline", "timeline": "rest"},
                                  *by_language(doc, "crumbRest", fits)]
    doc.events["onActivate"] = [{"op": "setState", "values": closed}, *by_language(doc, "crumbRest", fits),
                                {"op": "playTimeline", "timeline": "arrive"}, *focus_page()]
    doc.events["subpageLeave"] = [{"op": "playTimeline", "timeline": "subpageLeave"}, *by_language(doc, "crumbReturn", fits)]
    # Back from the ledger, the browser takes the focus on the chosen match:
    # the next one after a win.
    doc.events["arenaProgressReveal"] = focus_page()
    doc.events["arenaBrowserReveal"] = focus_page()
    root = group("screen", {**FULL, "background-color": colour([0, 0, 0, 1]), "font-family": font("marine"), "font-size": length(16),
                            "color": colour([1, 1, 1, 0.8])},
                 [*lit_field("field", 0.7), *bands, backing_node, content, titles, launch, result, reset, curtain])
    fitted = fit_narrow(doc.build(root), pages=True)
    grow_arena(doc, fitted)
    return tall_arena_page(fitted, doc.arena_lettered)


# Section 2's text size on the Arena page. The plates, headings, the info's
# labels, the steps, START MATCH, RESET PROGRESS and the best-result badge are
# furniture; the briefing, the progress, the chosen match's name, description
# and info and the difficulty's name are content.
PICTURE_LEAST = 30.0     # u: a levelshot squeezed shorter than this gives its room to the lines
ARENA_FLOOR = 420.0      # u: content stands over the docked bottom band's edge


def grow_arena(doc: Document, result: dict) -> None:
    """Above 100 % text the chosen match's lines grow under its levelshot,
    which gives them its room (shrinking from its foot, and giving way where
    too little is left), DIFFICULTY, its steps and START MATCH moving down
    only as far as the lines still need; the briefing's lines grow down the
    matches' column, its static rule giving way where it would cross the
    bottom band (the path above it says the same); the progress grows under
    the ladder. Each value ends in an ellipsis past its column. Every binding
    gives the authored value at 100 % (check_text_bindings). Runs on the page
    as the narrow-view fit left it, whose parts stand by their offset from
    the canvas's middle. In a tall view, whose arrangement grows the page a
    fixed amount, the content keeps that size at every text size, as the list
    pages' details do."""
    from build_retained_screens import LARGER as ANY_LARGER, TALL, TEXT, fixed_text, text_source, text_sum, walk_nodes
    text_source(doc)
    nodes = {node["id"]: node for node in walk_nodes(result["root"])}
    LARGER = {"op": "&&", "args": [ANY_LARGER, {"op": "!", "args": [TALL]}]}
    lettered = []

    def u(value):
        """`value` (u, or an expression in u) in dp."""
        return round(U * value, 4) if isinstance(value, (int, float)) else {"op": "*", "args": [U, value]}

    def times(value: float):
        return {"op": "*", "args": [value, TEXT]}

    def larger(ident: str, key: str, grown, authored=None) -> None:
        if authored is None:
            authored = nodes[ident]["properties"][key]["value"]
        doc.bind(f"text.{ident}.{key}", ident, key, {"op": "select", "args": [LARGER, grown, authored]})

    def stand(ident: str, top) -> None:
        """`ident` stands at `top` (u on the canvas) above 100 %."""
        props = nodes[ident]["properties"]
        if props["top"].get("unit") == "dp":
            larger(ident, "top", u(top))
        else:
            larger(ident, "margin-top", text_sum(-360, u(top)))

    def grow(ident: str, height_u: float, top=None) -> None:
        if top is not None:
            stand(ident, top)
        larger(ident, "height", u(times(height_u)))
        props = nodes[ident]["properties"]
        if props.get("white-space", {}).get("value") == "nowrap":
            props["text-overflow"] = keyword("clip")
            larger(ident, "text-overflow", "ellipsis", "clip")
        lettered.append(ident)

    # The chosen match: its levelshot, then its name, rule, description and
    # info, then DIFFICULTY, the steps and START MATCH.
    below = 6 + 6 + 4 + 4
    lines = 14 + 36 + (INFO[-1][3] + 12 - INFO[0][3])
    room = DIFFICULTY_TOP - SHOT_TOP
    squeezed = text_sum(room - below, times(-lines))                   # u: the levelshot's height above 100 %
    pictured = {"op": ">=", "args": [squeezed, PICTURE_LEAST]}
    picture_h = {"op": "select", "args": [pictured, {"op": "min", "args": [SHOT_H, squeezed]}, 0]}
    hidden = {"op": "&&", "args": [LARGER, {"op": "!", "args": [pictured]}]}
    for ident in ("arena-shot-back", "arena-shot", "arena-shot-border"):
        inset = SHOT_H - nodes[ident]["properties"]["height"]["value"] / U
        larger(ident, "height", u({"op": "max", "args": [0, text_sum(-inset, picture_h)]}))
        nodes[ident]["properties"]["display"] = keyword("block")
        doc.bind(f"text.{ident}.display", ident, "display", {"op": "select", "args": [hidden, "none", "block"]})
    # The best-result badge rides the levelshot's foot and goes with it.
    nodes["arena-best"]["properties"]["opacity"] = number(1)
    doc.bind("text.arena-best.opacity", "arena-best", "opacity",
             {"op": "select", "args": [{"op": "&&", "args": [LARGER, {"op": "<", "args": [picture_h, BEST_H + 10]}]}, 0, 1]})
    stand("arena-best", text_sum(SHOT_TOP - 1 - BEST_H, picture_h))
    title = {"op": "select", "args": [pictured, text_sum(SHOT_TOP + 6, picture_h), SHOT_TOP]}
    grow("arena-match-title", 14, title)
    stand("arena-title-rule", text_sum(2, title, times(14)))
    description = text_sum(6, title, times(14))
    grow("arena-match-description", 36, description)
    info = text_sum(4, description, times(36))
    for ident, _key, _state, top, info_lines in INFO:
        at = text_sum(info, times(top - INFO[0][3]))
        grow(f"arena-{ident}", 12 * info_lines, at)
        # The label keeps its size, centred in its grown line.
        stand(f"arena-{ident}-label", text_sum(1, at))
        larger(f"arena-{ident}-label", "height", u(times(11)))
        label_props = nodes[f"arena-{ident}-label"]["properties"]
        doc.bind(f"text.arena-{ident}-label.font-size", f"arena-{ident}-label", "font-size",
                 {"op": "/", "args": [label_props["font-size"]["value"], TEXT]})
    shift = {"op": "max", "args": [0, text_sum(4 - DIFFICULTY_TOP, info, times(INFO[-1][3] + 12 - INFO[0][3]))]}
    steps_top = DIFFICULTY_TOP + 13
    for ident, top in (("arena-difficulty-label", DIFFICULTY_TOP), ("arena_easier", steps_top), ("arena_harder", steps_top),
                       ("arena_start", START_TOP)):
        stand(ident, text_sum(top, shift))
    # The difficulty's name stays centred between its steps.
    grow("arena-difficulty", 14, text_sum(steps_top + 2 + 7, shift, times(-7)))
    # The briefing down the matches' column.
    name_top = BRIEFING_TOP + 12
    grow("arena-tier-name", 15)
    tier_description = text_sum(name_top + 2, times(15))
    grow("arena-tier-description", 36, tier_description)
    path_top = text_sum(3, tier_description, times(36))
    grow("arena-tier-path", 22, path_top)
    rule_top = text_sum(2, path_top, times(22))
    grow("arena-tier-rule", 36, rule_top)
    nodes["arena-tier-rule"]["properties"]["display"] = keyword("block")
    doc.bind("text.arena-tier-rule.display", "arena-tier-rule", "display",
             {"op": "select", "args": [{"op": ">", "args": [text_sum(rule_top, times(36)), ARENA_FLOOR]}, "none", "block"]})
    # The progress under the ladder: its line wraps onto a second, so that
    # its count stays whole in the narrow column.
    progress_lines = {"op": "select", "args": [{"op": ">", "args": [TEXT, 1.5]}, 3, {"op": "select", "args": [LARGER, 2, 1]}]}
    progress = {"op": "*", "args": [progress_lines, times(13)]}   # u
    larger("arena-progress", "height", u(progress))
    larger("arena-progress", "white-space", "normal")
    lettered.append("arena-progress")
    for ident in ("arena-progress-track", "arena-progress-fill"):
        stand(ident, text_sum(PROGRESS_TOP + 2, progress))
    grow("arena-complete", 12, text_sum(PROGRESS_TOP + 9, progress))
    # RESET PROGRESS stands under CAMPAIGN COMPLETE where that shows, else
    # under the bar, clear of the bottom band.
    complete = {"state": "arena_campaignComplete"}
    stand("arena-reset-row", {"op": "select", "args": [complete, text_sum(PROGRESS_TOP + 15, times(12), progress),
                                                       text_sum(PROGRESS_TOP + 11, progress)]})
    for ident in ("arena-tiers-head", "arena-matches-head", "arena-briefing-head", "arena-difficulty-label", "arena-best", "arena_easier",
                  "arena_harder", "arena_start", "arena_back", *(f"tier-{index}" for index in range(TIERS)),
                  *(f"match-{index}" for index in range(MATCHES))):
        fixed_text(doc, nodes[ident])
    doc.arena_lettered = lettered


# A tall view's Arena page (retained_tall): the three columns become two
# blocks, one under the other, each a column of plates with what they choose
# beside it, so the plates and START MATCH run down one line for the focus.
# First the tier ladder, with the chosen tier's briefing beside it and the
# campaign's progress at the ladder's foot; then the chosen tier's matches,
# with DIFFICULTY and START MATCH under them, and the chosen match's
# levelshot, name, description and rules beside them. The page grows 1.4
# times (the detail as much as the room beside the matches allows, the
# difficulty and START MATCH to the matches' width), which a view of 3:4
# holds between the bands, and stands centred there in taller views. The
# ledger and the reset question grow as the list pages' confirmations do, and
# the entrance's card as far as the column takes it.
TALL_GROW = 1.4
TALL_DIALOGS = 1.6
TALL_TOP, TALL_BOTTOM = 80.0, 72.0             # dp: under the top band; over the prompt bar
TALL_BESIDE = 24.0                             # dp: from a column of plates to what stands beside it
TALL_BLOCKS = 30.0                             # dp: between the tiers' block and the matches'


def tall_arena_page(result: dict, lettered: list = ()) -> dict:
    from build_retained_screens import TALL
    from retained_saves import BACKING_ALPHA
    from retained_tall import CONTENT_W, MARGIN, TallLayout
    tall = TallLayout(result)
    right = CANVAS_W - MARGIN
    grow = TALL_GROW
    span = lambda first, last: tall.canvas_top(last) + tall.dp(last, "height") - tall.canvas_top(first)  # noqa: E731
    ladder_w = U * LADDER[2] * grow
    ladder_h = span("arena-tiers-head", f"tier-{TIERS - 1}") * grow
    matches_w = U * COLUMN[2] * grow
    matches_h = span("arena-matches-head", f"match-{MATCHES - 1}") * grow
    detail_x = MARGIN + matches_w + TALL_BESIDE
    detail = min(grow, (right - detail_x) / (U * DETAIL_W))
    detail_h = span("arena-shot-back", f"arena-{INFO[-1][0]}") * detail
    steps = matches_w / (U * DETAIL_W)
    steps_h = span("arena-difficulty-label", "arena_start") * steps
    height = ladder_h + TALL_BLOCKS + max(matches_h + TALL_BESIDE + steps_h, detail_h)
    # The shortest tall view's height less the bands.
    room = CANVAS_W / TALL["args"][1] - TALL_TOP - TALL_BOTTOM
    if height > room:
        raise SystemExit(f"the Arena page needs {height:.0f} dp of a tall view's {room:.0f}")
    shift = tall.centred(height, TALL_TOP, TALL_BOTTOM)
    top = TALL_TOP
    tall.block(("arena-tiers-head", *(f"tier-{index}" for index in range(TIERS))), (U * LADDER[0], U * HEAD_TOP), (MARGIN, top), grow, shift)
    # Beside the ladder the briefing runs on to the column's margin, and the
    # progress stands at the ladder's foot, its bar as wide.
    beside = MARGIN + ladder_w + TALL_BESIDE
    beside_w = right - beside
    briefing_ids = ("arena-briefing-head", "arena-tier-name", "arena-tier-description", "arena-tier-path", "arena-tier-rule")
    tall.block(briefing_ids, (U * (COLUMN[0] + 4), U * BRIEFING_TOP), (beside, top), grow, shift)
    progress_ids = ("arena-progress", "arena-progress-track", "arena-progress-fill", "arena-complete", "arena-reset-row")
    progress_h = span("arena-progress", "arena-reset-row") * grow
    tall.block(progress_ids, (U * (LADDER[0] + 4), U * PROGRESS_TOP), (beside, top + ladder_h - progress_h), grow, shift)
    for ident in (*briefing_ids, "arena-progress", "arena-progress-track", "arena-complete"):
        tall.set(ident, "width", beside_w)
    fill = next(binding for binding in tall.bindings if binding["id"] == "arena-progress-fill.width")
    bar_w = tall.dp("arena-progress-track", "width")
    fill["value"] = {"op": "select", "args": [TALL, {"op": "*", "args": [round(beside_w / bar_w, 4), fill["value"]]}, fill["value"]]}
    top += ladder_h + TALL_BLOCKS
    tall.block(("arena-matches-head", *(f"match-{index}" for index in range(MATCHES))), (U * COLUMN[0], U * HEAD_TOP), (MARGIN, top), grow,
               shift)
    detail_ids = ("arena-shot-back", "arena-shot", "arena-best", "arena-shot-border", "arena-match-title", "arena-title-rule",
                  "arena-match-description", *(f"arena-{ident}{part}" for ident, *_rest in INFO for part in ("-label", "")))
    tall.block(detail_ids, (U * DETAIL_X, U * SHOT_TOP), (detail_x, top), detail, shift)
    tall.block(("arena-difficulty-label", "arena_easier", "arena-difficulty", "arena_harder", "arena_start"), (U * DETAIL_X, U * DIFFICULTY_TOP),
               (MARGIN, top + matches_h + TALL_BESIDE), steps, shift)
    # The content that grows with the text in a landscape view (grow_arena)
    # keeps its letters at the arrangement's size here.
    for ident in lettered:
        for key in ("font-size", "line-height"):
            found = next(binding for binding in tall.bindings if binding["id"] == f"tall.{ident}.{key}")
            found["value"]["args"][1] = {"op": "/", "args": [found["value"]["args"][1], {"state": "view.text"}]}
    tall.dialogs(TALL_DIALOGS)
    tall.scale("launch-stage", round(min(TALL_DIALOGS, CONTENT_W / tall.dp("launch-card", "width")), 4))
    tall.shade("arena-backing", BACKING_ALPHA)
    return result
