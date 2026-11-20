"""The retained Create Server sub-page (docs/dev/ui-visual-design.md sections 8
and 9, "Menu levels"; FLOW-050).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. CREATE SERVER on the Multiplayer page goes one
level deeper to guis/menu/createserver.rui as Single Player's CAMPAIGN goes to
Campaign: the page sweeps away, MULTIPLAYER becomes the crumb, the top band
steps one notch and the CREATE SERVER label carries to the step's foot, where
the sub-page presents at 350 ms (idSessionLocal::BeginRetainedSubpage). Back
climbs out again onto CREATE SERVER.

The content is the stock p_mp_create's, over the stock page backing: MAP
SELECTION over the chosen map's levelshot (183x137 u at 45,75 u in the 1 u
#414624 border) and the maps the chosen game type plays (42,219 u, 20 u rows,
a page of MAP_ROWS at a time); beside them, from 228,51 u, the settings under
the multiplayer card's tab strip, MATCH, RULES, MODIFIERS and SERVER, as
section 7 setting rows on the stock 24 u pitch, the value column 180 u in;
the enlarged CREATE SERVER plate at 244,397 u; the prompt bar in the bottom
band, its BACK where the stock BACK plate stood at 532,441 u.

The stock page showed eleven settings and hid the rest in ADVANCED OPTIONS, a
popup that grew four tabs of its own. The multiplayer menu tree audit (20
November 2026) put them all on the page under its tabs, the game type first,
the bots beside the player counts, and added the host settings later rounds
had left to the console (the map rotation and map requests, item timers,
team timeouts, the spectator lock and idle players).

The session owns every setting: it publishes create.* and each control asks
for a change through its own verb (RETAINED_CREATE_SETTINGS), which the
session checks against the CVar's range before it writes it. The game type
picks the limit row (Frag Limit, Capture Limit or Control Time) and whether
Tourney's round limit shows; the team rows lock outside team modes, as the
stock rows dimmed. The passwords are masked text fields, and VIEW LOCAL
SERVER BAN LIST opens PLAYER BAN LIST over the page. With nothing of the
stock page missing (RETAINED_CREATE_SERVER_MISSING is empty) the page is the
default.
"""
from __future__ import annotations

from build_retained_screens import (ACCEL, BOTTOM_TO_PAGE, CANVAS_W, CRUMB_SCALE, CRUMB_SLOT, CRUMB_SPAN, FULL, OLIVE, ORANGE,
                                    PAGE_TITLE_H, PAGE_TITLE_W, SUBPAGE_SLOT, TITLE_SLOT, TOP_TO_PAGE, TOP_TO_SUBPAGE, U, VALUE,
                                    Document, absolute, action_plate, by_language, colour, confirmation, crumb_fits,
                                    crumb_transform, framing_bands, group, keyword, label, length, linear, lit_field, number,
                                    objectives_scrollbar, page_prompts, page_title, path, rgb, solid, stroke, text_width, track,
                                    transform, typeface, vector)
from retained_difficulty import check_fits, primary_plate
from retained_mp_menus import match_number_row, row_face, row_states as setting_states, stack_row, value_row
from retained_saves import PLACEHOLDER, backing, bind_selected, grow_rows, pager_row, picture_frame, row_plates, row_states, table_text
from retained_subpage import subpage_document

# mp_create_val4's values in order, labelled by #str_200929's names; the
# session's RETAINED_CREATE_GAMETYPES lists the same.
GAMETYPES = ("DM", "Tourney", "Team DM", "CTF", "Arena CTF", "DeadZone", "Duel", "Clan Arena", "Freeze Tag", "Red Rover",
             "One Flag CTF", "Arena One Flag CTF")
GAMETYPE_NAMES = "#str_200929"
MAP_ROWS = 64                             # RETAINED_CREATE_MAP_ROWS in the session

# Section 9, measured from the stock p_mp_create (source units).
# mp_create_header stood from 37 u, under the stepped top band and over the
# page's title in its notch; it starts where the settings pages' strip does
# (STRIP_TOP, 46 u) and keeps its foot, the headings centred in what is left.
HEADER = (44.0, 46.0, 180.0, 28.0)
MAPS_HEAD = (58.0, 52.0, 185.0)           # mp_create_mapsel
SHOT = (45.0, 75.0, 183.0, 137.0)         # mp_create_mapshot
MAP_LIST = (42.0, 219.0, 186.0, 210.0)    # mp_create_maplist: 20 u items, the scrollbar in its last 16 u
MAP_ROW_H = 20.0
OPTIONS = (228.0, 73.0, 377.0)            # mp_create_b1-b12: 377 u on a 24 u pitch
ROW_PITCH = 24.0
VALUE_X = 180.0                           # mp_create_val*: 408 u, 180 u into the row
TABS_TOP = 51.0                           # u: the settings' tab strip, its baseline 2 u over the rows
START = (244.0, 397.0, 275.0, 37.0)       # mp_create_serverbtn
NAME_BYTES = 25                           # mp_create_val1: maxchars 25

# The setting rows: (state, verb, label, kind, minimum, maximum, message).
# A choice's entries come from CHOICES; a number row says `message` outside
# its range; a slider runs from its minimum to its maximum in whole steps.
# The session's RETAINED_CREATE_SETTINGS holds the same verbs, states and
# ranges. The stock page and its advanced popup held them on two levels; the
# page now holds every one of them under its tabs (CREATE_TABS).
SETTINGS = (
    ("lan", "createLan", "#str_200947", "choice", 0, 1, None),
    ("dedicated", "createDedicated", "#str_200053", "toggle", 0, 1, None),
    ("gametype", "createGameType", "#str_200042", "choice", 0, len(GAMETYPES) - 1, None),
    ("autobalance", "createAutoBalance", "#str_201008", "toggle", 0, 1, None),
    ("fraglimit", "createFragLimit", "#str_200060", "number", 0, 999, "#str_231123"),
    ("capturelimit", "createCaptureLimit", "#str_200215", "number", 1, 999, "#str_231124"),
    ("controltime", "createControlTime", "#str_222013", "number", 1, 999, "#str_231124"),
    ("timelimit", "createTimeLimit", "#str_200061", "slider", 0, 60, None),
    ("maxplayers", "createMaxPlayers", "#str_200046", "slider", 1, 16, None),
    ("minplayers", "createMinPlayers", "#str_201007", "slider", 1, 16, None),
    ("voting", "createVoting", "#str_201025", "toggle", 0, 1, None),
    ("teamdamage", "createTeamDamage", "#str_200058", "toggle", 0, 1, None),
    ("tourneylimit", "createTourneyLimit", "#str_200214", "number", 1, 999, "#str_231124"),
    ("usepass", "createUsePass", "#str_200067", "toggle", 0, 1, None),
    ("servermods", "createServerMods", "#str_200071", "toggle", 0, 1, None),
    ("weaponstay", "createWeaponStay", "#str_200967", "toggle", 0, 1, None),
    ("pure", "createPure", "#str_200073", "toggle", 0, 1, None),
    ("rate", "createRate", "#str_200074", "choice", 0, 5, None),
    ("ready", "createReady", "#str_200975", "toggle", 0, 1, None),
    ("spectators", "createSpectators", "#str_200064", "toggle", 0, 1, None),
    ("warmup", "createWarmup", "#str_200062", "toggle", 0, 1, None),
    ("voice", "createVoice", "#str_201021", "toggle", 0, 1, None),
    # Off, Quake 4's buy menu or The Awakening's (si_isBuyingEnabled and si_buyingRules).
    ("buying", "createBuying", "#str_231901", "choice", 0, 2, None),
    ("bots", "createBots", "#str_42210", "choice", 0, 16, None),
    ("botskill", "createBotSkill", "#str_42212", "choice", 1, 5, None),
    # openQ4's match rules a host sets most (si_shuffle, si_overtime,
    # si_mercyLimit, si_roundLimit), under the Match Control page's labels.
    ("shuffle", "createShuffle", "#str_201044", "toggle", 0, 1, None),
    ("overtime", "createOvertime", "#str_41642", "number", 0, 999, "#str_231123"),   # seconds
    ("mercy", "createMercy", "#str_41638", "number", 0, 999, "#str_231123"),
    ("roundlimit", "createRoundLimit", "#str_41404", "slider", 0, 60, None),
    # openQ4's Classic multiplayer ruleset (si_ruleset) and its grappling hook.
    ("ruleset", "createRuleset", "#str_42220", "choice", 0, 1, None),
    ("grapple", "createGrapple", "#str_42222", "choice", 0, 2, None),
    # Instagib (si_instagib): any game type with an unlimited railgun and one-hit kills.
    ("instagib", "createInstagib", "#str_231960", "toggle", 0, 1, None),
    # The modifier layer and presets (the game's mp/Modifiers.h and mp/Presets.h): the other
    # modifiers, the single-weapon arenas, the scales in percent and a preset that sets them together.
    ("quadhog", "createQuadHog", "#str_231962", "toggle", 0, 1, None),
    ("vampiric", "createVampiric", "#str_231964", "toggle", 0, 1, None),
    ("frenzy", "createFrenzy", "#str_231966", "toggle", 0, 1, None),
    ("weaponarena", "createWeaponArena", "#str_231968", "choice", 0, 10, None),
    ("powerups", "createPowerups", "#str_231971", "toggle", 0, 1, None),
    ("selfdamage", "createSelfDamage", "#str_231972", "number", 0, 999, "#str_231123"),
    ("knockback", "createKnockback", "#str_231973", "number", 0, 999, "#str_231123"),
    ("falldamage", "createFallDamage", "#str_231974", "number", 0, 999, "#str_231123"),
    ("weaponrespawn", "createWeaponRespawn", "#str_231975", "number", 0, 999, "#str_231123"),
    ("preset", "createPreset", "#str_231976", "choice", 0, 11, None),
    # The host settings later rounds added, which only the console set before
    # (the multiplayer menu tree audit, 20 November 2026): what follows a
    # finished match and whether players may queue maps (the map database's
    # g_mapRotation and g_mapQueue), who sees item timers (si_itemTimers),
    # casual team timeouts (si_timeouts), the spectator lock
    # (si_spectatorLock) and moving idle players to spectators (g_inactivity).
    ("maprotation", "createMapRotation", "#str_232600", "choice", 0, 3, None),
    ("mapqueue", "createMapQueue", "#str_232602", "toggle", 0, 1, None),
    ("itemtimers", "createItemTimers", "#str_232603", "choice", 0, 2, None),
    ("timeouts", "createTimeouts", "#str_41658", "slider", 0, 10, None),
    ("spectatorlock", "createSpectatorLock", "#str_232605", "choice", 0, 2, None),
    ("inactivity", "createInactivity", "#str_232607", "choice", 0, 600, None),
    # What players may show each other (MP-15): the strongest brightskin,
    # rimlight and outline they may put on others (si_maxBrightskin, by the
    # appearance rows' levels) and whether they may force others' models
    # (si_forcedModels).
    ("maxbrightskin", "createMaxBrightskin", "#str_232631", "choice", 0, 3, None),
    ("forcedmodels", "createForcedModels", "#str_232632", "toggle", 0, 1, None),
)
# The page's tabs over its settings column (the multiplayer menu tree audit,
# 20 November 2026): the stock page showed eleven rows and hid the rest in
# ADVANCED OPTIONS, a dialog of four tabs. Each tab's name and its rows, in
# order. MATCH holds what the match is: the game type first, its limits, who
# plays it and what map comes next; RULES how it is played; MODIFIERS what
# changes it; SERVER who may join and how the server runs. A row a game type
# does not use gives its place; one that cannot act on it dims.
CREATE_TABS = (
    ("match", "#str_231004", ("create-gametype", "create-fraglimit", "create-capturelimit", "create-controltime", "create-timelimit",
                              "create-tourneylimit", "create-roundlimit", "create-maxplayers", "create-minplayers", "create-bots",
                              "create-botskill", "create-warmup", "create-ready", "create-maprotation", "create-mapcycle",
                              "create-mapqueue")),
    ("rules", "#str_41704", ("create-ruleset", "create-grapple", "create-autobalance", "create-shuffle", "create-teamdamage",
                             "create-weaponstay", "create-buying", "create-overtime", "create-mercy", "create-itemtimers",
                             "create-maxbrightskin", "create-forcedmodels", "create-spectators", "create-spectatorlock",
                             "create-timeouts")),
    ("modifiers", "#str_231986", ("create-preset", "create-instagib", "create-quadhog", "create-vampiric", "create-frenzy",
                                  "create-weaponarena", "create-powerups", "create-weaponrespawn", "create-selfdamage",
                                  "create-knockback", "create-falldamage")),
    ("server", "#str_231007", ("create-name", "create-lan", "create-dedicated", "create-usepass", "create-password",
                               "create-rconpassword", "create-voting", "create-voice", "create-inactivity", "create-pure",
                               "create-servermods", "create-rate", "create-banlist")),
)
TAB_ROWS = 13           # the rows the column shows at once; a longer tab scrolls
# Each choice's stock list and its values.
CHOICES = {
    "lan": ("#str_200056", (0, 1)),
    "gametype": (GAMETYPE_NAMES, tuple(range(len(GAMETYPES)))),
    "rate": ("#str_200179", (0, 1, 2, 3, 4, 5)),
    "bots": ("#str_42211", (0, *range(2, 17))),
    "botskill": ("#str_42213", (1, 2, 3, 4, 5)),
    "buying": (("#str_41200", "#str_41584", "#str_231900"), (0, 1, 2)),
    "ruleset": ("#str_42221", (0, 1)),
    "grapple": ("#str_42223", (0, 1, 2)),
    # Off, then Quake 4's own weapon names in the game's arena order (RETAINED_CREATE_WEAPON_ARENAS).
    "weaponarena": (("#str_41200", "#str_200106", "#str_200105", "#str_200107", "#str_200108", "#str_200109", "#str_200110",
                     "#str_200111", "#str_200112", "#str_200113", "#str_222001"), tuple(range(11))),
    # Custom, then the built-in presets in the game's order (RETAINED_CREATE_PRESETS).
    "preset": (("#str_231977", "#str_231980", "#str_231960", "#str_231962", "#str_231964", "#str_231966", "#str_231981",
                "#str_231982", "#str_231983", "#str_231988", "#str_231984", "#str_231985"), tuple(range(12))),
    "maprotation": ("#str_232601", (0, 1, 2, 3)),
    "itemtimers": ("#str_232604", (0, 1, 2)),
    "spectatorlock": ("#str_232606", (0, 1, 2)),
    # g_inactivity in seconds: off, one, two, five or ten minutes.
    "inactivity": ("#str_232608", (0, 60, 120, 300, 600)),
    # Off, Low, Medium or High (no limit), as the appearance rows name them.
    "maxbrightskin": ("#str_41207", (0, 1, 2, 3)),
}
# The game types each row serves, by index: the limit row's three, Tourney's
# round limit, and the team modes in which Auto Balance and Team Damage apply
# (the stock gametypeChange event).
FRAG_TYPES = (0, 1, 2, 6, 7, 8, 9)
CAPTURE_TYPES = (3, 4, 10, 11)
CONTROL_TYPES = (5,)
TEAM_TYPES = (2, 3, 4, 5, 7, 8, 9, 10, 11)
TOURNEY_TYPES = (1,)
ROUND_TYPES = (7, 8, 9)       # the round modes, which end on si_roundLimit (GTF_ROUNDLIMIT)

MAPCYCLE_BYTES = 200                       # pop_createAdv_mapcycle_val: maxchars 200
# The password rows after Use password, as the stock popup orders them: the
# game's password (shown only while Use password is on) and the remote console
# password. Each is masked, may be empty and takes 16 bytes (maxchars 16).
PASSWORDS = (
    ("password", "createPassword", "#str_200068", "passwordEntered"),
    ("rconpassword", "createRconPassword", "#str_200069", "rconPasswordEntered"),
)
PASSWORD_BYTES = 16
# PLAYER BAN LIST (stock pop_p_createAdv_banlist): the game's ban list under
# NAME and GUID, BAN_ROWS rows at most on the stock 20 u pitch, REMOVE BAN and
# CLOSE where YES and NO stand; the MODS dialog's construction, as wide as
# the stock ADVANCED OPTIONS popup (BAN_MIN_WIDTH) or wider where a
# language's REMOVE BAN or CLOSE needs it,
# the list 22 u in from each side and the GUID column at its half, as the
# stock list's tab stops halved it.
BAN_ROWS = 32                              # RETAINED_CREATE_BAN_ROWS in the session
BAN_TOP, BAN_HEIGHT = 83.0, 280.0          # u
BAN_LIST_LEFT, BAN_LIST_TOP, BAN_LIST_H = 22.0, 25.0, 195.0
BAN_ROW_H = 20.0
BAN_MIN_WIDTH = 428.0                      # u: the stock pop_createAdv the list opened over


def setting_action(doc: Document, verb: str, kind: str) -> str:
    """The verb a row's control asks the session with, carrying its value."""
    doc.actions[verb] = {"operation": "session.menuValue", "input": "boolean" if kind == "toggle" else "number",
                         "arguments": {"command": verb, "value": {"input": "value"}}}
    return verb


def among(types: tuple) -> dict:
    """True while the chosen game type is one of `types`."""
    chosen = {"state": "create.gametype"}
    expression = {"op": "==", "args": [chosen, types[0]]}
    for index in types[1:]:
        expression = {"op": "||", "args": [expression, {"op": "==", "args": [chosen, index]}]}
    return expression


def text_row(doc: Document, ident: str, label_key: str, width: float, value_x: float, state: str, event: str, max_bytes: int,
             row_h: float, allow_empty: bool = False, masked: bool = False, caption: bool = True) -> dict:
    """A text setting (section 7 with the Save page's text field): the label in
    its column and the field in the value column, which Enter commits into the
    page's own `state` and then runs `event`, which asks the session to take it.
    `allow_empty` lets the text be empty; `masked` shows it as one * a character.
    Without `caption` the row shows no label (an entry dialog's lone field,
    which its title names); `label_key` still names it for assistive output."""
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)]
    plate = vector(f"{ident}-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1))])
    wash = vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
        path("wash", shape, fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))]))])
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=row_h), "opacity": number(0)}, [
        path("rail", [(0, 7), (6, 1), (6, row_h - 1), (0, row_h - 1)], fill=solid(rgb(ORANGE)))])
    caption_node = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                         "min-width": length(0), **row_face(False, row_h), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption_node])
    field_w, field_h = width - value_x - 12, 22.0
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    ink = lambda name, tint, w, h: vector(name, {**absolute(left=0, top=0, width=w, height=h), "display": keyword("block"),
                                                 "opacity": number(1), "pointer-events": keyword("none")},
                                          [path("ink", whole, fill=solid(tint))])
    parts = {part: f"{ident}-{part}" for part in ("viewport", "text", "selection", "caret", "composition", "validation")}
    field_plate = vector(f"{ident}-field", {**FULL, "pointer-events": keyword("none")},
                         [path("field", whole, fill=solid([0, 0, 0, 0.6]), stroke=stroke(solid(rgb(OLIVE, 0.45)), 1))])
    text_node = label(parts["text"], PLACEHOLDER, {**absolute(left=6, top=0, width=4096, height=field_h),
                      **typeface("lowpixel", 15, field_h, rgb(VALUE)), "white-space": keyword("pre"), "text-align": keyword("left")})
    viewport = group(parts["viewport"], {**absolute(left=value_x - 8, top=(row_h - field_h) / 2, width=field_w, height=field_h),
                                         "display": keyword("block"), "overflow": keyword("hidden")},
                     [field_plate, ink(parts["selection"], rgb(ORANGE, 0.4), 0, field_h), text_node,
                      ink(parts["caret"], [1, 1, 1, 1], 1, field_h), ink(parts["composition"], rgb(ORANGE), 0, 1)])
    validation = label(parts["validation"], PLACEHOLDER, {**absolute(left=21, top=row_h + 1, width=width - 33, height=16),
                       **typeface("lowpixel", 13, 16, rgb(OLIVE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                       "display": keyword("none")})
    action = f"enter.{ident}"
    doc.actions[action] = {"input": "string", "operation": "ui.state.set", "arguments": {"key": state, "value": {"input": "value"},
                                                                                         "event": event}}
    control = {"role": "text", "label": label_key, "action": action, "value": {"state": state}, "maxBytes": max_bytes,
               **({"allowEmpty": True} if allow_empty else {}), **({"masked": True} if masked else {}), "parts": parts,
               "states": setting_states(doc, ident, caption)}
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(width), "height": length(row_h),
                         "opacity": number(1), "flex-shrink": number(0)}, [plate, wash, rail, *([head] if caption else []), viewport, validation],
                 control=control)


def setting_rows(doc: Document, settings: tuple, width: float, value_x: float, row_h: float, controls: dict, placement: str) -> list:
    """The rows of `settings` (see SETTINGS), each reading create.<state> and
    asking its verb, a choice's list unfolding within `placement`; `controls`
    gets each row's node id by state."""
    rows = []
    for state, verb, label_key, kind, minimum, maximum, message in settings:
        key = f"create.{state}"
        doc.state[key] = {"type": "boolean", "initial": False} if kind == "toggle" else {"type": "number", "initial": minimum}
        action = setting_action(doc, verb, "number" if kind == "slider" else kind)
        ident = f"create-{state}"
        if kind == "number":
            row = match_number_row(doc, ident, label_key, width, value_x, value={"state": key}, action=action, maximum=maximum,
                                   message=message, minimum=minimum, row_h=row_h)
        elif kind == "slider":
            row = value_row(doc, ident, label_key, "slider", width, value_x, value={"state": key}, action=action,
                            slider=(minimum, maximum, 1, 0), row_h=row_h, wrap=True)
        else:
            entries = None
            if kind == "choice":
                names, values = CHOICES[state]
                # A stock list by index, or each option's own key.
                entries = [(names[index] if isinstance(names, tuple) else (names, index), value) for index, value in enumerate(values)]
            row = value_row(doc, ident, label_key, kind, width, value_x, value={"state": key}, action=action, entries=entries,
                            row_h=row_h, placement=placement, wrap=True)
        rows.append(row)
        controls[state] = ident
    return rows


def show_when(doc: Document, row: dict, ident: str, expression: dict) -> None:
    """The row takes its slot only while `expression` holds."""
    row["properties"]["display"] = keyword("none")
    doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [expression, "block", "none"]})


def lock_unless(doc: Document, ident: str, expression: dict) -> None:
    """The row dims and takes no input unless `expression` holds."""
    doc.bind(f"{ident}.enabled", ident, "enabled", expression)
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [expression, 1, 0.38]})


def header_band(ident: str, left: float, top: float, width: float, height: float) -> dict:
    """The header band (header, Appendix B.1): an upper-leading cut of about
    9 u, top and leading rails, the fill at 0.49 to half the width, half at
    75 % and 10 % at 95 %, olive at 0.60."""
    w, h, cut = U * width, U * height, U * 9
    fill = [(0, rgb(OLIVE, 0.49)), (0.5, rgb(OLIVE, 0.49)), (0.75, rgb(OLIVE, 0.245)), (0.95, rgb(OLIVE, 0.049)), (1, rgb(OLIVE, 0))]
    rail = [(0, rgb(OLIVE)), (0.5, rgb(OLIVE)), (0.75, rgb(OLIVE, 0.5)), (0.95, rgb(OLIVE, 0.1)), (1, rgb(OLIVE, 0))]
    return vector(ident, {**absolute(left=U * left, top=U * top, width=w, height=h), "opacity": number(0.6), "pointer-events": keyword("none")}, [
        path("fill", [(cut, 0), (w, 0), (w, h), (0, h), (0, cut)], fill=linear((0, 0), (w, 0), fill)),
        path("rail", [(0.75, h), (0.75, cut), (cut, 0.75), (w, 0.75)], closed=False, stroke=stroke(linear((0, 0), (w, 0), rail), 1.5)),
    ])


def map_row(doc: Document, index: int, row_w: float) -> dict:
    """One of the map list's rows: the map's name, Lowpixel at the stock list's
    textscale 0.22 (15.8 dp) on its 20 u pitch. Its first Accept chooses the
    map; Accept on the chosen map moves on to CREATE SERVER."""
    ident, key = f"map-{index}", f"create.map{index}"
    doc.state[key] = {"type": "string", "initial": ""}
    width, height = U * row_w, U * MAP_ROW_H
    tint = [1, 1, 1, 0.8]
    name = label(f"{ident}-name", PLACEHOLDER, {**absolute(left=U * 4, top=0, width=width - U * 8, height=height),
                 **typeface("lowpixel", 15.8, height, tint), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                 "pointer-events": keyword("none")})
    doc.bind(f"{ident}-name.text", f"{ident}-name", "text", {"state": key})
    chosen = {"op": "+", "args": [{"state": "create.map_first"}, index]}
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [chosen, {"state": "create.map_count"}]}, "block", "none"]})
    bind_selected(doc, ident, {"op": "==", "args": [{"state": "create.map_selected"}, chosen]})
    event = f"mapRow{index}"
    doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": "create.map_selected"}, chosen]},
                          "then": [{"op": "focus", "control": "create_start"}],
                          "else": [{"op": "setState", "values": {"create.pick": index}}, {"op": "action", "action": "createMap"}]}]
    ids = row_states(doc, ident, (f"{ident}-name",), (tint,))
    return group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                         "flex-shrink": number(0)}, [*row_plates(ident, width, height, selectable=True), name],
                 control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids})


def password_row(doc: Document, state: str, verb: str, label_key: str, event: str, width: float, value_x: float, row_h: float) -> dict:
    """A password row: a masked text field that may be empty, committing into
    create.<state> and asking `verb`, which the session takes as its CVar."""
    doc.state[f"create.{state}"] = {"type": "string", "initial": ""}
    doc.session(verb, verb)
    doc.events[event] = [{"op": "action", "action": verb}]
    return text_row(doc, f"create-{state}", label_key, width, value_x, f"create.{state}", event, PASSWORD_BYTES,
                    row_h, allow_empty=True, masked=True)


def link_row(doc: Document, ident: str, label_key: str, width: float, row_h: float, event: str) -> dict:
    """A row that opens a dialog (the stock popup's VIEW LOCAL SERVER BAN LIST):
    a setting row's plate, label and feedback with a chevron pointing on in
    the value column's end, the whole row one button running `event`."""
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}), (0, {"fraction": 1, "dp": -1}), (0, 7)]
    plate = vector(f"{ident}-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, 0.12)), stroke=stroke(solid(rgb(OLIVE, 0.32)), 1))])
    wash = vector(f"{ident}-wash", {**FULL, "opacity": number(0)}, [
        path("wash", shape, fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.45)), (1, rgb(OLIVE, 0.1))]))])
    rail = vector(f"{ident}-focus", {**absolute(left=0, top=0, width=4, height=row_h), "opacity": number(0)}, [
        path("rail", [(0, 7), (6, 1), (6, row_h - 1), (0, row_h - 1)], fill=solid(rgb(ORANGE)))])
    caption = label(f"{ident}-label", label_key, {**absolute(left=21, top=0, width=width - 60, height=row_h), **row_face(False, row_h),
                    "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    chevron = vector(f"{ident}-chevron", absolute(left=width - 21, top=(row_h - 12) / 2, width=6, height=12), [
        path("chevron", [(0, 0), (6, 6), (0, 12)], closed=False, stroke=stroke(solid(rgb(VALUE)), 1.5))])
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(width), "height": length(row_h),
                         "opacity": number(1), "flex-shrink": number(0)}, [plate, wash, rail, caption, chevron],
                 control={"role": "button", "label": label_key, "event": event, "states": setting_states(doc, ident)})


def ban_dialog_size() -> tuple[float, float]:
    """PLAYER BAN LIST's width and its action plates' (dp): each plate takes
    the longest REMOVE BAN or CLOSE of any language and 50 dp, at least the
    stock 180 dp, and the dialog both plates beside each other with its 30 and
    33 dp margins and a 12 dp gap, at least the stock popup's width."""
    longest = max(text_width("marine", text, 20) for key in ("#str_200922", "#str_200923") for text in table_text(key).values())
    action_w = round(max(180.0, longest + 50), 1)
    return round(max(U * BAN_MIN_WIDTH, 2 * action_w + 30 + 33 + 12), 1), action_w


def ban_list_modal(doc: Document, prefix: str = "create", pick_key: str | None = None, pick_verb: str = "createBanPick",
                   remove_verb: str = "createBanRemove") -> dict:
    """PLAYER BAN LIST (section 6, the list dialog as MODS builds it): the
    game's ban list under NAME and GUID headings and an olive rule, on 20 u
    rows with the list's light and a scrollbar, or a line saying there is
    none; REMOVE BAN and CLOSE stand where YES and NO do. The session
    publishes the list before the dialog shows (create.ban*) with no row
    chosen. A row's first Accept chooses it, Accept on the chosen row moves on
    to REMOVE BAN, which removes its ban; CLOSE and Back return to VIEW LOCAL
    SERVER BAN LIST. The multiplayer card's Admin page shows the same dialog
    over its own list: `prefix` names the list's state, `pick_key` the row a
    row's Accept names and the two verbs choose a ban and remove it."""
    pick_key = pick_key or f"{prefix}.ban_pick"
    doc.state.update({f"{prefix}.ban_count": {"type": "number", "initial": 0}, f"{prefix}.ban_selected": {"type": "number", "initial": -1},
                      pick_key: {"type": "number", "initial": -1}})
    doc.session(pick_verb, pick_verb)
    doc.session(remove_verb, remove_verb)
    width_dp, action_w = ban_dialog_size()
    left, top, list_h = BAN_LIST_LEFT, BAN_LIST_TOP, BAN_LIST_H
    list_w = width_dp / U - 2 * left
    guid_x = round(list_w / 2, 1)
    heading = {**typeface("lowpixel", 13, U * 12, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    parts = [
        label("ban-head-name", "#str_200242", {**absolute(left=U * left, top=U * 12, width=U * (guid_x - 4), height=U * 12), **heading}),
        label("ban-head-guid", "#str_200924", {**absolute(left=U * (left + guid_x), top=U * 12,
              width=U * (list_w - guid_x), height=U * 12), **heading}),
        vector("ban-rule", absolute(left=U * left, top=U * top - 0.6, width=U * list_w, height=1.2), [
            path("rule", [(0, 0.6), ({"fraction": 1}, 0.6)], closed=False, stroke=stroke(solid(rgb(OLIVE, 0.26)), 1.2))]),
        label("ban-empty", "#str_231130", {**absolute(left=U * left, top=U * (top + 8), width=U * list_w, height=U * 16),
              **typeface("lowpixel", 15, U * 16, [1, 1, 1, 0.6]), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
              "display": keyword("block")}),
    ]
    doc.bind("ban-empty.display", "ban-empty", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": f"{prefix}.ban_count"}, 0]}, "block", "none"]})
    rows = []
    width, height = U * (list_w - 16), U * BAN_ROW_H
    cell = {"white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")}
    tints = ([1, 1, 1, 0.9], [1, 1, 1, 0.65])
    for index in range(BAN_ROWS):
        ident, key = f"ban-row-{index}", f"{prefix}.ban{index}"
        cells = []
        for column, left_u, column_w, tint in (("name", 4, guid_x - 8, tints[0]),
                                               ("guid", guid_x, list_w - 16 - guid_x - 4, tints[1])):
            doc.state[f"{key}.{column}"] = {"type": "string", "initial": ""}
            node = f"{ident}-{column}"
            cells.append(label(node, PLACEHOLDER, {**absolute(left=U * left_u, top=0, width=U * column_w, height=height),
                                                  **typeface("lowpixel", 15.8, height, tint), **cell}))
            doc.bind(f"{node}.text", node, "text", {"state": f"{key}.{column}"})
        doc.bind(f"{ident}.display", ident, "display",
                 {"op": "select", "args": [{"op": "<", "args": [index, {"state": f"{prefix}.ban_count"}]}, "block", "none"]})
        bind_selected(doc, ident, {"op": "==", "args": [{"state": f"{prefix}.ban_selected"}, index]})
        event = f"banRow{index}"
        doc.events[event] = [{"op": "if", "condition": {"op": "==", "args": [{"state": f"{prefix}.ban_selected"}, index]},
                              "then": [{"op": "focus", "control": "banModal_yes"}],
                              "else": [{"op": "setState", "values": {pick_key: index}}, {"op": "action", "action": pick_verb}]}]
        ids = row_states(doc, ident, (f"{ident}-name", f"{ident}-guid"), tints)
        rows.append(group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width), "height": length(height),
                                  "flex-shrink": number(0)}, [*row_plates(ident, width, height, selectable=True), *cells],
                          control={"role": "button", "label": PLACEHOLDER, "event": event, "states": ids}))
    column_node = group("ban-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")}, rows)
    parts.append(group("ban-list", {**absolute(left=U * left, top=U * top, width=width, height=U * list_h), "overflow": keyword("auto")},
                       [column_node]))
    parts.append(objectives_scrollbar(doc, "ban_scroll", "ban-list", OLIVE, ORANGE, U * (left + list_w) - 24, U * top, U * list_h,
                                      line_step=U * BAN_ROW_H, key="#str_200921"))
    modal = confirmation(doc, "banModal", "#str_200921", PLACEHOLDER, None, yes_event="banRemoveConfirmed", wide_title=True,
                         height_u=BAN_HEIGHT, top_u=BAN_TOP, actions_u=BAN_HEIGHT - 40, contents=parts,
                         yes_key="#str_200922", no_key="#str_200923", width_dp=width_dp, action_w=action_w)
    doc.events["banRemoveConfirmed"] = [{"op": "if", "condition": {"op": ">=", "args": [{"state": f"{prefix}.ban_selected"}, 0]},
                                         "then": [{"op": "action", "action": remove_verb}]}]
    doc.bind("banModal_yes.enabled", "banModal_yes", "enabled", {"op": ">=", "args": [{"state": f"{prefix}.ban_selected"}, 0]})
    return modal


def settings_tabs(doc: Document, width: float, value_x: float, row_h: float) -> tuple[list, dict]:
    """The settings column under its tab strip (the multiplayer menu tree
    audit, 20 November 2026): every setting of the stock page and its
    ADVANCED OPTIONS popup, with the password and map cycle text fields and
    VIEW LOCAL SERVER BAN LIST, each row shown on its tab alone
    (CREATE_TABS). The strip is the multiplayer card's (retained_mp_menus.
    tab_strip): the shown tab rises in the tab green, the Q and E keycaps at
    its ends, or a controller's shoulder buttons, step through the tabs and
    take the focus to the tab shown, and each opening starts on MATCH.

    Rows follow what they act on: the limit row is the chosen game type's
    (Frag Limit, Capture Limit or Control Time), Tourney's round limit
    shows in Tourney alone and the Round Limit in the round modes (Clan
    Arena, Freeze Tag, Red Rover); the team rows (Auto Balance, Shuffle Teams, Team
    Damage, Timeouts per Team) dim outside team modes; Set password dims and
    hides its text while Use password is off; the grappling hook dims under
    Quake 4's ruleset; the map cycle script's name shows only while Map
    Rotation runs it; the spectator lock dims while spectators are off; a
    fixed loadout (Instagib, a weapon arena) dims the rules it overrides.
    Returns the strip's nodes and the column."""
    controls = {}
    rows = setting_rows(doc, SETTINGS, width, value_x, row_h, controls, "screen")
    by_state = {state: row for (state, *_), row in zip(SETTINGS, rows)}
    doc.events["nameEntered"] = [{"op": "action", "action": "createName"}]
    rows.append(text_row(doc, "create-name", "#str_200052", width, value_x, "create.name", "nameEntered", NAME_BYTES, row_h))
    rows += [password_row(doc, *password, width, value_x, row_h) for password in PASSWORDS]
    doc.state["create.mapcycle"] = {"type": "string", "initial": ""}
    doc.session("createMapCycle", "createMapCycle")
    doc.events["mapCycleEntered"] = [{"op": "action", "action": "createMapCycle"}]
    rows.append(text_row(doc, "create-mapcycle", "#str_200072", width, value_x, "create.mapcycle", "mapCycleEntered",
                         MAPCYCLE_BYTES, row_h, allow_empty=True))
    # VIEW LOCAL SERVER BAN LIST asks for the list, then opens it over the
    # page; its CLOSE comes back to this row.
    doc.session("createBanList", "createBanList")
    doc.events["banListOpen"] = [{"op": "action", "action": "createBanList"}, {"op": "call", "event": "banModalShow"}]
    rows.append(link_row(doc, "create-banlist", "#str_200948", width, row_h, "banListOpen"))

    # What each row acts on.
    team, tourney = among(TEAM_TYPES), among(TOURNEY_TYPES)
    for state, types in (("fraglimit", FRAG_TYPES), ("capturelimit", CAPTURE_TYPES), ("controltime", CONTROL_TYPES)):
        show_when(doc, by_state[state], controls[state], among(types))
    show_when(doc, by_state["tourneylimit"], controls["tourneylimit"], tourney)
    show_when(doc, by_state["roundlimit"], controls["roundlimit"], among(ROUND_TYPES))
    script = {"op": "==", "args": [{"state": "create.maprotation"}, 3]}
    mapcycle = next(row for row in rows if row["id"] == "create-mapcycle")
    show_when(doc, mapcycle, "create-mapcycle", script)
    for state in ("autobalance", "shuffle", "teamdamage", "timeouts"):
        lock_unless(doc, controls[state], team)
    using = {"state": "create.usepass"}
    lock_unless(doc, "create-password", using)
    doc.bind("create-password-viewport.display", "create-password-viewport", "display", {"op": "select", "args": [using, "block", "none"]})
    lock_unless(doc, "create-grapple", {"op": "==", "args": [{"state": "create.ruleset"}, 1]})
    lock_unless(doc, "create-spectatorlock", {"state": "create.spectators"})
    # A fixed loadout (Instagib, a weapon arena) spawns no weapons and sells
    # nothing, so Weapon Stay, buying and Weapons Frenzy have nothing to act
    # on while one is on. Instagib also switches off the rules it overrides,
    # and Quad Hog is a Deathmatch rule (rvMPModifiers::Resolve in the game).
    not_instagib = {"op": "!", "args": [{"state": "create.instagib"}]}
    free_loadout = {"op": "&&", "args": [not_instagib, {"op": "==", "args": [{"state": "create.weaponarena"}, 0]}]}
    for ident in ("create-weaponstay", "create-buying", "create-frenzy"):
        lock_unless(doc, ident, free_loadout)
    for ident in ("create-vampiric", "create-weaponarena", "create-powerups"):
        lock_unless(doc, ident, not_instagib)
    lock_unless(doc, "create-quadhog", {"op": "&&", "args": [not_instagib, {"op": "==", "args": [{"state": "create.gametype"}, 0]}]})

    # The rows in their tabs' order, each shown on its tab alone.
    by_id = {row["id"]: row for row in rows}
    assert sorted(by_id) == sorted(ident for _, _, idents in CREATE_TABS for ident in idents), sorted(by_id)
    rows = [by_id[ident] for _, _, idents in CREATE_TABS for ident in idents]
    doc.state["create.tab"] = {"type": "number", "initial": 0}
    for tab, (_name, _key, idents) in enumerate(CREATE_TABS):
        on_tab = {"op": "==", "args": [{"state": "create.tab"}, tab]}
        for ident in idents:
            # A row its game type or another setting shows keeps that
            # condition on its tab; every other row shows on its tab alone.
            own = next((binding for binding in doc.bindings if binding["node"] == ident and binding["property"] == "display"), None)
            if own is not None:
                own["value"] = {"op": "select", "args": [on_tab, own["value"], "none"]}
                by_id[ident]["properties"]["display"] = keyword("none")
                continue
            shown = by_id[ident]["properties"].get("display", keyword("block"))["value"]
            by_id[ident]["properties"]["display"] = keyword(shown if tab == 0 else "none")
            doc.bind(f"{ident}.tab", ident, "display", {"op": "select", "args": [on_tab, shown, "none"]})
    from retained_mp_menus import tab_strip
    shows = lambda tab: {"op": "==", "args": [{"state": "create.tab"}, tab]}  # noqa: E731
    count = len(CREATE_TABS)
    focus_shown = [{"op": "if", "condition": shows(tab), "then": [{"op": "focus", "control": f"tab-{name}"}]}
                   for tab, (name, _key, _idents) in enumerate(CREATE_TABS)]
    for tab, (name, _key, _idents) in enumerate(CREATE_TABS):
        doc.events[f"tab_{name}"] = [{"op": "setState", "values": {"create.tab": tab}}]
    for event, step in (("onTabNext", 1), ("onTabPrevious", -1)):
        target = (count - 1 + step) % count
        for tab in reversed(range(count - 1)):
            target = {"op": "select", "args": [shows(tab), (tab + step) % count, target]}
        doc.events[event] = [{"op": "setState", "values": {"create.tab": target}}, *focus_shown]
    strip = tab_strip(doc, width, [(name, key, None) for name, key, _idents in CREATE_TABS], top=0, state="create.tab")
    # Above 100 % text each row's value stands under its label (section 2's
    # reflow) and the rows scroll in the column's room.
    for row in rows:
        if row["control"]["role"] != "button":
            stack_row(doc, row, width, value_x, row_h)
    column = group("create-options", {**absolute(left=U * OPTIONS[0], top=U * OPTIONS[1], width=width, height=row_h * TAB_ROWS),
                                      "display": keyword("flex"), "flex-direction": keyword("column"), "overflow": keyword("auto")},
                   rows)
    return strip, column


def check_create_fits() -> None:
    """Every shipped language's labels fit: the headings and the plates'
    labels on one line, a row's label in its column on two lines, and a
    choice's names in its value column; the number rows check their own
    messages and the tab strip its tabs."""
    one_line = [("#str_200050", "lowpixel", 14, U * MAPS_HEAD[2]), ("#str_200026", "marine", 23.7, U * 215),
                ("#str_231127", "marine", 16, U * (MAP_LIST[2] - 16 - 18)),
                ("#str_231128", "marine", 16, U * (MAP_LIST[2] - 16 - 18))]
    # VIEW LOCAL SERVER BAN LIST in its row, and PLAYER BAN LIST's title (the
    # wide title slot) and empty line.
    ban_w, _ = ban_dialog_size()
    one_line += [("#str_200948", "lowpixel", 15, U * OPTIONS[2] - 60), ("#str_231130", "lowpixel", 15, ban_w - 2 * U * BAN_LIST_LEFT),
                 ("#str_200921", "marine", 20, ban_w * (1 - 7 / 512) - 6 - (ban_w * 14 / 512 + 17))]
    check_fits(tuple(one_line), ())
    from retained_mp_menus import wrapped_width
    labels = [key for _, _, key, *_ in SETTINGS] + ["#str_200052", "#str_200072", *(key for _, _, key, _ in PASSWORDS)]
    over = {(key, language): round(wrapped_width(text)) for key in labels for language, text in table_text(key).items()
            if wrapped_width(text) > U * VALUE_X - 29}
    if over:
        raise SystemExit(f"Create Server labels past their two lines: {over}")
    value_w = (U * OPTIONS[2] - U * VALUE_X - 12) - 14
    for state, (names, values) in CHOICES.items():
        if isinstance(names, tuple):
            texts = {}
            for key in names:
                for language, text in table_text(key).items():
                    texts.setdefault(language, []).append(text)
            lists = {language: ";".join(items) for language, items in texts.items()}
        else:
            lists = table_text(names)
        for language, text in lists.items():
            items = text.split(";")
            if len(items) < len(values):
                raise SystemExit(f"{names} in {language} lists {len(items)} of {len(values)} names")
            for item in items[:len(values)]:
                if text_width("lowpixel", item, 14) > value_w:
                    raise SystemExit(f"{names} in {language}: {item!r} needs {text_width('lowpixel', item, 14):.0f} dp of {value_w:.0f}")


def create_server_document() -> dict:
    """The Create Server sub-page in the sub-page state (sections 8 and 9): the
    top band one notch on, the bottom band docked, MULTIPLAYER as the crumb
    and CREATE SERVER at the step's foot; MAP SELECTION, the chosen map's
    levelshot over the maps the chosen game type plays, beside the settings
    under their MATCH, RULES, MODIFIERS and SERVER tabs; CREATE SERVER under
    them, over the stock page backing.
    onActivate    every arrival: the content and backing fade in over 150 ms
                  (content.in), MATCH shows and CREATE SERVER takes the focus;
    subpageLeave  Back, 300 ms: its title drops at once, it sweeps 640 u toward
                  the leading edge with its content fading over 250 ms, the
                  top band steps back at once and the crumb returns to the
                  title slot over the last 150 ms;
    onDeactivate  places it at rest while it is hidden."""
    check_create_fits()
    doc = Document("openq4.createserver")
    doc.session("back", "createBack")
    doc.session("createMap", "createMap")
    doc.session("createStart", "createStart")
    doc.session("createName", "createName")
    doc.session("mapsNewer", "createMapsNewer")
    doc.session("mapsOlder", "createMapsOlder")
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    doc.state.update({
        "create.gametype": {"type": "number", "initial": 0},
        "create.map_count": {"type": "number", "initial": 0},
        "create.map_first": {"type": "number", "initial": 0},
        "create.map_selected": {"type": "number", "initial": -1},
        "create.pick": {"type": "number", "initial": -1},
        "create.shot": {"type": "string", "initial": ""},
        "create.name": {"type": "string", "initial": ""},
    })

    # The map selection: the chosen map's levelshot over the list.
    shot = picture_frame("create-shot", *SHOT, fit="fill")
    doc.bind("create-shot.image", "create-shot", "image", {"state": "create.shot"})
    heading = {**typeface("lowpixel", 14, U * 16, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    headings = [
        header_band("create-header", *HEADER),
        label("create-head-maps", "#str_200050", {**absolute(left=U * MAPS_HEAD[0], top=U * MAPS_HEAD[1], width=U * MAPS_HEAD[2],
                                                             height=U * 16), **heading}),
    ]
    left, top, list_w, list_h = MAP_LIST
    row_w = list_w - 16
    newer = pager_row(doc, "maps-newer", "#str_231127", "mapsNewer", {"op": ">", "args": [{"state": "create.map_first"}, 0]}, row_w=row_w)
    older = pager_row(doc, "maps-older", "#str_231128", "mapsOlder",
                      {"op": "<", "args": [{"op": "+", "args": [{"state": "create.map_first"}, MAP_ROWS]}, {"state": "create.map_count"}]},
                      row_w=row_w)
    maps = group("maps-column", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column")},
                 [newer, *[map_row(doc, index, row_w) for index in range(MAP_ROWS)], older])
    # Above 100 % text the maps' rows grow with it (section 2's reflow).
    grow_rows(doc, maps)
    viewport = group("maps-list", {**absolute(left=U * left, top=U * top, width=U * row_w, height=U * list_h), "overflow": keyword("auto")},
                     [maps])
    scrollbar = objectives_scrollbar(doc, "maps_scroll", "maps-list", OLIVE, ORANGE, U * (left + list_w) - 16, U * top, U * list_h,
                                     line_step=U * MAP_ROW_H, key="#str_200050")

    # The settings under their tabs, the strip in the header's room.
    strip, column = settings_tabs(doc, U * OPTIONS[2], U * VALUE_X, U * ROW_PITCH)
    tabs = group("create-tabs", absolute(left=U * OPTIONS[0], top=U * TABS_TOP, width=U * OPTIONS[2], height=U * (OPTIONS[1] - TABS_TOP)), strip)
    start = primary_plate(doc, "create_start", "#str_200026", *START, action="createStart", label_box=(43.0, 6.0, 215.0, 25.0), text=23.7,
                          marker=(13.0, 9.5, 18.0))
    back = page_prompts(doc, "create_back", action="back")

    # PLAYER BAN LIST stands over the page, and closing it returns the focus
    # to VIEW LOCAL SERVER BAN LIST.
    bans = ban_list_modal(doc)
    # Above 100 % text the bans' rows grow with it (section 2's reflow); the
    # headings are furniture and keep their size.
    from build_retained_screens import fixed_text, walk_nodes
    nodes = {node["id"]: node for node in walk_nodes(bans)}
    grow_rows(doc, nodes["ban-column"])
    for ident in ("ban-head-name", "ban-head-guid"):
        fixed_text(doc, nodes[ident])
    # Each arrival starts on MATCH.
    activate = [{"op": "setState", "values": {"create.tab": 0}}, {"op": "focus", "control": "create_start"}]
    # Above 100 % text the maps' rows and the settings grow; the headings,
    # the tabs and CREATE SERVER are furniture and keep their size.
    for node in [*headings, *shot, scrollbar, tabs, start]:
        fixed_text(doc, node)
    return tall_create_page(subpage_document(doc, "create", "#str_200002", "#str_200026", "create_start",
                                             [*headings, *shot, viewport, scrollbar, tabs, column, start, back], [bans],
                                             activate=activate))


# A tall view's Create Server page (retained_tall): MAP SELECTION, the chosen
# map's levelshot with the map list beside it, then the settings' tabs with
# their rows across the column and CREATE SERVER, 1.45 times the landscape
# size, which keeps CREATE SERVER above the bottom band at 3:4.
TALL_GROW = 1.45


def tall_create_page(result: dict) -> dict:
    from retained_saves import BACKING_ALPHA
    from retained_tall import CONTENT_W, MARGIN, TallLayout
    tall = TallLayout(result)
    tall.show("create-header", "none", "block")
    tall.block(("create-head-maps",), (U * MAPS_HEAD[0], U * MAPS_HEAD[1]), (MARGIN + 4, 84), TALL_GROW)
    shot_top = 84 + U * 16 * TALL_GROW + 6
    tall.block(("create-shot-back", "create-shot", "create-shot-border"), (U * SHOT[0], U * SHOT[1]), (MARGIN, shot_top), TALL_GROW)
    shot_w, shot_h = U * SHOT[2] * TALL_GROW, U * SHOT[3] * TALL_GROW
    list_left = MARGIN + shot_w + 16
    list_w = CONTENT_W - shot_w - 16 - 40
    tall.bind("maps-list", "left", list_left)
    tall.top("maps-list", shot_top)
    tall.bind("maps-list", "width", list_w)
    tall.bind("maps-list", "height", shot_h)
    tall.grow("maps-column", TALL_GROW)
    for ident in [f"map-{index}" for index in range(MAP_ROWS)] + ["maps-newer", "maps-older"]:
        for node in (ident, f"{ident}-line", f"{ident}-hover", f"{ident}-selected", f"{ident}-focus"):
            tall.set(node, "width", list_w)
        text = f"{ident}-name" if ident.startswith("map-") else f"{ident}-label"
        tall.set(text, "width", list_w - (U * 8 if ident.startswith("map-") else U * 18) * TALL_GROW)
    tall.bind("maps_scroll", "left", list_left + list_w + 4)
    tall.top("maps_scroll", shot_top)
    tall.bind("maps_scroll", "height", shot_h)
    tabs_top = shot_top + shot_h + 18
    tall.block(("create-tabs",), (U * OPTIONS[0], U * TABS_TOP), (MARGIN, tabs_top), TALL_GROW)
    options_top = tabs_top + U * (OPTIONS[1] - TABS_TOP) * TALL_GROW
    tall.block(("create-options",), (U * OPTIONS[0], U * OPTIONS[1]), (MARGIN, options_top), TALL_GROW)
    start_top = options_top + U * ROW_PITCH * TAB_ROWS * TALL_GROW + 10
    tall.block(("create_start",), (U * START[0], U * START[1]), (MARGIN, start_top), 1.3)
    tall.dialogs(TALL_GROW)
    tall.shade("create-backing", BACKING_ALPHA)
    return result
