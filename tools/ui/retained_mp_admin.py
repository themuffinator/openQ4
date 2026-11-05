"""The Escape card's Admin page (docs/dev/ui-visual-design.md section 14.18).

Built by retained_mp_menus.escape_document. The stock Admin page (mpmain.gui's
p_admin) asked for the server's remote console password, then offered three
sub-pages: Players (the player list with Kick Player, Ban Player, Force Team
Switch and View Ban List), Server (the rules and Apply Changes) and Console.
The card keeps that order. Until the player is an admin (a listen server's host
always is; a client once the server accepts its password, until the map
changes) the page shows the server, the masked password field and ACCEPT, and
whether the password is being checked or was refused. Then a second, smaller
strip of three sections, as the Match page's:

- Players: the players by client number with their teams, a row's Accept
  choosing it; Kick, Ban and Force team switch as the card's action plates,
  each saying why it cannot act (no player chosen, the player's own row, a
  mode without teams, a managed match); and View ban list, which asks the
  server for its list and opens PLAYER BAN LIST, the Create Server page's
  dialog, where REMOVE BAN lifts the chosen ban.
- Server: the stock page's settings drafted as the Vote page drafts a call
  (the map and game type lists, the limits the drafted game type uses,
  balance, shuffle, restart and buying), and Apply changes, unavailable until
  something would change.
- Console: the remote console's newest lines and a field whose Enter runs the
  command there, as the stock console's did.

The game publishes the page as mp.admin.* (idMultiplayerGame::
PublishRetainedAdmin) and takes its commands through the session's mpAdmin
verbs (idSessionLocal::HandleRetainedMultiplayerRequest).
"""
from __future__ import annotations

import math

import build_retained_screens as b
from build_retained_screens import FULL, OLIVE, ORANGE, VALUE, group, keyword, label, length, number, path, rgb, solid, stroke, typeface, vector
from retained_create_server import ban_list_modal, text_row
from retained_mp_menus import (ERROR, HEADING, INSET, MATCH_LIST_ROW_H, MATCH_STRIP_GAP, MATCH_STRIP_H, PAGE_TOP, PLACEHOLDER, PROMPT_BOTTOM,
                               PROMPT_H, SLOT_PITCH, VOTE_ROW_H, Document, action_plate, any_text, colour, heading_text, in_game_plate,
                               MAP_CATALOG, link, page_group, row_face, section_triggers, value_row, wrapped_width)

# The page's sections, in the stock sub-pages' order, with their stock names.
SECTIONS = ("players", "server", "console")
SECTION_KEYS = ("#str_200038", "#str_200228", "#str_200229")
PASSWORD_HEADING, PASSWORD_LABEL, ACCEPT = "#str_200317", "#str_200069", "#str_200318"
VIEW_BANS = "#str_200233"
PASSWORD_BYTES = 64                 # RETAINED_MP_PASSWORD_BYTES in the session
COMMAND_BYTES = 200                 # RETAINED_MP_COMMAND_BYTES in the session
PLAYERS = 32                        # RETAINED_ADMIN_PLAYERS in the game
CONSOLE_LINES = 12                  # RETAINED_ADMIN_CONSOLE_LINES in the game
TEAM_COLUMN = 130.0                 # the players list's team column (dp)
# The player actions and Apply changes, the game's retainedAdminAction_t, with
# the session verb each runs; the game publishes each as mp.admin.<name>.*.
ACTIONS = (("kick", "mpAdminKick"), ("ban", "mpAdminBan"), ("switch", "mpAdminSwitch"))
# Kick and Ban ask first in the stock confirmation (section 14.18: destructive
# actions use the stock modal), titled as their plates and naming the player;
# the stock page asked nothing. Each modal's question, in openQ4's tables.
CONFIRMED = {"kick": ("#str_200230", "#str_231590"), "ban": ("#str_200231", "#str_231591")}
CONFIRM_W = 480.0
APPLY = ("apply", "mpAdminApply")
# The Server section's drafted settings, in the game's retainedVoteField_t
# order without the kick, as the session's RETAINED_MP_ADMIN_FIELDS lists
# their verbs: the game's key, the verb, the label (the stock Admin page's)
# and the control, as the Vote page's VOTE_ROWS give them. The left column
# takes the lists and limits, the right the yes or no settings.
ROWS = (
    ("map", "mpAdminMap", "#str_200209", ("choice", MAP_CATALOG)),
    ("gametype", "mpAdminGameType", "#str_200210", ("choice", 16)),
    ("timelimit", "mpAdminTimeLimit", "#str_200236", ("slider", 0, 60, 1)),
    ("fraglimit", "mpAdminFragLimit", "#str_200060", ("slider", 0, 100, 1)),
    ("capturelimit", "mpAdminCaptureLimit", "#str_200235", ("slider", 1, 50, 1)),
    ("tourneylimit", "mpAdminTourneyLimit", "#str_200214", ("slider", 1, 20, 1)),
    ("controltime", "mpAdminControlTime", "#str_222013", ("slider", 10, 600, 10)),
    ("balance", "mpAdminBalance", "#str_200240", ("toggle",)),
    ("shuffle", "mpAdminShuffle", "#str_201044", ("toggle",)),
    ("restart", "mpAdminRestart", "#str_200234", ("toggle",)),
    ("buying", "mpAdminBuying", "#str_222000", ("toggle",)),
)
LEFT_ROWS = ("map", "gametype", "timelimit", "fraglimit", "capturelimit", "tourneylimit", "controltime")


def section_tab(doc: Document, position: int, section: str, key: str) -> tuple[dict, str]:
    """One of the inner strip's sections, as the Match page's: its name,
    orange with focus, over a bar while it is the section shown."""
    ident = f"admin-tab-{section}"
    selected = {"op": "==", "args": [{"state": "card.admin_section"}, position]}
    caption = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                    **typeface("lowpixel", 16, MATCH_STRIP_H, [1, 1, 1, 0.7]), "white-space": keyword("nowrap")})
    bar = vector(f"{ident}-bar", {**b.absolute(left=0, top=MATCH_STRIP_H - 3, width=0, height=3), "width": length(100, "%"),
                                  "display": keyword("none")},
                 [path("bar", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 3), (0, 3)], fill=solid(rgb(ORANGE)))])
    doc.bind(f"{ident}-bar.display", f"{ident}-bar", "display", {"op": "select", "args": [selected, "block", "none"]})
    states = doc.states(ident, {
        "default": [(f"{ident}-label", "color", colour([1, 1, 1, 0.7]))],
        "hover": [(f"{ident}-label", "color", colour([1, 1, 1, 1]))],
        "focus": [(f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-label", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-label", "color", colour([1, 1, 1, 0.7]))],
    })
    event = f"admin_section_{section}"
    doc.events[event] = [{"op": "setState", "values": {"card.admin_section": position}}, {"op": "focus", "control": ident}]
    node = group(ident, {"position": keyword("relative"), "display": keyword("block"), "height": length(MATCH_STRIP_H),
                         "padding-left": length(10), "padding-right": length(10), "margin-right": length(4), "flex-shrink": number(0)},
                 [caption, bar], control={"role": "button", "label": key, "event": event, "states": states})
    return node, ident


def player_row(doc: Document, row: int, width: float) -> tuple[dict, str]:
    """A player, the scoreboard's row at 22 dp as the Match page's lists draw
    it: the name, then the team; the chosen player's row lit in olive. Accept
    captures its client and connection identity in mpAdminSelect. A changed
    occupant cancels a held press; the game checks the captured pair again."""
    ident, state = f"admin-player-{row}", f"mp.admin.player{row}"
    for field in ("c0", "c1", "identity"):
        doc.state[f"{state}.{field}"] = {"type": "string", "initial": ""}
    doc.state[f"{state}.client"] = {"type": "number", "initial": -1}
    band = group(f"{ident}-band", {**FULL, "background-color": colour([1, 1, 1, 0.06])})
    chosen = group(f"{ident}-chosen", {**FULL, "display": keyword("none"), "background-color": colour(rgb(OLIVE, 0.45))})
    doc.bind(f"{ident}-chosen.display", f"{ident}-chosen", "display",
             {"op": "select", "args": [{"op": "==", "args": [{"state": "mp.admin.player.selected"}, row]}, "block", "none"]})
    rail = vector(f"{ident}-focus", {**b.absolute(left=0, top=0, width=4, height=MATCH_LIST_ROW_H), "opacity": number(0)},
                  [path("rail", [(0, 0), (3, 0), (3, MATCH_LIST_ROW_H), (0, MATCH_LIST_ROW_H)], fill=solid(rgb(ORANGE)))])
    cell = {"position": keyword("relative"), "display": keyword("block"), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
            "flex-shrink": number(0), **typeface("lowpixel", 14, MATCH_LIST_ROW_H, [1, 1, 1, 0.85])}
    name = label(f"{ident}-c0", PLACEHOLDER, {**cell, "flex-grow": number(1), "flex-shrink": number(1), "min-width": length(0)})
    team = label(f"{ident}-c1", PLACEHOLDER, {**cell, "width": length(TEAM_COLUMN), "margin-left": length(6),
                                               **typeface("lowpixel", 14, MATCH_LIST_ROW_H, [1, 1, 1, 0.6])})
    for field in ("c0", "c1"):
        doc.bind(f"{ident}-{field}.text", f"{ident}-{field}", "text", {"state": f"{state}.{field}"})
    content = group(f"{ident}-content", {**b.absolute(left=10, top=0, width=width - 14, height=MATCH_LIST_ROW_H), "display": keyword("flex"),
                                         "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none")},
                    [name, team])
    states = doc.states(ident, {
        "default": [(f"{ident}-focus", "opacity", number(0)), (f"{ident}-c0", "color", colour([1, 1, 1, 0.85]))],
        "hover": [(f"{ident}-focus", "opacity", number(0.5)), (f"{ident}-c0", "color", colour([1, 1, 1, 1]))],
        "focus": [(f"{ident}-focus", "opacity", number(1)), (f"{ident}-c0", "color", colour(rgb(ORANGE)))],
        "pressed": [(f"{ident}-focus", "opacity", number(1)), (f"{ident}-c0", "color", colour(rgb(ORANGE)))],
        "disabled": [(f"{ident}-focus", "opacity", number(0)), (f"{ident}-c0", "color", colour([1, 1, 1, 0.85]))],
    })
    event = f"admin_player_{row}"
    doc.events[event] = [{"op": "setState", "values": {"card.admin_row": row, "card.admin_client": {"state": f"{state}.client"}, "card.admin_identity": {"state": f"{state}.identity"}}}, {"op": "action", "action": "mpAdminSelect"}]
    node = group(ident, {"position": keyword("relative"), "display": keyword("none"), "width": length(width),
                         "height": length(MATCH_LIST_ROW_H), "margin-bottom": length(1), "flex-shrink": number(0)},
                 [band, chosen, rail, content], control={"role": "button", "label": PLACEHOLDER, "event": event, "states": states,
                                                       "context": {"state": f"{state}.identity"}})
    doc.bind(f"{ident}.display", ident, "display",
             {"op": "select", "args": [{"op": "<", "args": [row, {"state": "mp.admin.player.count"}]}, "block", "none"]})
    return node, ident


def server_row(doc: Document, key: str, verb: str, label_key: str, control: tuple, width: float, value_x: float) -> dict:
    """One drafted setting, the Vote page's row: a list's rows come from the
    game; the row shows where the drafted game type uses its field
    (mp.admin.<key>.row_shown)."""
    kind = control[0]
    state_key = f"mp.admin.{key}"
    boolean = kind == "toggle"
    doc.state.update({
        f"{state_key}.row_shown": {"type": "boolean", "initial": False},
        state_key: {"type": "boolean", "initial": False} if boolean else {"type": "number", "initial": -1 if kind == "choice" else 0},
    })
    doc.actions[verb] = {"operation": "session.menuValue", "input": "boolean" if boolean else "number",
                         "arguments": {"command": verb, "value": {"input": "value"}}}
    entries, count, slider = None, None, None
    if kind == "choice":
        slots = control[1]
        for row in range(slots):
            doc.state[f"{state_key}{row}"] = {"type": "string", "initial": ""}
        doc.state[f"{state_key}_count"] = {"type": "number", "initial": 0}
        entries = [({"state": f"{state_key}{row}"}, row) for row in range(slots)]
        count = {"state": f"{state_key}_count"}
    elif kind == "slider":
        slider = (*control[1:], 0)
    return value_row(doc, f"admin-row-{key}", label_key, kind, width, value_x, value={"state": state_key}, action=verb,
                     entries=entries, count=count, slider=slider, shown={"state": f"{state_key}.row_shown"}, wrap=True)


def label_column(keys, width: float, wrap: bool = False) -> float:
    """Where a column's values start: after its longest label in any
    language (in two lines at 14 dp where `wrap`), at most half the column."""
    need = max((wrapped_width(text) if wrap else b.text_width("lowpixel", text, 15)) for key in keys for text in any_text(key).values())
    return min(math.ceil(21 + need + 8), round(width * 0.5, 3))


def check_labels(page_width: float, right_w: float, apply_w: float, left_x: float, right_x: float) -> None:
    """Fail generation when a label, a reason or a plate's label would not
    fit its room in any language. (The Arena Campaign's refusal never shows:
    the card does not cover an Arena match.)"""
    over = {}
    for key, room in ((PASSWORD_HEADING, page_width), (ACCEPT, 300 - 46), (VIEW_BANS, right_w - 40),
                      *((key, 200) for key in SECTION_KEYS)):
        face, size = ("marine", 20) if key == ACCEPT else ("lowpixel", 16 if key in SECTION_KEYS else 14)
        for language, text in any_text(key).items():
            if b.text_width(face, text, size) > room:
                over[(key, language)] = round(b.text_width(face, text, size))
    for key, room in (("#str_200230", right_w), ("#str_200231", right_w), ("#str_200232", right_w), ("#str_200241", apply_w)):
        for language, text in any_text(key).items():
            if b.text_width("marine", text, 20) > room - 46 - 20:
                over[(key, language)] = round(b.text_width("marine", text, 20))
    for key, room in (*((key, right_w) for key in ("#str_231291", "#str_231292", "#str_231293", "#str_231294", "#str_231577")),
                      ("#str_231294", apply_w), ("#str_231295", apply_w)):
        for language, text in any_text(key).items():
            if b.text_width("lowpixel", text, 14) > room - 26:
                over[(key, language)] = round(b.text_width("lowpixel", text, 14))
    for key in ("#str_231290", "#str_104847"):
        for language, text in any_text(key).items():
            text = text.replace("\\n", "").strip()
            if b.text_width("lowpixel", text, 15) > page_width:
                over[(key, language)] = round(b.text_width("lowpixel", text, 15))
    for keys, value_x in (([row[2] for row in ROWS if row[0] in LEFT_ROWS], left_x),
                          ([row[2] for row in ROWS if row[0] not in LEFT_ROWS], right_x)):
        for key in keys:
            for language, text in any_text(key).items():
                if wrapped_width(text) > value_x - 29:
                    over[(key, language)] = round(wrapped_width(text))
    if over:
        raise SystemExit(f"Admin page labels past their room: {over}")


def admin_page(doc: Document, index: int, ident: str, width: float, height: float) -> tuple[dict, list]:
    """The Admin page (section 14.18; see the module's description)."""
    page_width = width - 2 * INSET
    page_height = height - PAGE_TOP - PROMPT_BOTTOM - PROMPT_H - 16
    doc.state.update({
        "card.admin_section": {"type": "number", "initial": 0},
        "card.admin_row": {"type": "number", "initial": -1},
        "card.admin_ban_pick": {"type": "number", "initial": -1},
        # The fields' entered text, which the session takes and clears.
        "card.admin.password": {"type": "string", "initial": ""},
        "card.admin.command": {"type": "string", "initial": ""},
        "mp.admin.valid": {"type": "boolean", "initial": False},
        "mp.admin.checking": {"type": "boolean", "initial": False},
        "mp.admin.notice": {"type": "string", "initial": ""},
        "mp.admin.player.count": {"type": "number", "initial": 0},
        "mp.admin.player.selected": {"type": "number", "initial": -1},
        "mp.admin.client": {"type": "number", "initial": -1},
        "mp.admin.identity": {"type": "string", "initial": ""},
        "card.admin_client": {"type": "number", "initial": -1},
        "card.admin_identity": {"type": "string", "initial": ""},
        "mp.server.name": {"type": "string", "initial": ""},
        "mp.server.address": {"type": "string", "initial": ""},
        # The adapter keeps this: a field holds text not yet entered.
        "ui.numberDraftsPending": {"type": "boolean", "initial": False},
        **{f"mp.admin.console{line}": {"type": "string", "initial": ""} for line in range(CONSOLE_LINES)},
    })
    for verb in ("mpAdminPass", "mpAdminBans", "mpAdminConsole", APPLY[1]):
        doc.session(verb, verb)
    for verb in ("mpAdminSelect", *(verb for _name, verb in ACTIONS)):
        prefix = "card.admin_" if verb == "mpAdminSelect" else "mp.admin."
        doc.actions[verb] = {"operation": "session.menu", "arguments": {
            "command": verb, "client": {"state": prefix + "client"}, "identity": {"state": prefix + "identity"}}}
    doc.actions["adminEditField"] = {"operation": "ui.numberDrafts.focus", "arguments": {}}
    valid = {"state": "mp.admin.valid"}
    controls: list = []

    # Until the player is an admin: the server, the password and ACCEPT.
    pass_value_x = label_column((PASSWORD_LABEL,), min(page_width, 560))
    field_w = min(page_width, 560)
    # The session takes and clears each field's entry, and ignores an empty
    # one, so a field is never in error for being empty.
    password = text_row(doc, "admin-password", PASSWORD_LABEL, field_w, pass_value_x, "card.admin.password", "admin_pass_entered",
                        PASSWORD_BYTES, 30, allow_empty=True, masked=True)
    password["properties"].update(b.absolute(left=0, top=84))
    doc.events["admin_pass_entered"] = [{"op": "action", "action": "mpAdminPass"}]
    # ACCEPT checks the entered password; one typed but not entered takes the
    # focus back, where Enter checks it.
    doc.events["admin_pass_accept"] = [{"op": "if", "condition": {"state": "ui.numberDraftsPending"},
                                        "then": [{"op": "action", "action": "adminEditField"}],
                                        "else": [{"op": "action", "action": "mpAdminPass"}]}]
    accept = in_game_plate(doc, "admin-accept", ACCEPT, 0, 128, width=300, primary=True, event="admin_pass_accept")
    server_name = label("admin-server-name", PLACEHOLDER, {**b.absolute(left=0, top=26, width=page_width, height=22),
                        **typeface("marine", 17, 22, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("admin-server-name.text", "admin-server-name", "text", {"state": "mp.server.name"})
    address = label("admin-server-address", PLACEHOLDER, {**b.absolute(left=0, top=50, width=page_width, height=20),
                    **typeface("lowpixel", 15, 20, rgb(VALUE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind("admin-server-address.text", "admin-server-address", "text", {"state": "mp.server.address"})
    notice_box = {**b.absolute(left=0, top=184, width=page_width, height=20), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                  "display": keyword("none")}
    checking = label("admin-checking", PLACEHOLDER, {**notice_box, **typeface("lowpixel", 15, 20, rgb(VALUE))})
    refused = label("admin-refused", PLACEHOLDER, {**notice_box, **typeface("lowpixel", 15, 20, rgb(ERROR))})
    for node, shown in (("admin-checking", {"state": "mp.admin.checking"}),
                        ("admin-refused", {"op": "&&", "args": [{"op": "!", "args": [{"state": "mp.admin.checking"}]},
                                                                {"op": "!=", "args": [{"state": "mp.admin.notice"}, ""]}]})):
        doc.bind(f"{node}.text", node, "text", {"state": "mp.admin.notice"})
        doc.bind(f"{node}.display", node, "display", {"op": "select", "args": [shown, "block", "none"]})
    gate = group("admin-pass", {**b.absolute(left=0, top=0, width=page_width, height=page_height), "display": keyword("block")},
                 [heading_text("admin-pass-heading", PASSWORD_HEADING, 0, 0, page_width), server_name, address, password, accept,
                  checking, refused])
    doc.bind("admin-pass.display", "admin-pass", "display", {"op": "select", "args": [valid, "none", "block"]})
    controls += ["admin-password", "admin-accept"]

    # The admin's sections, under their own strip.
    tabs = []
    for position, (section, key) in enumerate(zip(SECTIONS, SECTION_KEYS)):
        node, tab_id = section_tab(doc, position, section, key)
        tabs.append(node)
        controls.append(tab_id)
    strip = group("admin-strip", {**b.absolute(left=0, top=0, width=page_width, height=MATCH_STRIP_H), "display": keyword("flex"),
                                  "flex-direction": keyword("row"), "align-items": keyword("center")},
                  [*tabs, section_triggers(doc, "admin-triggers", SECTION_KEYS, page_width)])
    rule = vector("admin-strip-rule", b.absolute(left=0, top=MATCH_STRIP_H, width=page_width, height=1), [
        path("rule", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 1), (0, 1)], fill=solid(rgb(OLIVE, 0.5)))])
    section_top = MATCH_STRIP_H + MATCH_STRIP_GAP
    section_height = page_height - section_top
    # The triggers step the sections while the page is current, as on the
    # Match page.
    current = {"op": "==", "args": [{"state": "card.tab"}, index]}
    for name, step in (("onSectionPrevious", -1), ("onSectionNext", 1)):
        steps: list = []
        for position in reversed(range(len(SECTIONS))):
            target = SECTIONS[(position + step) % len(SECTIONS)]
            steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.admin_section"}, position]},
                      "then": [{"op": "call", "event": f"admin_section_{target}"}], **({"else": steps} if steps else {})}]
        doc.events.setdefault(name, []).append({"op": "if", "condition": {"op": "&&", "args": [current, valid]}, "then": steps})

    def section_group(section: str, position: int, children: list) -> dict:
        node = group(f"admin-section-{section}", {**b.absolute(left=0, top=section_top, width=page_width, height=section_height),
                                                  "display": keyword("none")}, children)
        doc.bind(f"admin-section-{section}.display", f"admin-section-{section}", "display",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "card.admin_section"}, position]}, "block", "none"]})
        return node

    # Players: the list on the leading side, the actions beside it.
    left_w = round(page_width * 0.46, 3)
    right_x = left_w + 16
    right_w = page_width - right_x
    rows = []
    for row in range(PLAYERS):
        node, row_id = player_row(doc, row, left_w - 12)
        rows.append(node)
        controls.append(row_id)
    player_list = group("admin-players", {**b.absolute(left=0, top=0, width=left_w, height=section_height), "overflow": keyword("auto")}, rows)
    actions = []
    for slot, (name, verb) in enumerate(ACTIONS):
        node = f"admin-{name}"
        # A confirmed action captures the player it will name when it asks.
        steps = ([{"op": "setState", "values": {"card.admin_confirm_client": {"state": "mp.admin.client"},
                                                "card.admin_confirm_identity": {"state": "mp.admin.identity"},
                                                "card.admin_confirm_name": {"state": f"mp.admin.{name}.detail"}}},
                  {"op": "call", "event": f"{confirm_modal(name)}Show"}] if name in CONFIRMED else [{"op": "action", "action": verb}])
        actions.append(action_plate(doc, node, f"mp.admin.{name}", right_w, slot * SLOT_PITCH, f"admin_{name}", steps))
        next(child for child in actions[-1]["children"] if child["id"] == node)["control"]["context"] = {"state": "mp.admin.identity"}
        controls.append(node)
    # View ban list asks for the list, then opens PLAYER BAN LIST.
    doc.events["admin_bans"] = [{"op": "action", "action": "mpAdminBans"}, {"op": "call", "event": "banModalShow"}]
    bans = link(doc, "admin-bans", VIEW_BANS, event="admin_bans")
    bans_row = group("admin-bans-row", {**b.absolute(left=right_x, top=len(ACTIONS) * SLOT_PITCH + 8, width=right_w, height=24),
                                        "display": keyword("flex"), "flex-direction": keyword("row"), "align-items": keyword("center")}, [bans])
    controls.append("admin-bans")
    players_section = section_group("players", 0, [player_list, group("admin-actions", b.absolute(left=right_x, top=0, width=right_w,
                                                                                                     height=section_height), actions),
                                                   bans_row])

    # Server: the drafted settings in two columns, Apply changes across the
    # foot.
    server_left_w = round(page_width * 0.5 - 8, 3)
    server_right_x = server_left_w + 16
    server_right_w = page_width - server_right_x
    apply_w = min(page_width, 640.0)
    left_x = label_column([row[2] for row in ROWS if row[0] in LEFT_ROWS], server_left_w, wrap=True)
    right_value_x = label_column([row[2] for row in ROWS if row[0] not in LEFT_ROWS], server_right_w, wrap=True)
    check_labels(page_width, right_w, apply_w, left_x, right_value_x)
    left_rows, right_rows = [], []
    for key, verb, label_key, control in ROWS:
        on_left = key in LEFT_ROWS
        row = server_row(doc, key, verb, label_key, control, server_left_w if on_left else server_right_w,
                         left_x if on_left else right_value_x)
        (left_rows if on_left else right_rows).append(row)
        # Each row binds its own availability, as the Vote page's do.
        enabled = {"op": "==", "args": [{"state": "card.tab"}, index]}
        if control[0] == "choice":
            enabled = {"op": "&&", "args": [enabled, {"op": ">", "args": [{"state": f"mp.admin.{key}_count"}, 0]}]}
        doc.bind(f"admin-row-{key}.enabled", f"admin-row-{key}", "enabled", enabled)
    server_left = group("admin-server-left", {**b.absolute(left=0, top=0, width=server_left_w, height=section_height),
                                              "display": keyword("flex"), "flex-direction": keyword("column")}, left_rows)
    server_right = group("admin-server-right", {**b.absolute(left=server_right_x, top=0, width=server_right_w, height=len(right_rows) * VOTE_ROW_H),
                                                "display": keyword("flex"), "flex-direction": keyword("column")}, right_rows)
    apply_name, apply_verb = APPLY
    apply = action_plate(doc, "admin-apply", f"mp.admin.{apply_name}", apply_w, section_height - 58, "admin_apply",
                         [{"op": "action", "action": apply_verb}], primary=True, menu_button=True)
    controls.append("admin-apply")
    server_section = section_group("server", 1, [server_left, server_right,
                                                 group("admin-apply-holder", b.absolute(left=0, top=0, width=apply_w, height=section_height),
                                                       [apply])])

    # Console: the newest lines in the stock console's frame, the field under them.
    console_h = section_height - 40
    frame = vector("admin-console-frame", b.absolute(left=0, top=0, width=page_width, height=console_h), [
        path("frame", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})], fill=solid([0, 0, 0, 0.5]),
             stroke=stroke(solid(rgb(OLIVE, 0.4)), 1))])
    line_h = min(18.0, math.floor((console_h - 12) / CONSOLE_LINES))
    lines = []
    for line in range(CONSOLE_LINES):
        node = f"admin-console-{line}"
        lines.append(label(node, PLACEHOLDER, {**b.absolute(left=8, top=6 + line * line_h, width=page_width - 16, height=line_h),
                           **typeface("lowpixel", 14, line_h, [1, 1, 1, 0.85]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}))
        doc.bind(f"{node}.text", node, "text", {"state": f"mp.admin.console{line}"})
    command = text_row(doc, "admin-command", SECTION_KEYS[2], page_width, 8, "card.admin.command", "admin_command_entered",
                       COMMAND_BYTES, 30, allow_empty=True, caption=False)
    command["properties"].update(b.absolute(left=0, top=console_h + 8))
    doc.events["admin_command_entered"] = [{"op": "action", "action": "mpAdminConsole"}]
    controls.append("admin-command")
    console_section = section_group("console", 2, [frame, *lines, command])

    live = group("admin-live", {**b.absolute(left=0, top=0, width=page_width, height=page_height), "display": keyword("none")},
                 [strip, rule, players_section, server_section, console_section])
    doc.bind("admin-live.display", "admin-live", "display", {"op": "select", "args": [valid, "block", "none"]})

    # PLAYER BAN LIST: the Create Server page's dialog over the game's list.
    doc.session("mpAdminBanPick", "mpAdminBanPick")
    doc.session("mpAdminUnban", "mpAdminUnban")
    page = page_group(doc, index, ident, width, height, [gate, live], controls)
    # Opening the page focuses the password until the player is an admin,
    # then the section shown.
    # On Players the chosen player's row takes the focus, which scrolls it
    # into view, as the Players page reveals its selected row.
    chosen = [{"op": "if", "condition": {"op": "==", "args": [{"state": "mp.admin.player.selected"}, row]},
               "then": [{"op": "focus", "control": f"admin-player-{row}"}]} for row in range(PLAYERS)]
    focus_steps: list = []
    for position in reversed(range(len(SECTIONS))):
        steps = [{"op": "focus", "control": f"admin-tab-{SECTIONS[position]}"}, *(chosen if SECTIONS[position] == "players" else [])]
        focus_steps = [{"op": "if", "condition": {"op": "==", "args": [{"state": "card.admin_section"}, position]},
                        "then": steps, **({"else": focus_steps} if focus_steps else {})}]
    focus = [{"op": "if", "condition": valid, "then": focus_steps, "else": [{"op": "focus", "control": "admin-password"}]}]
    return page, focus


def confirm_modal(name: str) -> str:
    return f"admin{name.capitalize()}Modal"


def confirm_modals(doc: Document) -> list:
    """Kick's and Ban's confirmations: the stock modal titled as the plate, its
    question and under it the player's name as the dialog captured it. Yes
    sends the action for that captured player (the game refuses it if they
    have left or their slot was taken); No and back cancel. Above 100 % text
    the question takes the lines the running language needs and the dialog
    grows with it, as the Arena's reset question does."""
    from build_retained_screens import (LARGER, absolute, confirmation, per_language, text_buckets, text_source, text_sum,
                                        text_times, wrapped_lines)
    doc.state.update({"card.admin_confirm_client": {"type": "number", "initial": -1},
                      "card.admin_confirm_identity": {"type": "string", "initial": ""},
                      "card.admin_confirm_name": {"type": "string", "initial": ""}})
    text_source(doc)
    modals = []
    for name, verb in ACTIONS:
        if name not in CONFIRMED:
            continue
        title, question = CONFIRMED[name]
        modal = confirm_modal(name)
        confirmed = f"{verb}Confirmed"
        doc.actions[confirmed] = {"operation": "session.menu", "arguments": {
            "command": verb, "client": {"state": "card.admin_confirm_client"}, "identity": {"state": "card.admin_confirm_identity"}}}
        body_w = CONFIRM_W - 66
        counts = wrapped_lines((question,), "lowpixel", 17, body_w)
        lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
        base = max(found[1] for found in counts.values())
        if base > 2:
            raise SystemExit(f"{modal}: the question needs {base} lines at 100 %; the dialog holds two and the name")
        needed = text_times({"op": "*", "args": [lines, 22]})
        ask = label(f"{modal}-question", question, {**absolute(left=33, top=71, width=body_w, height=44),
                                                     **typeface("lowpixel", 17, 22, [1, 1, 1, 0.8])})
        doc.bind(f"text.{modal}-question.height", f"{modal}-question", "height",
                 {"op": "select", "args": [LARGER, {"op": "max", "args": [44, needed]}, 44]})
        who = label(f"{modal}-name", PLACEHOLDER, {**absolute(left=33, top=71 + 44, width=body_w, height=22),
                                                    **typeface("lowpixel", 17, 22, rgb(VALUE)), "white-space": keyword("nowrap"),
                                                    "overflow": keyword("hidden")})
        doc.bind(f"{modal}-name.text", f"{modal}-name", "text", {"state": "card.admin_confirm_name"})
        doc.bind(f"text.{modal}-name.top", f"{modal}-name", "top",
                 {"op": "select", "args": [LARGER, {"op": "+", "args": [71, {"op": "max", "args": [44, needed]}]}, 115]})
        doc.bind(f"text.{modal}-name.height", f"{modal}-name", "height", {"op": "select", "args": [LARGER, text_times(22), 22]})
        doc.events[f"{modal}Yes"] = [{"op": "call", "event": f"{modal}Hide"}, {"op": "action", "action": confirmed}]
        modals.append(confirmation(doc, modal, title, PLACEHOLDER, None, yes_event=f"{modal}Yes", contents=[ask, who],
                                   contents_grow=text_sum({"op": "max", "args": [0, text_sum(-44, needed)]}, text_times(22), -22)))
    return modals


def admin_modals(doc: Document) -> list:
    """The Admin page's dialogs: PLAYER BAN LIST over the game's list
    (mp.admin.ban*), its rows choosing through mpAdminBanPick and REMOVE BAN
    lifting the chosen ban through mpAdminUnban; and Kick's and Ban's
    confirmations."""
    return [ban_list_modal(doc, prefix="mp.admin", pick_key="card.admin_ban_pick", pick_verb="mpAdminBanPick", remove_verb="mpAdminUnban"),
            *confirm_modals(doc)]
