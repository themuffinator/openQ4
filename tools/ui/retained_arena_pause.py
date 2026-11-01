"""The retained Arena pause menu (docs/dev/ui-visual-design.md section 13.7,
"Arena", with sections 6, 8 and 9).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. During a single-player Arena Campaign match game_mp
opens its own pause menu (guis/arena_pause.gui) in the multiplayer menu's
place, and the session covers it with this card the way the multiplayer cards
cover the game's menu (idSessionLocal::UpdateRetainedMultiplayer). The match
pauses behind it like a story level behind the single-player pause.

The card keeps the single-player pause's frame without reading as it: MATCH
PAUSED stands where GAME PAUSED does, and the navigation offers Resume,
Restart match, Settings and Leave match on the stock main menu's rows, with
nothing on the plinth. The match block takes the level block's place: the
match's tier and role, the map's levelshot, the match, the game type the
campaign presents, the map, the paused clock and the score. Restart match and
Leave match ask first in the stock modal. The verbs reach the game's Arena
pause menu as its own commands (idSessionLocal::HandleRetainedMultiplayerRequest),
so the campaign keeps the restart and the abandoned report.

The game publishes the arena_pause_* values (idMultiplayerGame::
PublishArenaPauseMenu) and the session the levelshot as pause_shot.
"""
from __future__ import annotations

import build_retained_screens as b
from build_retained_screens import (CANVAS_W, FULL, MARKER, PICTURE, RAIL_CARD, SOFT_FOCUS_BLUR, SOFT_FOCUS_SATURATION, U, VALUE,
                                    Document, absolute, colour, group, keyword, label, length, linear, number, path, picture, rgb,
                                    solid, stroke, track, transform, typeface, vector)

TITLE_KEY = "#str_42186"                     # MATCH PAUSED
RESUME_KEY, SETTINGS_KEY = "#str_200381", "#str_200009"
RESTART_KEY, LEAVE_KEY = "#str_42180", "#str_42181"
RESTART_TITLE, RESTART_BODY = "#str_42182", "#str_42183"
LEAVE_TITLE, LEAVE_BODY = "#str_42184", "#str_42185"
SCORE_KEY = "#str_42018"                     # SCORE
NAV_TOP_U = 212.2                            # the stock main menu's first row (214 u) less the plate's 2.8 u lead
TITLE_TOP_U = 164.0                          # the title stands 48 u above the navigation, as GAME PAUSED does
STATE_KEYS = ("arena_pause_title", "arena_pause_detail", "arena_pause_mode", "arena_pause_map", "arena_pause_clock",
              "arena_pause_score", "pause_shot")


def match_block(doc: Document) -> dict:
    """The match in the level block's place (section 13.7): the section 6 card
    with a 40 dp header naming the match's tier and role, the levelshot in its
    picture frame, the match, its game type and map, and under a faint rule
    the score with the paused clock in the value colour."""
    x, y, w, h = U * 374, U * 118, U * 262, U * 270
    major, minor = 12.0, 3.0
    card = [(minor, 0), (w - major, 0), (w, major), (w, h - minor), (w - minor, h), (major, h), (0, h - major), (0, minor)]
    header = 40.0
    frame = vector("match-card", absolute(left=0, top=0, width=w, height=h), [
        path("body", card, fill=solid([0, 0, 0, 0.94])),
        path("wash", [(minor, 0.75), (w - major, 0.75), (w - major + header * 0.3, header), (0.75, header), (0.75, minor)],
             fill=linear((0, 0), (w, 0), [(0, [1, 1, 1, 0.08]), (0.5, [1, 1, 1, 0.08]), (1, [1, 1, 1, 0])])),
        path("rule", [(0, header), (w, header)], closed=False, stroke=stroke(solid(rgb(RAIL_CARD, 0.5)), 1)),
        path("rail", card, stroke=stroke(solid(rgb(RAIL_CARD)), 1.5)),
        b.marker_path("mark", 14, 16, 8, rgb(MARKER)),
    ])
    pw, ph = w - 30, U * 118
    shot = picture("match-shot", "", {**absolute(left=15, top=header + 12, width=pw, height=ph)})
    shot_frame = vector("match-shot-frame", absolute(left=15, top=header + 12, width=pw, height=ph), [
        path("frame", [(0.5, 0.5), ({"fraction": 1, "dp": -0.5}, 0.5), ({"fraction": 1, "dp": -0.5}, {"fraction": 1, "dp": -0.5}),
                       (0.5, {"fraction": 1, "dp": -0.5})], stroke=stroke(solid(rgb(PICTURE)), 1.5))])
    line = {"white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    heading = label("match-heading", "#str_42006", {**absolute(left=30, top=0, width=w - 60, height=header),
        **typeface("marine", 14, header, [1, 1, 1, 0.9]), **line})
    below = header + 12 + ph + 10
    name = label("match-name", "#str_42006", {**absolute(left=15, top=below, width=pw, height=24),
        **typeface("marine", 20, 24, [1, 1, 1, 0.9]), **line})
    mode = label("match-mode", "#str_42006", {**absolute(left=15, top=below + 26, width=pw, height=20),
        **typeface("lowpixel", 17, 20, [1, 1, 1, 0.6]), **line})
    where = label("match-map", "#str_42006", {**absolute(left=15, top=below + 46, width=pw, height=20),
        **typeface("lowpixel", 17, 20, [1, 1, 1, 0.6]), **line})
    stats_rule = h - U * 40
    rule = vector("match-stats-rule", absolute(left=15, top=stats_rule, width=pw, height=1),
                  [path("rule", [(0, 0.5), (pw, 0.5)], closed=False, stroke=stroke(solid([1, 1, 1, 0.1]), 1))])
    score_label = label("match-score-label", SCORE_KEY, {**absolute(left=15, top=stats_rule + 6, width=pw / 2, height=U * 12),
        **typeface("lowpixel", 13, U * 12, rgb(b.LEVEL_STATS)), "display": keyword("none"), **line})
    clock = label("match-clock", "#str_42006", {**absolute(left=15 + pw / 2, top=stats_rule + 6, width=pw / 2, height=U * 12),
        **typeface("lowpixel", 13, U * 12, rgb(VALUE)), "text-align": keyword("right"), **line})
    score = label("match-score", "#str_42006", {**absolute(left=15, top=stats_rule + 6 + U * 12, width=pw, height=U * 15),
        **typeface("marine", 16, U * 15, [1, 1, 1, 0.85]), **line})
    for node, state in (("match-heading", "arena_pause_detail"), ("match-name", "arena_pause_title"), ("match-mode", "arena_pause_mode"),
                        ("match-map", "arena_pause_map"), ("match-clock", "arena_pause_clock"), ("match-score", "arena_pause_score")):
        doc.bind(f"{node}.text", node, "text", {"state": state})
    doc.bind("match-shot.image", "match-shot", "image", {"state": "pause_shot"})
    # A match before its first frag, or a clock the game cannot read, shows no empty line.
    doc.bind("match-score-label.display", "match-score-label", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "arena_pause_score"}, ""]}, "none", "block"]})
    return group("match-block", absolute(left=x, top=y, width=w, height=h),
                 [frame, shot, shot_frame, heading, name, mode, where, rule, score_label, clock, score])


def tall_arena_pause(doc: Document, home: dict, title: dict, nav: dict, block: dict, modals: tuple) -> None:
    """The pause in a tall view, as the single-player pause's column: ARENA
    PAUSED, the actions and the match block under them, centred between the
    bands at the column's scale, and its questions at 1.6 times their size."""
    b.tall_canvas(doc, home)
    plates = sum(child["properties"]["height"]["value"] for child in nav["children"])
    card_w, card_h = block["properties"]["width"]["value"], block["properties"]["height"]["value"]
    scale, _ = b.tall_column(doc, [(title["properties"]["height"]["value"], title), (6, None), (plates, nav), (24, None), (card_h, block)],
                             b.TALL_HOME_TOP, b.TALL_HOME_BOTTOM)
    b.tall_scale(doc, title, scale, own=("left", "width", "height"))
    b.tall_scale(doc, nav, scale, own=("width",))
    b.tall_scale(doc, block, scale, own=("width", "height"))
    b.tall_bind(doc, block, "left", {"op": "*", "args": [0.5, b.text_sum(CANVAS_W, {"op": "*", "args": [-card_w, scale]})]})
    for modal in modals:
        b.tall_dialog(doc, modal)


def arena_pause_document() -> dict:
    doc = Document("openq4.arena_pause")
    doc.state.update({key: {"type": "string", "initial": ""} for key in STATE_KEYS})
    doc.session("resume", "resume")
    doc.session("settings", "settings")
    doc.session("arenaRestart", "arenaRestart")
    doc.session("arenaLeave", "arenaLeave")
    # Titles too long for the lit slot in some language run on over the raised
    # trailing section, as the pause's QUIT TO MENU does.
    restart_modal = b.confirmation(doc, "restartModal", RESTART_TITLE, RESTART_BODY, "arenaRestart", wide_title=True)
    leave_modal = b.confirmation(doc, "leaveModal", LEAVE_TITLE, LEAVE_BODY, "arenaLeave", wide_title=True)
    doc.events["onBack"] = [{"op": "action", "action": "resume"}]
    items = [("nav_resume", RESUME_KEY, "resume", None), ("nav_restart", RESTART_KEY, None, "restartModalShow"),
             ("nav_settings", SETTINGS_KEY, "settings", None), ("nav_leave", LEAVE_KEY, None, "leaveModalShow")]
    plates = [b.navigation_plate(doc, ident, key, action=action, event=event) for ident, key, action, event in items]
    b.wrap_menu(plates)
    nav = group("nav", {**absolute(left=0, top=U * NAV_TOP_U, width=U * 413), "display": keyword("flex"),
                        "flex-direction": keyword("column")}, plates)
    title = label("paused-title", TITLE_KEY, {**absolute(left=U * 44, top=U * TITLE_TOP_U, width=U * 330, height=U * 24),
                  **typeface("marine", 18, U * 24, [1, 1, 1, 0.5]), "white-space": keyword("nowrap")})
    block = match_block(doc)
    home = group("home-content", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)},
                 [title, nav, block])
    content = group("home", {**FULL, "transform": transform(), "opacity": number(1)}, [home])
    tall_arena_pause(doc, home, title, nav, block, (restart_modal, leave_modal))
    # The paused match, softened and never dimmed, as the single-player pause
    # shows its level (sections 9 and 13.7); the scrim stands in where the
    # view cannot soften (section 13.6).
    doc.state["soft_focus"] = {"type": "boolean", "initial": False, "cvar": "ui_retainedSoftFocus"}
    softened = group("scene-softfocus", {**FULL, "display": keyword("none"), "backdrop-blur": length(SOFT_FOCUS_BLUR),
                                         "backdrop-saturate": number(SOFT_FOCUS_SATURATION)})
    doc.bind("scene-softfocus.display", "scene-softfocus", "display", {"op": "select", "args": [{"state": "soft_focus"}, "block", "none"]})
    doc.bind("scrim.display", "scrim", "display", {"op": "select", "args": [{"state": "soft_focus"}, "none", "block"]})
    scrim = vector("scrim", {**FULL, "display": keyword("block"), "opacity": number(1)}, [
        path("dim", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid([0, 0, 0, 0.45])),
        path("lead", [(0, 0), (b.vx(413), 0), (b.vx(413), {"fraction": 1}), (0, {"fraction": 1})],
             fill=linear((b.vx(-107), 0), (b.vx(413), 0), [(0, [0, 0, 0, 0.7]), (0.55, [0, 0, 0, 0.35]), (1, [0, 0, 0, 0])])),
    ])
    prompts = [("#str_107019", "#str_200747"), ("#str_107020", RESUME_KEY)]
    doc.state["pause.released"] = {"type": "boolean", "initial": False}
    chrome = group("chrome", {**FULL, "display": keyword("block")}, [
        *b.lit_field("field", 0.45),
        *b.framing_bands("band"),
        content,
        b.prompt_bar(doc, prompts),
        restart_modal,
        leave_modal,
        group("fade", {**FULL, "background-color": colour([0, 0, 0, 0]), "pointer-events": keyword("none")}),
    ])
    doc.bind("chrome.display", "chrome", "display", {"op": "select", "args": [{"state": "pause.released"}, "none", "block"]})
    root = group("screen", {**FULL, "font-family": b.font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [softened, scrim, chrome])
    b.band_motion(doc, "band", "home", [])
    opening = next(timeline for timeline in doc.timelines.items if timeline["id"] == "open")
    opening["tracks"] += [
        track("scene-softfocus", "backdrop-blur", [(0, length(0)), (1, length(0)), (250, length(SOFT_FOCUS_BLUR))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(1)), (1, number(1)), (250, number(SOFT_FOCUS_SATURATION))]),
    ]
    opening["tracks"] += [track(node, "opacity", [(0, number(1)), (1, number(1)), (250, number(1))]) for node in ("prompts", "scrim")]
    # Each opening starts with no question asked: a match can end under an
    # open modal (the campaign restarts or leaves it).
    reset = {"op": "setState", "values": {"pause.released": False, "restartModal.visible": False, "restartModal.contents": False,
                                          "leaveModal.visible": False, "leaveModal.contents": False}}
    doc.events["open"] = [reset, *doc.events.get("open", [{"op": "playTimeline", "timeline": "open"}])]
    # Resume closes the card at once (section 8), but the softened view, or
    # the scrim standing in for it, releases over 250 ms as the modal backdrop
    # does (section 4); the session keeps drawing the closed card until then.
    doc.timelines.add("release", 250, [
        track("scene-softfocus", "backdrop-blur", [(0, length(SOFT_FOCUS_BLUR)), (250, length(0))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(SOFT_FOCUS_SATURATION)), (250, number(1))]),
        track("scrim", "opacity", [(0, number(1)), (250, number(0))]),
    ])
    doc.events["release"] = [{"op": "setState", "values": {"pause.released": True}}, {"op": "playTimeline", "timeline": "release"}]
    doc.timelines.add("unrelease", 250, [
        track("scene-softfocus", "backdrop-blur", [(0, length(0)), (1, length(0)), (250, length(SOFT_FOCUS_BLUR))]),
        track("scene-softfocus", "backdrop-saturate", [(0, number(1)), (1, number(1)), (250, number(SOFT_FOCUS_SATURATION))]),
        track("scrim", "opacity", [(0, number(1)), (1, number(1)), (250, number(1))]),
    ])
    doc.events["onActivate"] = [{"op": "if", "condition": {"state": "pause.released"},
                                 "then": [{"op": "setState", "values": {"pause.released": False}},
                                          {"op": "playTimeline", "timeline": "unrelease"}]}]
    return b.fit_narrow(doc.build(root))
