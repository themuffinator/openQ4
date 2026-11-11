"""Player Setup edits inside the Multiplayer page, without another menu level.

The session owns committed identity, catalog and model assets. Text Enter and
value controls submit typed proposals with the observed value/catalog identity;
neither GUI source nor a deferred row number can select another asset silently.
"""
from __future__ import annotations

import math

from build_retained_screens import (CANVAS_W, FULL, OLIVE, ORANGE, TEXT, VALUE, U, Document, absolute, colour, fixed_text, group, keyword,
                                    label, length, linear, number, path, rgb, solid, stroke, text_sum, text_times, text_width, transform,
                                    typeface, vector)
from retained_create_server import text_row
from retained_mp_menus import (APPEARANCE_ROWS, OUTLINE_WIDTHS, any_text, cvar_state, in_game_plate, setting_action, stack_row,
                               value_row)
from retained_saves import table_text

MODEL_ROWS = 128  # Bounded choice page, not a limit on the installed catalog.
ROTATION_LABEL = "#str_231547"
# The rows are 540 dp wide: the tab strip's three labels and its keycaps need
# 528 dp in the widest language (Brazilian Portuguese), and the preview's
# figure starts 70 dp past them.
WIDTH, VALUE_X, ROW_H = 540.0, 190.0, 45.0
# The page stands from 72 u, under the docked top band: from 130 u its rows
# ran under the bottom band, and two tabs' last rows and BACK had to be
# scrolled to.
SETUP_TOP, FIELDS_H = 72.0, 312.0          # u: the heading's top, and the rows' room under it
# The preview's figure in the canvas (FIGURE): its middle and its feet (dp).
BAY_X, BAY_FEET = 0.83 * 960, (0.226 + 0.527) * 720


def colour_chip(doc: Document, row: str, value: dict, colours: tuple) -> list:
    """A colour row's chip: a 14 dp square in the colour the row is set to
    (`value`, one of `colours`' RGB texts), with a thin light edge, before
    the row's chevron."""
    channels = [[float(part) for part in text.split()] for text in colours]

    def channel(index: int):
        result = channels[-1][index]
        for text, parts in reversed(list(zip(colours[:-1], channels[:-1]))):
            result = {"op": "select", "args": [{"op": "==", "args": [value, text]}, parts[index], result]}
        return result
    box = absolute(left=WIDTH - 64, top=round((ROW_H - 14) / 2, 3), width=14, height=14)
    chip = group(f"{row}-chip", {**box, "background-color": colour([*channels[0], 1]), "pointer-events": keyword("none")})
    doc.bind(f"{row}-chip.background-color", f"{row}-chip", "background-color", [channel(0), channel(1), channel(2), 1])
    edge = vector(f"{row}-chip-edge", {**box, "pointer-events": keyword("none")}, [
        path("edge", [(0.5, 0.5), (13.5, 0.5), (13.5, 13.5), (0.5, 13.5)], stroke=stroke(solid([1, 1, 1, 0.45]), 1))])
    return [chip, edge]


def preview_bay() -> dict:
    """The preview's bay under the figure: a ground ring where it stands (two
    ellipses and four ticks, the HUD's line at low strength) and corner
    brackets around it. A tall view, where the figure moves, goes without."""
    ring = lambda rx, ry: [(round(BAY_X + rx * math.cos(math.radians(angle)), 3), round(BAY_FEET + ry * math.sin(math.radians(angle)), 3))  # noqa: E731
                           for angle in range(0, 360, 9)]
    line = stroke(solid(rgb(OLIVE, 0.55)), 1.5)
    faint = stroke(solid(rgb(OLIVE, 0.3)), 1.2)
    paths = [path("ring", ring(104, 17), stroke=line), path("ring-inner", ring(70, 11), stroke=faint)]
    for index, angle in enumerate((0, 90, 180, 270)):
        cos, sin = math.cos(math.radians(angle)), math.sin(math.radians(angle))
        paths.append(path(f"tick{index}", [(round(BAY_X + 104 * cos, 3), round(BAY_FEET + 17 * sin, 3)),
                                           (round(BAY_X + 122 * cos, 3), round(BAY_FEET + 20 * sin, 3))], closed=False, stroke=line))
    left, right, top, bottom, arm = BAY_X - 132, BAY_X + 132, 0.226 * 720 - 26, BAY_FEET + 30, 20
    for index, (x, y, dx, dy) in enumerate(((left, top, 1, 1), (right, top, -1, 1), (right, bottom, -1, -1), (left, bottom, 1, -1))):
        paths.append(path(f"bracket{index}", [(x + dx * arm, y), (x, y), (x, y + dy * arm)], closed=False, stroke=faint))
    return vector("setup-bay", {**absolute(left=0, top=0, width=CANVAS_W, height=720), "display": keyword("block"),
                                "pointer-events": keyword("none")}, paths)


def proposal(doc: Document, ident: str, field: str, *, value: dict, expected: str = "", kind: str | None = None) -> str:
    action = f"setup.{ident}"
    doc.actions[action] = {"operation": "player.setup.apply", "arguments": {
        "field": field, "value": value, "catalog": {"state": "setup.catalog"},
        "expected": {"state": expected} if expected else ""}}
    if kind is not None:
        doc.actions[action]["input"] = kind
    return action


def rail_row(doc: Document) -> tuple[dict, list[str]]:
    """Stock hues, vector chips, 44 dp targets and a selected chip's taller ink."""
    swatches = ((1, 0, 0), (1, .501, 0), (1, 1, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1), (1, 0, .501))
    shape = [(6, 1), ({"fraction": 1}, 1), ({"fraction": 1}, {"fraction": 1, "dp": -1}),
             (0, {"fraction": 1, "dp": -1}), (0, 7)]
    children = [vector("setup-rail-plate", FULL, [path("plate", shape, fill=solid(rgb(OLIVE, .12)),
                                                                  stroke=stroke(solid(rgb(OLIVE, .32)), 1))]),
                # In the rows' own label style, on the row's middle line: it stood
                # smaller, in olive, at the row's top.
                label("setup-rail-label", "#str_200981", {**absolute(left=21, top=13, width=VALUE_X-29, height=26),
                      **typeface("lowpixel", 15, 26, [1, 1, 1, 0.8])})]
    controls = []
    for row, tint in enumerate(swatches):
        ident = f"setup-rail-{row}"
        selected = {"op": "==", "args": [{"state": "setup.rail"}, row]}
        ring = vector(f"{ident}-ring", {**absolute(left=13, top=11, width=18, height=22), "opacity": number(0)}, [
            path("ring", [(.75, .75), (17.25, .75), (17.25, 21.25), (.75, 21.25)], stroke=stroke(solid(rgb(ORANGE)), 1.5))])
        chip = vector(f"{ident}-chip", {**absolute(left=16, top=17, width=12, height=10), "transform": transform()}, [
            path("chip", [(0, 0), (12, 0), (12, 10), (0, 10)], fill=solid([*tint, 1]))])
        doc.bind(f"{ident}.selected", f"{ident}-chip", "transform",
                 [0, {"op": "select", "args": [selected, -3, 0]}, 1, {"op": "select", "args": [selected, 1.6, 1]}, 0])
        states = doc.states(ident, {state: [(f"{ident}-ring", "opacity", number(alpha))]
                                  for state, alpha in (("default", 0), ("hover", .6), ("focus", 1), ("pressed", 1), ("disabled", 0))})
        action = proposal(doc, f"rail{row}", "rail", value=row, expected="setup.observed_rail")
        children.append(group(ident, absolute(left=VALUE_X + 44*row, top=4, width=44, height=44), [ring, chip],
                              control={"role": "button", "label": "#str_200981", "action": action, "states": states,
                                       "context": {"op": "numberText", "args": [{"state": "setup.catalog"}]}}))
        controls.append(ident)
    return group("setup-rail", {"position": keyword("relative"), "width": length(WIDTH), "height": length(52),
                                 "flex-shrink": number(0)}, children), controls


def stack_rail(doc: Document, row: dict) -> None:
    """The rail colour row above 100 % text: its label across the row on a
    line that grows with the text, the swatches, furniture, under it from the
    label's edge (retained_mp_menus.stack_row's layout)."""
    larger = {"op": ">", "args": [TEXT, 1]}
    line = text_times(26)
    height = row["properties"]["height"]["value"]
    doc.bind("text.setup-rail.height", "setup-rail", "height", {"op": "select", "args": [larger, text_sum(height, line), height]})
    caption = next(child for child in row["children"] if child["id"] == "setup-rail-label")["properties"]
    for language, text in table_text("#str_200981").items():
        if text_width(caption["font-family"]["value"], text, caption["font-size"]["value"] * 2) > WIDTH - 33:
            raise SystemExit(f"the rail colour's label in {language} does not hold one line of its row at 200 % text")
    doc.bind("text.setup-rail-label.width", "setup-rail-label", "width", {"op": "select", "args": [larger, WIDTH - 33, caption["width"]["value"]]})
    doc.bind("text.setup-rail-label.height", "setup-rail-label", "height", {"op": "select", "args": [larger, line, caption["height"]["value"]]})
    # The label stands on the row's middle line at 100 %, and on its own line at the row's top above it.
    doc.bind("text.setup-rail-label.top", "setup-rail-label", "top", {"op": "select", "args": [larger, 0, caption["top"]["value"]]})
    for child in row["children"]:
        if child.get("control"):
            index = int(child["id"].rsplit("-", 1)[1])
            props = child["properties"]
            doc.bind(f"text.{child['id']}.left", child["id"], "left", {"op": "select", "args": [larger, 21 + 44 * index, props["left"]["value"]]})
            doc.bind(f"text.{child['id']}.top", child["id"], "top",
                     {"op": "select", "args": [larger, text_sum(props["top"]["value"], line), props["top"]["value"]]})


def player_setup(doc: Document) -> dict:
    """A same-level editor: stock leading rows and right-side animated preview.

    Back closes this editor onto PLAYER SETUP; committed changes already apply,
    and unfinished field text is discarded by the ordinary hidden-owner rules.
    The title and bands remain on the Multiplayer page throughout.
    """
    doc.state.update({"setup.open": {"type": "boolean", "initial": False},
                      "setup.catalog": {"type": "number", "initial": 0},
                      "setup.model_row": {"type": "number", "initial": -1},
                      "setup.model_count": {"type": "number", "initial": 0},
                      "setup.previous": {"type": "boolean", "initial": False},
                      "setup.next": {"type": "boolean", "initial": False},
                      "setup.rotation": {"type": "number", "initial": 200},
                      "setup.rail": {"type": "number", "initial": -1}})
    for state in ("name", "clan", "observed_name", "observed_clan", "observed_model", "observed_rail",
                  "model_name", "body", "head", "skin", "head_skin", "notice"):
        doc.state[f"setup.{state}"] = {"type": "string", "initial": ""}
    rows = []
    for ident, key, field, size, empty in (("name", "#str_200078", "name", 23, False),
                                          ("clan", "#str_200079", "clan", 12, True)):
        node = f"setup-{ident}"
        row = text_row(doc, node, key, WIDTH, VALUE_X, f"setup.{field}", "unused", size, ROW_H, allow_empty=empty)
        # Text Enter carries its exact proposal to the session; it is never
        # reconstructed by reading a later GUI-state string from a bare verb.
        generated_action = row["control"]["action"]
        doc.actions.pop(generated_action)
        row["control"]["action"] = proposal(doc, ident, field, value={"input": "value"},
                                             expected=f"setup.observed_{field}", kind="string")
        rows.append(row)
    options = []
    for row in range(MODEL_ROWS):
        doc.state[f"setup.model.{row}.name"] = {"type": "string", "initial": ""}
        options.append(({"state": f"setup.model.{row}.name"}, row))
    action = proposal(doc, "model", "model", value={"input": "value"}, expected="setup.observed_model", kind="number")
    rows.append(value_row(doc, "setup-model", "#str_200080", "choice", WIDTH, VALUE_X,
                          value={"state": "setup.model_row"}, action=action, entries=options,
                          count={"state": "setup.model_count"}, row_h=ROW_H, placement="setup-fields", wrap=True))
    pages = []
    for ident, key, value, allowed in (("previous", "#str_42885", -1, "setup.previous"), ("next", "#str_42886", 1, "setup.next")):
        action = proposal(doc, ident, "page", value=value)
        button = in_game_plate(doc, f"setup-model-{ident}", key, 0 if value < 0 else WIDTH/2+6, 0,
                               width=WIDTH/2-6, action=action)
        button["control"]["enabled"] = True
        doc.bind(f"setup-model-{ident}.enabled",f"setup-model-{ident}","enabled",{"state":allowed})
        button["control"]["context"] = {"op": "numberText", "args": [{"state": "setup.catalog"}]}
        pages.append(button)
    rows.append(group("setup-model-pages", {"position": keyword("relative"), "width": length(WIDTH), "height": length(44),
                                            "flex-shrink": number(0)}, pages))
    action = proposal(doc, "rotation", "rotation", value={"input": "value"}, kind="number")
    rows.append(value_row(doc, "setup-rotation", ROTATION_LABEL, "slider", WIDTH, VALUE_X,
                          value={"state": "setup.rotation"}, action=action, slider=(0, 360, 5, 0), row_h=ROW_H, wrap=True))
    rail, rail_controls = rail_row(doc)
    rows.append(rail)
    own = [row["id"] for row in rows]
    # The page held the player's own name, model and rail color. It now has
    # three tabs (user note, 11 November 2026): PLAYER, those rows and the
    # handicap; OPPONENTS and TEAMMATES, how each side is drawn: its outline,
    # rim light, their color, its brightskin and that color, the settings the
    # in-game card's Settings page has (retained_mp_menus.settings_page), set
    # the same way (settings.player.set, read back from each CVar). The
    # outline's width, which both sides share, stands on OPPONENTS, and
    # TEAMMATES says that its rows apply in team modes.
    rows.append(value_row(doc, "setup-handicap", "#str_223001", "slider", WIDTH, VALUE_X, value=cvar_state(doc, "ui_handicap", "number"),
                          action=setting_action(doc, "ui_handicap", "number"), slider=(1, 100, 1, 0), row_h=ROW_H, wrap=True))
    own.append("setup-handicap")
    sides = {1: [], 2: []}
    # OPPONENTS' first row forces a model on every other player outside team
    # modes (g_forceModel), from the same catalog page as the player's own
    # model, through the same proposal: Off, then the page's models. The
    # in-game card's Settings page forces the two teams' models, which depend
    # on the team the player is on.
    doc.state.update({"setup.force_row": {"type": "number", "initial": -1}, "setup.observed_force": {"type": "string", "initial": ""}})
    forced = [(("#str_41207", 0), -1)] + [({"state": f"setup.model.{row}.name"}, row) for row in range(MODEL_ROWS)]
    rows.append(value_row(doc, "setup-enemy-model", "#str_41204", "choice", WIDTH, VALUE_X, value={"state": "setup.force_row"},
                          action=proposal(doc, "force", "force_model", value={"input": "value"}, expected="setup.observed_force",
                                          kind="number"),
                          entries=forced, count={"op": "+", "args": [{"state": "setup.model_count"}, 1]}, row_h=ROW_H,
                          placement="setup-fields", wrap=True))
    sides[1].append("setup-enemy-model")
    for tab, side in ((1, "enemy"), (2, "team")):
        for label_key, enemy, team, list_key, values in APPEARANCE_ROWS:
            cvar = enemy if tab == 1 else team
            node = f"setup-{side}-{cvar.split('_', 2)[2].replace('_' + side, '').replace('_', '-')}"
            rows.append(value_row(doc, node, label_key, "choice", WIDTH, VALUE_X, value=cvar_state(doc, cvar, "string"),
                                  action=setting_action(doc, cvar, "string"),
                                  entries=[((list_key, option), value) for option, value in enumerate(values)], row_h=ROW_H,
                                  placement="setup-fields", wrap=True))
            sides[tab].append(node)
    rows.append(value_row(doc, "setup-outline-width", "#str_41211", "choice", WIDTH, VALUE_X,
                          value=cvar_state(doc, "cl_player_outline_width", "string"), action=setting_action(doc, "cl_player_outline_width", "string"),
                          entries=[(("#str_41214", option), value) for option, value in enumerate(OUTLINE_WIDTHS)], row_h=ROW_H,
                          placement="setup-fields", wrap=True))
    sides[1].append("setup-outline-width")
    team_note = label("setup-team-note", "#str_41212", {"position": keyword("relative"), "width": length(WIDTH), "height": length(40),
                                                        **typeface("lowpixel", 14, 20, [1, 1, 1, 0.6]), "white-space": keyword("normal"),
                                                        "overflow": keyword("hidden"), "display": keyword("block")})
    rows.append(team_note)
    sides[2].append("setup-team-note")
    value_rows = [ident for ident in sides[1] + sides[2] if ident != "setup-team-note"] + ["setup-handicap"]
    # Each row shows on its tab alone.
    doc.state["setup.tab"] = {"type": "number", "initial": 0}
    by_id = {row["id"]: row for row in rows}
    for tab, idents in ((0, own), (1, sides[1]), (2, sides[2])):
        for ident in idents:
            row = by_id[ident]
            shown = row["properties"].get("display", keyword("block"))["value"]
            on_tab = {"op": "==", "args": [{"state": "setup.tab"}, tab]}
            if ident == "setup-model-pages":
                # PREVIOUS and NEXT page a catalog of more than 128 models: with
                # one page neither can do anything, and the row gives its place.
                on_tab = {"op": "&&", "args": [on_tab, {"op": "||", "args": [{"state": "setup.previous"}, {"state": "setup.next"}]}]}
                row["properties"]["display"] = keyword("none")
            else:
                row["properties"]["display"] = keyword(shown if tab == 0 else "none")
            doc.bind(f"{ident}.tab", ident, "display", {"op": "select", "args": [on_tab, shown, "none"]})
    # The tabs are the in-game card's strip (retained_mp_menus.tab_strip): the
    # shown tab rises in the tab green inside its outline, each tab is as wide
    # as its label in the running language and a share of the rest, and the Q
    # and E keycaps at its ends, or a controller's shoulder buttons, step
    # through them (onTabPrevious, onTabNext) and take the focus to the tab.
    # Three plates with an underline stood here first; their labels had to
    # shrink to 13 dp to fit the longest translations.
    from retained_mp_menus import TAB_RISE, tab_strip
    names = (("player", "#str_200255"), ("opponents", "#str_42037"), ("teammates", "#str_42038"))
    shows = lambda tab: {"op": "==", "args": [{"state": "setup.tab"}, tab]}  # noqa: E731
    focus_shown = [{"op": "if", "condition": shows(tab), "then": [{"op": "focus", "control": f"tab-{ident}"}]}
                   for tab, (ident, _key) in enumerate(names)]
    for tab, (ident, _key) in enumerate(names):
        doc.events[f"tab_{ident}"] = [{"op": "setState", "values": {"setup.tab": tab}}]
    for event, order in (("onTabNext", (1, 2, 0)), ("onTabPrevious", (2, 0, 1))):
        assert event not in doc.events, event
        target = {"op": "select", "args": [shows(0), order[0], {"op": "select", "args": [shows(1), order[1], order[2]]}]}
        doc.events[event] = [{"op": "if", "condition": {"state": "setup.open"},
                              "then": [{"op": "setState", "values": {"setup.tab": target}}, *focus_shown]}]
    strip = tab_strip(doc, WIDTH, [(ident, key, None) for ident, key in names], top=0, state="setup.tab")
    rows.insert(0, group("setup-tabs", {"position": keyword("relative"), "width": length(WIDTH), "height": length(TAB_RISE + 8),
                                        "flex-shrink": number(0)}, strip))
    # A colour's row shows the colour beside its name.
    for tab, side in ((1, "enemy"), (2, "team")):
        for _label, enemy, team, _list, values in APPEARANCE_ROWS:
            cvar = enemy if tab == 1 else team
            if cvar.endswith("_color"):
                ident = f"setup-{side}-{cvar.split('_', 2)[2].replace('_' + side, '').replace('_', '-')}"
                by_id[ident]["children"] += colour_chip(doc, ident, cvar_state(doc, cvar, "string"), values)
    doc.session("playerSetupClose", "playerSetupClose")
    rows.append(in_game_plate(doc, "setup-close", "#str_230045", 0, 0, width=WIDTH, action="playerSetupClose"))
    # Let the rows flow in a viewport: enlarged text and narrow windows retain
    # every control rather than pushing the last one under the prompt bar.
    for row in rows:
        row["properties"].update({"position": keyword("relative"), "left": length(0), "top": length(0),
                                   "flex-shrink": number(0)})
    # Above 100 % text each setting's value stands under its label and the
    # rows scroll (section 2's reflow); the plates are furniture.
    for row in rows:
        if row["id"] in ("setup-name", "setup-clan", "setup-model", "setup-rotation", *value_rows):
            stack_row(doc, row, WIDTH, VALUE_X, ROW_H)
        elif row["id"] == "setup-team-note":
            doc.bind("text.setup-team-note.height", "setup-team-note", "height", text_times(40))
        elif row["id"] == "setup-rail":
            stack_rail(doc, row)
        else:
            fixed_text(doc, row)
    fields = group("setup-fields", {**absolute(left=U*58, top=U*(SETUP_TOP+28), width=WIDTH+16, height=U*FIELDS_H),
                                      "overflow": keyword("auto"), "display": keyword("flex"), "flex-direction": keyword("column"),
                                      "row-gap": length(6)}, rows)
    model = {"id": "setup-preview", "type": "model", "properties": {**absolute(left=0, top=0, width=CANVAS_W, height=720),
                                                                                        "pointer-events": keyword("none")},
             "scene": {"parts": [
                 {"model": {"state": "setup.body"}, "skin": {"state": "setup.skin"}, "anim": "idle", "animClass": "player_ui_anims"},
                 {"model": {"state": "setup.head"}, "skin": {"state": "setup.head_skin"}, "joint": "chest", "anim": "idle",
                  "animClass": {"state": "setup.head"}},
                 {"model": "weapon_machinegun_world", "joint": "PST_ATTACHER", "anim": "idle", "animClass": "weapon_blaster_gui"}],
                 "origin": [-50, 0, -40], "rotation": [0, {"state": "setup.rotation"}, 0], "camera": [-160, 65, -5],
                 "lights": [{"origin": [-100, -50, -60], "colour": [.45, .25, .125]}, {"origin": [-120, 120, 120], "colour": [.8, .9, 1]}]}}
    heading = label("setup-heading", "#str_231122", {**absolute(left=U*67, top=U*SETUP_TOP, width=WIDTH, height=32),
                                                     **typeface("marine", 20, 32, rgb(OLIVE))})
    current = label("setup-current-model", "#str_230030", {**absolute(left=U*421, top=U*380, width=U*200, height=48),
                                                          **typeface("lowpixel", 16, 22, rgb(VALUE)), "overflow": keyword("hidden"),
                                                          "text-align": keyword("center"), "text-overflow": keyword("ellipsis")})
    # On OPPONENTS, while a model is forced, the preview is that model: the
    # session names its parts (setup.force_*) as it does the player's own.
    for state in ("force_body", "force_head", "force_skin", "force_head_skin", "force_name"):
        doc.state[f"setup.{state}"] = {"type": "string", "initial": ""}
    forced_shown = {"op": "&&", "args": [{"op": "==", "args": [{"state": "setup.tab"}, 1]},
                                         {"op": "!=", "args": [{"state": "setup.force_body"}, ""]}]}
    forced = {"id": "setup-preview-forced", "type": "model",
              "properties": {**absolute(left=0, top=0, width=CANVAS_W, height=720), "pointer-events": keyword("none"), "display": keyword("none")},
              "scene": {"parts": [
                  {"model": {"state": "setup.force_body"}, "skin": {"state": "setup.force_skin"}, "anim": "idle", "animClass": "player_ui_anims"},
                  {"model": {"state": "setup.force_head"}, "skin": {"state": "setup.force_head_skin"}, "joint": "chest", "anim": "idle",
                   "animClass": {"state": "setup.force_head"}},
                  {"model": "weapon_machinegun_world", "joint": "PST_ATTACHER", "anim": "idle", "animClass": "weapon_blaster_gui"}],
                  "origin": [-50, 0, -40], "rotation": [0, {"state": "setup.rotation"}, 0], "camera": [-160, 65, -5],
                  "lights": [{"origin": [-100, -50, -60], "colour": [.45, .25, .125]}, {"origin": [-120, 120, 120], "colour": [.8, .9, 1]}]}}
    model["properties"]["display"] = keyword("block")
    doc.bind("setup-preview.display", "setup-preview", "display", {"op": "select", "args": [forced_shown, "none", "block"]})
    doc.bind("setup-preview-forced.display", "setup-preview-forced", "display", {"op": "select", "args": [forced_shown, "block", "none"]})
    doc.bind("setup-current-model.text", "setup-current-model", "text",
             {"op": "select", "args": [forced_shown, {"state": "setup.force_name"}, {"state": "setup.model_name"}]})
    bay = preview_bay()
    notice = label("setup-notice", "#str_230030", {**absolute(left=U*67, top=U*413, width=U*520, height=40),
                                                   **typeface("lowpixel", 14, 20, rgb(VALUE)), "overflow": keyword("hidden")})
    doc.bind("setup-notice.text", "setup-notice", "text", {"state": "setup.notice"})
    # The heading is furniture; the model's name and the notice are content,
    # whose lines grow with the text.
    fixed_text(doc, heading)
    for node in (current, notice):
        doc.bind(f"text.{node['id']}.height", node["id"], "height", text_times(node["properties"]["height"]["value"]))
    pane = group("setup-panel", {**absolute(left=0, top=0, width=CANVAS_W, height=720), "display": keyword("none")},
                 [bay, model, forced, heading, current, fields, notice])
    doc.bind("setup-panel.display", "setup-panel", "display", {"op": "select", "args": [{"state": "setup.open"}, "block", "none"]})
    # A tall view sizes the rows for the tab with the most: the player's.
    tall_player_setup(doc, pane, [row for row in rows if row["id"] in ("setup-tabs", "setup-close", *own)])
    # The forced model's preview stands where the player's does in a tall
    # view too, and the bay stays with the landscape figure.
    from build_retained_screens import tall_condition
    for binding in [binding for binding in doc.bindings if binding["node"] == "setup-preview" and binding["property"] != "display"]:
        doc.bind(binding["id"].replace("setup-preview", "setup-preview-forced"), "setup-preview-forced", binding["property"], binding["value"])
    doc.bind("setup-bay.display", "setup-bay", "display", {"op": "select", "args": [tall_condition(doc), "none", "block"]})
    return pane


# A tall view's Player Setup (sections 3 and 13.3, "adapt, don't shrink"):
# in the Multiplayer page's tall column, which runs the view's height, the
# heading and the rows stand across the column up to 1.6 times their size,
# the notice under them, and the model preview fills the rest of the height
# down to the bottom band, the figure centred with its name under it.
TALL_SETUP_MOST = 1.6
TALL_SETUP_TOP, TALL_SETUP_BOTTOM = 110.0, 100.0   # dp: under the docked top band's title, over the docked bottom band
FIGURE = (0.83, 0.226, 0.527)    # the figure's middle across the preview, its top and its height down it (fractions of the 4:3 box)
TALL_PREVIEW_LEAST = 260.0       # dp: the figure's room the rows' scale leaves at least


def tall_player_setup(doc: Document, pane: dict, rows: list) -> None:
    from build_retained_screens import TALL_MARGIN, VIEW_HEIGHT, tall_bind, tall_condition, tall_scale
    nodes = {node["id"]: node for node in pane["children"]}
    heading, fields, current, notice, model = (nodes[ident] for ident in
                                               ("setup-heading", "setup-fields", "setup-current-model", "setup-notice", "setup-preview"))
    rows_h = sum(row["properties"]["height"]["value"] for row in rows) + 6 * (len(rows) - 1)
    head_h = heading["properties"]["height"]["value"]
    notice_h = notice["properties"]["height"]["value"]
    label_h = current["properties"]["height"]["value"]
    # The rows' scale: as large as leaves the figure its least room, from the
    # landscape size up to 1.6 times it.
    fixed = TALL_SETUP_TOP + 8 + 4 + 8 + TALL_PREVIEW_LEAST + TALL_SETUP_BOTTOM
    scale = {"op": "max", "args": [1, {"op": "min", "args": [TALL_SETUP_MOST, {"op": "/", "args": [
        text_sum(-fixed, VIEW_HEIGHT), round(head_h + rows_h + notice_h + label_h, 4)]}]}]}
    times = lambda value: {"op": "*", "args": [scale, value]}  # noqa: E731
    tall_bind(doc, pane, "height", VIEW_HEIGHT)
    for node in (heading, fields, notice, current):
        tall_scale(doc, node, scale, own=("width", "height"))
    # The runtime places and sizes a field's caret, selection and composition
    # in its scaled viewport: they take no scale of their own.
    from build_retained_screens import walk_nodes
    inks = {node["id"] for node in walk_nodes(fields) if node["id"].endswith(("-caret", "-selection", "-composition"))}
    doc.bindings[:] = [binding for binding in doc.bindings if not (binding["node"] in inks and binding["property"] == "transform")]
    for node in walk_nodes(fields):
        if node["id"] in inks:
            node["properties"].pop("transform", None)
    tall_bind(doc, heading, "left", TALL_MARGIN)
    tall_bind(doc, heading, "top", TALL_SETUP_TOP)
    fields_top = text_sum(TALL_SETUP_TOP + 8, times(head_h))
    # The notice and the model's name take their lines at the running text
    # size; the rows, which scroll, leave the figure its least room.
    notice_size = times({"op": "*", "args": [notice_h, {"state": "view.text"}]})
    label_size = times({"op": "*", "args": [label_h, {"state": "view.text"}]})
    fields_h = {"op": "min", "args": [times(rows_h), text_sum(-(4 + 8 + TALL_PREVIEW_LEAST + 8 + TALL_SETUP_BOTTOM), VIEW_HEIGHT,
                                                              *({"op": "*", "args": [-1, part]} for part in (fields_top, notice_size, label_size)))]}
    tall_bind(doc, fields, "left", TALL_MARGIN)
    tall_bind(doc, fields, "top", fields_top)
    tall_bind(doc, fields, "height", fields_h)
    notice_top = text_sum(4, fields_top, fields_h)
    tall_bind(doc, notice, "left", TALL_MARGIN)
    tall_bind(doc, notice, "top", notice_top)
    tall_bind(doc, notice, "width", CANVAS_W - 2 * TALL_MARGIN)
    # The figure in the room left, its 4:3 preview grown so that the figure
    # fills it, between its landscape size and 1.6 times it.
    room_top = text_sum(8, notice_top, notice_size)
    room = text_sum(-TALL_SETUP_BOTTOM - 8, VIEW_HEIGHT, {"op": "*", "args": [-1, room_top]}, {"op": "*", "args": [-1, label_size]})
    width, height = model["properties"]["width"]["value"], model["properties"]["height"]["value"]
    grown = {"op": "max", "args": [0.6, {"op": "min", "args": [TALL_SETUP_MOST, {"op": "/", "args": [room, round(FIGURE[2] * height, 4)]}]}]}
    tall_bind(doc, model, "width", {"op": "*", "args": [width, grown]})
    tall_bind(doc, model, "height", {"op": "*", "args": [height, grown]})
    tall_bind(doc, model, "left", text_sum(CANVAS_W / 2, {"op": "*", "args": [-FIGURE[0] * width, grown]}))
    tall_bind(doc, model, "top", text_sum(room_top, {"op": "*", "args": [-FIGURE[1] * height, grown]}))
    # The model's name under the figure, centred on it.
    tall_bind(doc, current, "left", text_sum(CANVAS_W / 2, times(-current["properties"]["width"]["value"] / 2)))
    tall_bind(doc, current, "top", text_sum(room_top, {"op": "*", "args": [FIGURE[2] * height, grown]}, 8))
    tall_condition(doc)
