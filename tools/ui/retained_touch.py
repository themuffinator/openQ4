"""The touch variant's simplified menus (docs/dev/ui/interface-variants.md;
docs/dev/ui-visual-design.md sections 13.2, 13.3 and 13.6; roadmap AND-04.3).

Every desktop menu lays out on the 720 dp view-height canvas, which on a
phone spans few of the device's own dp: a landscape phone gives about 0.57
physical dp to each canvas dp, a portrait one 0.43, so the desktop
compositions are too small to read or to touch there. The touch variant
replaces them with one simple column sized in the device's own dp, which
reads the same in landscape and portrait, on a phone or a tablet:

- buttons the column's width (at most 440 dp), at least 56 dp tall, with a
  16 dp Marine label that wraps, 10 dp apart; the player's text size (150 %
  on a new phone install) applies to every label;
- one screen title above them;
- the column scrolls when it outgrows the view, by a finger's drag.

Menu levels (the title, the pauses, Single Player, the campaigns,
Multiplayer and SETTINGS) keep their document: touch_menu() adds the column
over the composition, hidden outside the touch variant, its buttons twins
of the composition's (the same labels and actions, shown and enabled under
the same conditions), and sets the composition aside there. Dialogs and
notices stay above it, as they were. Its sizes bind to view.physical, the
device dp a canvas dp spans.

The settings pages (GAME OPTIONS, CONTROLS, AUDIO) are their own documents
in the touch variant (guis/menu/touch/*.rui), which the retained adapter
loads in place of the desktop page while the variant is touch. They have no
canvas, so a dp there is the device's: touch_subpage() frames the page's own
rows, built by its module at touch_geometry()'s 368 by 56 dp, as a header
(Back and the title) over a list of the page's categories, and each category
as a scrolling list of its rows, as many abreast as the view holds."""
from __future__ import annotations

import contextlib

import build_retained_screens as b
from build_retained_screens import (OLIVE, ORANGE, U, Document, absolute, colour, font, group, keyword, label, length, number,
                                    path, rgb, solid, stroke, track, vector, walk_nodes)

TARGET, PHYSICAL, TEXT, DEVICE = "view.target", "view.physical", "view.text", "view.device"
# The touch variant: the runtime's target (a 48 dp touch target in document
# dp) is positive there and 0 in every other variant.
TOUCH = {"op": ">", "args": [{"state": TARGET}, 0]}
COLUMN_DP = 440.0          # the menu column's widest, in the device's dp
MARGIN_DP = 16.0           # around the column
PAD_DP = 16.0              # above and below a button's label
LINE_DP = 24.0             # a button label's line
LABEL_DP = 16.0            # a button label
GAP_DP = 10.0              # between buttons
TITLE_DP = 20.0            # a screen's title
HEADER_DP = 64.0           # a settings page's header
ROW_W_DP, ROW_H_DP = 368.0, 56.0   # a settings row in the touch pages
VALUE_ROOM_DP = 172.0               # the row's value: a choice's text, a slider's track and number
TOUCH_PAGES = {"game": "touch_game", "controls": "touch_controls", "audio": "touch_audio", "system": "touch_system"}
FOOTER_PAD_DP = 12.0                # around the footer's message and button


def dp(value: float) -> dict:
    """`value` of the device's dp in canvas dp."""
    return {"op": "/", "args": [value, {"op": "max", "args": [{"state": PHYSICAL}, 0.05]}]}


# The text size a device class starts at (RetainedUI_DeviceTextDefault):
# the canvas needs it, the touch layouts divide it out.
DEVICE_TEXT = {"op": "select", "args": [{"op": "==", "args": [{"state": DEVICE}, "phone"]}, 1.5,
                                        {"op": "select", "args": [{"op": "||", "args": [
                                            {"op": "==", "args": [{"state": DEVICE}, "tablet"]},
                                            {"op": "==", "args": [{"state": DEVICE}, "handheld"]}]}, 1.25, 1]}]}
# A text length on the canvas, which the runtime grows by the text size:
# the factor that leaves the touch documents' size, the player's choice over
# the device's starting size.
TEXT_NORMAL = {"op": "/", "args": [{"op": "max", "args": [1, {"op": "/", "args": [{"state": TEXT}, DEVICE_TEXT]}]},
                                   {"op": "max", "args": [{"state": TEXT}, 0.05]}]}
TEXT_LENGTHS = ("font-size", "line-height")


def declare_sources(result: dict) -> None:
    """The view sources the touch layout reads in a canvas document."""
    states = result.setdefault("state", {})
    states.setdefault(TARGET, {"type": "number", "initial": 0, "view": "target"})
    states.setdefault(PHYSICAL, {"type": "number", "initial": 1, "view": "physical"})
    states.setdefault(TEXT, {"type": "number", "initial": 1, "view": "text"})
    states.setdefault(DEVICE, {"type": "string", "initial": "", "view": "device"})
    states.setdefault(TEXT, {"type": "number", "initial": 1, "view": "text"})
    states.setdefault(DEVICE, {"type": "string", "initial": "", "view": "device"})


class Sizes:
    """Lengths in the device's dp: bound to view.physical in a canvas
    document, as authored in a touch document, whose dp is the device's."""

    def __init__(self, bindings: list | None):
        self.bindings = bindings

    def set(self, node: dict, sizes: dict) -> dict:
        for prop, value in sizes.items():
            node["properties"][prop] = length(value)
            if self.bindings is not None:
                bound = dp(value)
                if prop in TEXT_LENGTHS:
                    bound = {"op": "*", "args": [bound, TEXT_NORMAL]}
                self.bindings.append({"id": f"touch.{node['id']}.{prop}", "node": node["id"], "property": prop, "value": bound})
        return node


def _states(timelines: list, ident: str) -> dict:
    """A touch button's feedback: hover and focus light its wash and turn its
    label orange, focus with the rail; a press lights it at once."""
    def keys(wash: float, rail: float, tint: list) -> list:
        return [(f"{ident}-wash", "opacity", number(wash)), (f"{ident}-rail", "opacity", number(rail)),
                (f"{ident}-label", "color", colour(tint))]
    looks = {"default": keys(0, 0, [1, 1, 1, 0.92]), "hover": keys(0.6, 0, rgb(ORANGE)), "focus": keys(0.8, 1, rgb(ORANGE)),
             "pressed": keys(1, 1, rgb(ORANGE)), "disabled": keys(0, 0, [1, 1, 1, 0.35])}
    durations = {"default": 200, "hover": 1, "focus": 80, "pressed": 1, "disabled": 150}
    ids = {}
    for state, entries in looks.items():
        timeline = f"{ident}.{state}"
        timelines.append({"id": timeline, "durationMs": durations[state], "tracks": [
            track(node, prop, [(0, value), (durations[state], value)]) for node, prop, value in entries]})
        ids[state] = timeline
    return ids


def button(sizes: Sizes, timelines: list, ident: str, key: str, control: dict, *, width=None) -> dict:
    """A big touch button: the stock plate's olive fill and rail across its
    width, its label wrapping inside it."""
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    cut = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (12, {"fraction": 1}), (0, {"fraction": 1, "dp": -12})]
    # Opaque, so whatever stands behind (a scene, a menu) never shows through
    # the label.
    plate = vector(f"{ident}-plate", dict(b.FULL), [path("base", cut, fill=solid([0.035, 0.042, 0.025, 0.94])),
                                                     path("plate", cut, fill=solid(rgb(OLIVE, 0.22)),
                                                          stroke=stroke(solid(rgb(OLIVE, 0.6)), 1.5))])
    wash = vector(f"{ident}-wash", {**b.FULL, "opacity": number(0)}, [
        path("wash", cut, fill=b.linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(ORANGE, 0.35)), (1, rgb(ORANGE, 0.06))]))])
    rail = vector(f"{ident}-rail", {**absolute(left=0, top=0), "width": length(4), "height": length(100, "%"), "opacity": number(0)},
                  [path("rail", whole, fill=solid(rgb(ORANGE)))])
    sizes.set(rail, {"width": 4})
    text = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"), "font-family": font("marine"),
                 "color": colour([1, 1, 1, 0.92]), "white-space": keyword("normal"), "pointer-events": keyword("none")})
    sizes.set(text, {"font-size": LABEL_DP, "line-height": LINE_DP, "padding-top": PAD_DP, "padding-bottom": PAD_DP,
                     "padding-left": 22, "padding-right": 16})
    node = group(ident, {"position": keyword("relative"), "display": keyword("block"), "opacity": number(1), "flex-shrink": number(0),
                         **({"width": length(100, "%")} if width is None else {})},
                 [plate, wash, rail, text], control={"role": "button", "label": key, **control, "states": _states(timelines, ident)})
    sizes.set(node, {"margin-bottom": GAP_DP, **({"width": width} if width is not None else {})})
    return node


def heading(sizes: Sizes, ident: str, key: str) -> dict:
    """A screen or section title in the Marine face."""
    node = label(ident, key, {"position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"),
                              "font-family": font("marine"), "color": colour([1, 1, 1, 0.7]), "white-space": keyword("normal"),
                              "pointer-events": keyword("none"), "flex-shrink": number(0)})
    return sizes.set(node, {"font-size": TITLE_DP, "line-height": 28, "margin-top": 8, "margin-bottom": 12, "padding-left": 4})


# --------------------------------------------------------------- menu levels

def _find(node: dict, ident: str):
    return next((item for item in walk_nodes(node) if item["id"] == ident), None)


def _binding(result: dict, node: str, prop: str):
    return next((entry for entry in result.get("bindings", []) if entry["node"] == node and entry["property"] == prop), None)


def hide_in_touch(result: dict, ident: str) -> None:
    """The node gives way to the touch column there; elsewhere it is as it was."""
    node = _find(result["root"], ident)
    if node is None:
        raise SystemExit(f"{result['id']}: no node {ident} to set aside in the touch variant")
    existing = _binding(result, ident, "display")
    if existing is not None:
        existing["value"] = {"op": "select", "args": [TOUCH, "none", existing["value"]]}
        return
    shown = node["properties"].setdefault("display", keyword("block"))["value"]
    result.setdefault("bindings", []).append({"id": f"touch.{ident}.display", "node": ident, "property": "display",
                                              "value": {"op": "select", "args": [TOUCH, "none", shown]}})


def twin(result: dict, sizes: Sizes, source: str) -> dict:
    """A touch button doing what the composition's control `source` does,
    shown and enabled when it is."""
    found = _find(result["root"], source)
    if found is None or "control" not in found:
        raise SystemExit(f"{result['id']}: no control {source} for the touch column")
    control = found["control"]
    ident = f"touch-{source}"
    node = button(sizes, result.setdefault("timelines", []), ident, control["label"],
                  {key: control[key] for key in ("action", "event") if key in control})
    for prop in ("display", "enabled"):
        bound = _binding(result, source, prop)
        if bound is not None:
            result["bindings"].append({"id": f"touch.{ident}.{prop}", "node": ident, "property": prop, "value": bound["value"]})
    # A choice among buttons (a difficulty) lights the chosen one: the twin
    # outlines itself in orange while its source's lit art shows.
    lit = next((bound for name in (f"{source}-lit", f"{source}-plate-lit") if (bound := _binding(result, name, "display"))), None)
    if lit is not None:
        mark = vector(f"{ident}-chosen", {**b.FULL, "display": keyword("none"), "pointer-events": keyword("none")}, [
            path("chosen", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (12, {"fraction": 1}),
                            (0, {"fraction": 1, "dp": -12})], stroke=stroke(solid(rgb(ORANGE)), 2.5))])
        node["children"].insert(1, mark)
        result["bindings"].append({"id": f"touch.{ident}-chosen.display", "node": mark["id"], "property": "display",
                                   "value": lit["value"]})
    return node


def _follow_twins(steps: list, twins: set) -> None:
    """Each step that focuses a control the touch column twins focuses the
    twin instead in the touch variant, where the control is set aside."""
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            continue
        if step.get("op") == "focus" and step.get("control") in twins:
            steps[index] = {"op": "if", "condition": TOUCH, "then": [{"op": "focus", "control": f"touch-{step['control']}"}],
                            "else": [step]}
            continue
        for branch in ("then", "else"):
            if isinstance(step.get(branch), list):
                _follow_twins(step[branch], twins)


def touch_menu(result: dict, title_key: str | None, sources: list, composition: list) -> dict:
    """`result` with its `composition` set aside in the touch variant and
    the touch column of `sources`' twins in its place, under its dialogs and
    notices."""
    declare_sources(result)
    bindings = result.setdefault("bindings", [])
    sizes = Sizes(bindings)
    for ident in composition:
        hide_in_touch(result, ident)
    parts = ([heading(sizes, "touch-title", title_key)] if title_key else []) + [twin(result, sizes, source) for source in sources]
    for steps in result.get("events", {}).values():
        _follow_twins(steps, set(sources))
    inner = sizes.set(group("touch-column", {"position": keyword("relative"), "display": keyword("block"), "max-width": length(100, "%"),
                                             "margin-left": keyword("auto"), "margin-right": keyword("auto")}, parts),
                      {"width": COLUMN_DP})
    scroll = sizes.set(group("touch-scroll", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                              "overflow": keyword("auto"), "box-sizing": keyword("border-box")}, [inner]),
                       {"padding-left": MARGIN_DP, "padding-right": MARGIN_DP, "padding-top": 28, "padding-bottom": 28})
    layer = group("touch", {**b.FULL, "display": keyword("none"),
                            **({"background-color": colour([0, 0, 0, 0.92])} if result["id"] in SOLID else {})}, [scroll])
    bindings.append({"id": "touch.display", "node": "touch", "property": "display", "value": {"op": "select", "args": [TOUCH, "block", "none"]}})
    # Under the first dialog of the container that holds one, else under the notices.
    for holder in walk_nodes(result["root"]):
        children = holder.get("children", [])
        at = next((index for index, child in enumerate(children) if "modal" in child), None)
        if at is not None:
            children.insert(at, layer)
            return result
    children = result["root"]["children"]
    at = next((index for index, child in enumerate(children) if child["id"].startswith("notice-")), len(children))
    children.insert(at, layer)
    return result


# Each menu level's column: its title, the controls it twins in reading
# order, and the composition it sets aside.
PAUSE = (None, ["nav_resume", "nav_savegame", "nav_loadgame", "nav_restart", "nav_objectives", "nav_settings", "nav_quit",
                "link_vrsetup", "link_exit"], ["home", "prompts", "carry", "objectives-bar"])
HUB = ["content", "titles", "prompts"]
MENUS = {
    "title": (None, ["nav_continue", "nav_singleplayer", "nav_loadgame", "nav_multiplayer", "nav_settings", "link_mods",
                     "link_updates", "link_credits", "link_exit"], ["home", "home-stage", "prompts", "carry", "depth-frame"]),
    "pause": PAUSE, "pause_strogg": PAUSE, "pause_awakening": PAUSE,
    "singleplayer": ("#str_42000", ["campaign", "arena", "back"], HUB),
    "campaigns": ("#str_230038", ["quake4", "awakening", "newgame", "chapters", "back"], HUB),
    "multiplayer": ("#str_200002", ["join", "create", "demos", "setup", "back"], HUB),
    "settings": ("#str_200009", ["controls", "game", "display", "audio", "defaults", "back"], HUB),
    "settings_awakening": ("#str_200009", ["controls", "game", "display", "audio", "defaults", "back"], HUB),
    # New Game's difficulty, the restart screen after a death, Restart
    # Level's difficulty and the Arena's pause.
    "skill": (None, ["skill0", "skill1", "skill2", "skill3", "skill4", "turbo", "start", "back"], HUB),
    "restartmenu": (None, ["death_restart", "death_load", "death_menu", "death_exit"], ["death-stage"]),
    **{name: (None, ["skill-0", "skill-1", "skill-2", "skill-3", "skill-4", "restart_go", "restart_back"], ["restart-stage"])
       for name in ("restartlevel", "restartlevel_strogg", "restartlevel_awakening")},
    "arena_pause": (None, ["nav_resume", "nav_restart", "nav_settings", "nav_leave"], ["home", "prompts"]),
}
# Screens that open over another menu (Restart Level over the pause), whose
# column stands on black rather than on that menu.
SOLID = ("openq4.restartlevel", "openq4.restartlevel_strogg", "openq4.restartlevel_awakening")


def with_touch_menus(documents: dict) -> None:
    for name, (title_key, sources, composition) in MENUS.items():
        if name in documents:
            documents[name] = touch_dialogs(touch_menu(documents[name], title_key, sources, composition))


# ------------------------------------------------------------------ dialogs

def _text_twin(result: dict, sizes: Sizes, source: dict, ident: str, face: str, size: float, line: float, tint: list) -> dict:
    """A label saying what `source` says (its key, or what its binding says)
    at `size` device dp, wrapping."""
    node = label(ident, source["properties"].get("text", {}).get("value", "#str_230030"), {
        "position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"), "font-family": font(face),
        "color": colour(tint), "white-space": keyword("normal"), "pointer-events": keyword("none")})
    sizes.set(node, {"font-size": size, "line-height": line, "margin-bottom": 14})
    bound = _binding(result, source["id"], "text")
    if bound is not None:
        result["bindings"].append({"id": f"touch.{ident}.text", "node": ident, "property": "text", "value": bound["value"]})
    return node


def touch_dialog(result: dict, modal: dict) -> bool:
    """The confirmation modal `modal` (confirmation()) as a touch card in the
    touch variant: its title, its body and its buttons (twins of the
    dialog's) in a column the menus' width, centred, the dialog's art set
    aside. A dialog whose contents hold more than buttons (a list, a typed
    name) keeps its art. True when the card was added."""
    ident = modal["id"]
    stage, contents = _find(modal, f"{ident}-stage"), _find(modal, f"{ident}-contents")
    if stage is None or contents is None:
        return False
    controls = [node for node in walk_nodes(contents) if "control" in node]
    if not controls or any(node["control"].get("role") != "button" for node in controls):
        return False
    bindings = result.setdefault("bindings", [])
    sizes = Sizes(bindings)
    hide_in_touch(result, stage["id"])
    parts = []
    title = _find(contents, f"{ident}-title")
    if title is not None:
        parts.append(_text_twin(result, sizes, title, f"touch-{ident}-title", "marine", TITLE_DP, 28, [1, 1, 1, 0.92]))
    body = _find(contents, f"{ident}-body")
    if body is not None:
        parts.append(_text_twin(result, sizes, body, f"touch-{ident}-body", "lowpixel", 17, 24, [1, 1, 1, 0.82]))
    for control in controls:
        twin_node = twin(result, sizes, control["id"])
        bound = _binding(result, f"{control['id']}-label", "text")
        if bound is not None:
            result["bindings"].append({"id": f"touch.{twin_node['id']}-label.text", "node": f"{twin_node['id']}-label",
                                       "property": "text", "value": bound["value"]})
        parts.append(twin_node)
    card = sizes.set(group(f"touch-{ident}-card", {"position": keyword("relative"), "display": keyword("block"), "max-width": length(100, "%"),
                                                   "margin-left": keyword("auto"), "margin-right": keyword("auto"),
                                                   "box-sizing": keyword("border-box"),
                                                   "background-color": colour([0.02, 0.025, 0.015, 0.97])}, parts),
                     {"width": COLUMN_DP, "padding-left": 20, "padding-right": 20, "padding-top": 18, "padding-bottom": 10})
    frame = vector(f"touch-{ident}-frame", dict(b.FULL), [
        path("frame", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             stroke=stroke(solid([0, 0, 0, 1]), 4)),
        path("edge", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             stroke=stroke(solid(rgb(OLIVE, 0.7)), 1.5))])
    card["children"].insert(0, frame)
    # The card stands in the view's middle, scrolling where its text outgrows it.
    layer = sizes.set(group(f"touch-{ident}", {**b.FULL, "display": keyword("none"), "overflow": keyword("auto"),
                                               "box-sizing": keyword("border-box"), "flex-direction": keyword("column"),
                                               "justify-content": keyword("center")}, [card]),
                      {"padding-left": MARGIN_DP, "padding-right": MARGIN_DP, "padding-top": 24, "padding-bottom": 24})
    shown = {"op": "&&", "args": [TOUCH, {"state": f"{ident}.contents"}]}
    bindings.append({"id": f"touch.{ident}.display", "node": layer["id"], "property": "display",
                     "value": {"op": "select", "args": [shown, "flex", "none"]}})
    modal["children"].append(layer)
    # The dialog's first focus is its own button, set aside here: the card's
    # twin takes it as the contents show.
    initial = modal["modal"]["initialFocus"]
    shown_event = result.get("events", {}).get(f"{ident}Shown")
    if shown_event is not None and _find(layer, f"touch-{initial}") is not None:
        shown_event.append({"op": "if", "condition": TOUCH, "then": [{"op": "focus", "control": f"touch-{initial}"}]})
    return True


def touch_dialogs(result: dict) -> dict:
    """Every confirmation modal of `result` with its touch card."""
    declare_sources(result)
    for node in list(walk_nodes(result["root"])):
        if "modal" in node:
            touch_dialog(result, node)
    return result


def touch_message_box() -> dict:
    """The message box (retained_msgbox) in the touch variant, the document
    the adapter loads for guis/menu/msgbox.rui there: the session's title,
    message, typed field and up to three answers in a card the menus' width,
    under the same state names and verbs, so the session drives it alike."""
    from retained_create_server import text_row
    from retained_msgbox import ENTRY_BYTES
    doc = Document("openq4.msgbox")
    for name in ("title", "message", "left", "right", "mid", "str_entry"):
        doc.state[name] = {"type": "string", "initial": ""}
    for name in ("visible_msgbox", "visible_waitbox", "visible_left", "visible_right", "visible_mid", "visible_entry"):
        doc.state[name] = {"type": "boolean", "initial": False}
    for verb in ("msgLeft", "msgRight", "msgMid"):
        doc.session(verb, verb)
    sizes = Sizes(None)
    timelines = doc.timelines.items
    text = lambda ident, face, size, line, tint: label(ident, "#str_230030", {
        "position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"), "font-family": font(face),
        "font-size": length(size), "line-height": length(line), "color": colour(tint), "white-space": keyword("pre-wrap"),
        "margin-bottom": length(14), "pointer-events": keyword("none")})
    title = text("msg-title", "marine", TITLE_DP, 28, [1, 1, 1, 0.92])
    body = text("msg-body", "lowpixel", 17, 24, [1, 1, 1, 0.82])
    doc.bind("msg-title.text", "msg-title", "text", {"state": "title"})
    doc.bind("msg-body.text", "msg-body", "text", {"state": "message"})
    doc.events["msgEntered"] = [{"op": "action", "action": "msgLeft"}]
    field = text_row(doc, "msg-entry", "#str_04321", COLUMN_DP - 40, 29, "str_entry", "msgEntered", ENTRY_BYTES, ROW_H_DP,
                     allow_empty=True, caption=False)
    field["properties"]["width"] = length(100, "%")
    entry = group("msg-entry-rows", {"position": keyword("relative"), "display": keyword("none"), "flex-direction": keyword("column"),
                                     "height": length(ROW_H_DP), "margin-bottom": length(14)}, [field])
    doc.bind("msg-entry-rows.display", "msg-entry-rows", "display", {"op": "select", "args": [{"state": "visible_entry"}, "flex", "none"]})
    answers = []
    for ident, event, shown, said in (("msg_yes", "msgPickLeft", "visible_left", "left"), ("msg_no", "msgPickRight", "visible_right", "right"),
                                      ("msg_mid", "msgPickMid", "visible_mid", "mid")):
        node = button(sizes, timelines, ident, "#str_230030", {"event": event})
        node["properties"]["display"] = keyword("none")
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"state": shown}, "block", "none"]})
        doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": said})
        answers.append(node)
    # Escape answers as the stock box's did: the trailing button of two, or
    # the lone one; a wait has no answer.
    doc.events["msgPickLeft"] = [{"op": "action", "action": "msgLeft"}]
    doc.events["msgPickMid"] = [{"op": "action", "action": "msgMid"}]
    doc.events["msgPickRight"] = [{"op": "if", "condition": {"state": "visible_right"}, "then": [{"op": "action", "action": "msgRight"}],
                                   "else": [{"op": "if", "condition": {"state": "visible_mid"},
                                             "then": [{"op": "action", "action": "msgMid"}]}]}]
    doc.events["msgFocus"] = [{"op": "if", "condition": {"state": "visible_entry"}, "then": [{"op": "focus", "control": "msg-entry"}],
                               "else": [{"op": "if", "condition": {"state": "visible_mid"}, "then": [{"op": "focus", "control": "msg_mid"}],
                                         "else": [{"op": "if", "condition": {"state": "visible_right"},
                                                   "then": [{"op": "focus", "control": "msg_no"}]}]}]}]
    doc.events["onActivate"] = [{"op": "call", "event": "msgFocus"}]
    doc.events["Prompt"] = [{"op": "setState", "values": {"str_entry": ""}}]
    doc.events["onBack"] = [{"op": "call", "event": "msgPickRight"}]
    frame = vector("msg-frame", dict(b.FULL), [
        path("frame", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             stroke=stroke(solid([0, 0, 0, 1]), 4)),
        path("edge", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             stroke=stroke(solid(rgb(OLIVE, 0.7)), 1.5))])
    card = group("msg-card", {"position": keyword("relative"), "display": keyword("block"), "width": length(COLUMN_DP),
                              "max-width": length(100, "%"), "margin-left": keyword("auto"), "margin-right": keyword("auto"),
                              "box-sizing": keyword("border-box"), "padding-left": length(20), "padding-right": length(20),
                              "padding-top": length(18), "padding-bottom": length(10),
                              "background-color": colour([0.02, 0.025, 0.015, 0.97])}, [frame, title, body, entry, *answers])
    layer = group("msg-layer", {**b.FULL, "display": keyword("flex"), "flex-direction": keyword("column"),
                                "justify-content": keyword("center"), "overflow": keyword("auto"), "box-sizing": keyword("border-box"),
                                "padding-left": length(MARGIN_DP), "padding-right": length(MARGIN_DP), "padding-top": length(24),
                                "padding-bottom": length(24)}, [card])
    root = group("screen", {**b.FULL, "background-color": colour([0, 0, 0, 0.6]), "font-family": font("marine"),
                            "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [layer])
    result = doc.build(root)
    result.pop("canvas", None)
    return result


# ------------------------------------------------------------ settings pages

@contextlib.contextmanager
def touch_geometry():
    """The settings modules build their rows ROW_W_DP wide and ROW_H_DP tall
    while it holds: their row helpers read the column width and row pitch
    (in units of U) from these modules as they run."""
    import retained_audio
    import retained_controls
    import retained_game
    import retained_settings_rows
    import retained_system
    import retained_vr
    # A fine slider keeps its slider alone: its typed number has no room.
    # SYSTEM's notes span the row after the label's indent, and its reset
    # marks become a strip under their rows, the row's width and a finger's
    # height.
    values = {"COLUMN_W": ROW_W_DP / U, "ROW_PITCH": ROW_H_DP / U, "FINE_STEPS": float("inf"), "NOTE_W": ROW_W_DP - 29,
              "TOUCH_MARKS": True}
    saved = []
    for module in (retained_settings_rows, retained_game, retained_controls, retained_audio, retained_vr, retained_system):
        for name, value in values.items():
            if hasattr(module, name):
                saved.append((module, name, getattr(module, name)))
                setattr(module, name, value)
        # A row's value keeps VALUE_ROOM_DP of its width: a long label takes
        # its two lines in the room left.
        for name in ("value_column", "rows_value_column", "keys_column"):
            if hasattr(module, name):
                original = getattr(module, name)
                saved.append((module, name, original))
                setattr(module, name, lambda *args, original=original: min(original(*args), ROW_W_DP - VALUE_ROOM_DP))
    try:
        yield
    finally:
        for module, name, value in saved:
            setattr(module, name, value)


def _centre_sliders(row: dict) -> None:
    """A slider's track and number stand at the top of value_row's row; the
    touch rows are taller, so they move to its middle, as its check box and
    chevron are."""
    from retained_mp_menus import VOTE_ROW_H
    for node in walk_nodes(row):
        if node.get("control", {}).get("role") == "slider":
            middle = (ROW_H_DP - VOTE_ROW_H) / 2
            for child in node.get("children", []):
                if child["id"] in (f"{node['id']}-track", f"{node['id']}-value") and child["properties"].get("top") == length(0):
                    child["properties"]["top"] = length(middle)


def _is_note(item: dict) -> bool:
    """A line of text among the rows (a status, a hint): nothing to touch."""
    return not any("control" in node for node in walk_nodes(item))


def _units(prefix: str, ident: str, side: int, column: list) -> list:
    """The column's rows and headings, each note joined to the row before
    it, so that the two stay together however many rows stand abreast."""
    units: list = []
    for item in column:
        if isinstance(item, tuple) or not _is_note(item) or not units or isinstance(units[-1], tuple):
            units.append(item if isinstance(item, tuple) else [item])
            continue
        # A note takes its text's own height (the text size may grow it).
        item["properties"].pop("height", None)
        item["properties"]["width"] = length(ROW_W_DP - 29)
        item["properties"]["margin-top"] = length(4)
        units[-1].append(item)
    laid = []
    for unit in units:
        if isinstance(unit, tuple) or len(unit) == 1:
            laid.append(unit if isinstance(unit, tuple) else unit[0])
            continue
        laid.append(group(f"{unit[0]['id']}-unit", {"position": keyword("relative"), "display": keyword("block"),
                                                    "width": length(ROW_W_DP), "flex-shrink": number(0)}, unit))
    return laid


def touch_footer(ident: str, message: dict, action: dict) -> dict:
    """A page's footer under its lists: the page's status `message` beside
    its main button `action` (a button()), the message over the button where
    the view is too narrow for both, the button then wider."""
    action["properties"].update({"flex-grow": number(1), "max-width": length(360), "margin-bottom": length(0)})
    message["properties"].update({"position": keyword("relative"), "display": keyword("block"), "flex-grow": number(1),
                                  "flex-shrink": number(1), "min-width": length(200), "margin-right": length(12),
                                  "pointer-events": keyword("none")})
    rule = vector(f"{ident}-rule", {**absolute(left=0, top=0, right=0, height=1)}, [
        path("rule", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 1), (0, 1)], fill=solid(rgb(OLIVE, 0.6)))])
    return group(ident, {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("row"),
                         "flex-wrap": keyword("wrap"), "align-items": keyword("center"), "justify-content": keyword("flex-end"),
                         "flex-shrink": number(0), "width": length(100, "%"), "box-sizing": keyword("border-box"),
                         "padding-left": length(MARGIN_DP), "padding-right": length(MARGIN_DP), "padding-top": length(FOOTER_PAD_DP),
                         "padding-bottom": length(FOOTER_PAD_DP), "row-gap": length(8), "opacity": number(1),
                         "background-color": colour([0.02, 0.025, 0.015, 0.92])}, [rule, message, action])


def touch_subpage(doc: Document, prefix: str, title_key: str, tabs: list, *, overlays: list = (), back_event: str | None = None,
                  back_id: str | None = None, footer: list = (), body: str | None = None, first_focus: dict | None = None,
                  activate=None, **_unused) -> dict:
    """A settings page in the touch variant: Back and the page's title in
    a 64 dp header; under it, on a page of several categories, the list of
    them as big buttons, and a category's rows once one is chosen (Back
    returns to the list); a single category shows its rows at once. Rows
    stand ROW_W_DP wide, as many abreast as the view holds, in a list that
    scrolls; `footer` stands under the list (SYSTEM's Apply), and `body`
    names the group around the lists, where a row's choices unfold. The
    page's states, actions and rows are its module's own, so it reads and
    sets everything the desktop page does."""
    sizes = Sizes(None)
    timelines = doc.timelines.items
    tabbed = len(tabs) > 1
    bounds = body
    if back_event is None:
        doc.session("back", f"{prefix}Back")
        leave = [{"op": "action", "action": "back"}]
    elif back_event == "onBack":
        # The page's own Back program (SYSTEM asks before it drops a draft)
        # runs once Back has left the categories.
        doc.events["touchLeave"] = doc.events.pop("onBack")
        leave = [{"op": "call", "event": "touchLeave"}]
    else:
        leave = [{"op": "call", "event": back_event}]
    doc.state.update({"page.tab": {"type": "number", "initial": 0}, "page.leaving": {"type": "number", "initial": -1},
                      "touch.list": {"type": "boolean", "initial": tabbed}})
    listing = {"state": "touch.list"}
    first = {}
    bodies = []
    for index, (ident, key, columns) in enumerate(tabs):
        items = []
        for side, column in enumerate(columns):
            rest = list(column)
            lead = rest.pop(0) if rest and isinstance(rest[0], tuple) else None
            if lead is not None:
                items.append(heading(sizes, f"{prefix}-{ident}-head-{side}", lead[1]))
            for position, item in enumerate(_units(prefix, ident, side, rest)):
                if isinstance(item, tuple):
                    items.append(heading(sizes, f"{prefix}-{ident}-head-{side}-{position}", item[1]))
                    continue
                item["properties"]["margin-left"] = length(4)
                item["properties"]["margin-right"] = length(4)
                item["properties"]["margin-bottom"] = length(8)
                _centre_sliders(item)
                items.append(item)
                first.setdefault(ident, next(node["id"] for node in walk_nodes(item) if "control" in node))
        rows = group(f"{prefix}-{ident}-flow", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("row"),
                                                "flex-wrap": keyword("wrap"), "justify-content": keyword("center"),
                                                "align-items": keyword("flex-start")}, items)
        body = group(f"{prefix}-{ident}-rows", {**absolute(left=0, top=0, right=0, bottom=0), "overflow": keyword("auto"),
                                                 "padding-left": length(MARGIN_DP), "padding-right": length(MARGIN_DP),
                                                 "padding-top": length(8), "padding-bottom": length(24), "display": keyword("none")}, [rows])
        shown = {"op": "&&", "args": [{"op": "==", "args": [{"state": "page.tab"}, index]}, {"op": "!", "args": [listing]}]}
        doc.bind(f"{body['id']}.display", body["id"], "display", {"op": "select", "args": [shown, "block", "none"]})
        bodies.append(body)
    focus = {ident: (first_focus or {}).get(ident, [{"op": "focus", "control": first[ident]}]) for ident, _key, _columns in tabs}
    for index, (ident, key, _columns) in enumerate(tabs):
        doc.events[f"tab_{ident}"] = [{"op": "setState", "values": {"page.tab": index, "touch.list": False}}, *focus[ident]]
        # A module's show_tab (an arrival on a category, as GAME OPTIONS'
        # language reload makes) places the category at once: its rows show.
        doc.timelines.add(f"place-{ident}", 1, [track("touch-header", "opacity", [(0, number(1)), (1, number(1))])],
                          complete="touchPlaced")
    doc.events["touchPlaced"] = [{"op": "setState", "values": {"touch.list": False}}]
    # The categories, and the title over the rows: the page's, or the category's.
    parts = []
    if tabbed:
        entries = [button(sizes, timelines, f"touch-tab-{ident}", key, {"event": f"tab_{ident}"}, width=ROW_W_DP)
                   for ident, key, _columns in tabs]
        flow = group("touch-tabs-flow", {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("row"),
                                         "flex-wrap": keyword("wrap"), "justify-content": keyword("center")}, entries)
        for entry in entries:
            entry["properties"]["margin-left"] = length(4)
            entry["properties"]["margin-right"] = length(4)
        listed = group("touch-tabs", {**absolute(left=0, top=0, right=0, bottom=0), "overflow": keyword("auto"),
                                      "padding-left": length(MARGIN_DP), "padding-right": length(MARGIN_DP), "padding-top": length(12),
                                      "padding-bottom": length(24), "display": keyword("block")}, [flow])
        doc.bind("touch-tabs.display", "touch-tabs", "display", {"op": "select", "args": [listing, "block", "none"]})
        parts.append(listed)
    title_value = title_key
    for index, (_ident, key, _columns) in reversed(list(enumerate(tabs))):
        if tabbed:
            title_value = {"op": "select", "args": [{"op": "&&", "args": [{"op": "!", "args": [listing]},
                                                                          {"op": "==", "args": [{"state": "page.tab"}, index]}]},
                                                    key, title_value]}
    back = button(sizes, timelines, back_id or f"{prefix}_back", "#str_230045", {"event": "touchBack"}, width=120)
    back["properties"].update({"position": keyword("absolute"), "left": length(8), "top": length((HEADER_DP - ROW_H_DP) / 2),
                               "margin-bottom": length(0)})
    title = label("touch-page-title", title_key, {**absolute(left=140, top=0, right=12, height=HEADER_DP), "font-family": font("marine"),
                  "font-size": length(TITLE_DP), "line-height": length(HEADER_DP), "color": colour([1, 1, 1, 0.8]),
                  "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "text-overflow": keyword("ellipsis"),
                  "pointer-events": keyword("none")})
    if tabbed:
        doc.bind("touch-page-title.text", "touch-page-title", "text", title_value)
    rule = vector("touch-header-rule", {**absolute(left=0, top=HEADER_DP - 1, right=0, height=1)}, [
        path("rule", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 1), (0, 1)], fill=solid(rgb(OLIVE, 0.6)))])
    header = group("touch-header", {"position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"),
                                    "height": length(HEADER_DP), "flex-shrink": number(0), "opacity": number(1),
                                    "background-color": colour([0.02, 0.025, 0.015, 0.92])}, [rule, title, back])
    # Back from a category returns to the list; from the list it leaves.
    back_steps = ([{"op": "if", "condition": {"op": "!", "args": [listing]},
                    "then": [{"op": "setState", "values": {"touch.list": True}}, {"op": "focus", "control": f"touch-tab-{tabs[0][0]}"}],
                    "else": leave}] if tabbed else leave)
    doc.events["touchBack"] = back_steps
    doc.events["onBack"] = back_steps
    opening = [{"op": "setState", "values": {"touch.list": tabbed}}]
    opening.append({"op": "focus", "control": f"touch-tab-{tabs[0][0]}"} if tabbed else focus[tabs[0][0]][0])
    doc.events["onActivate"] = activate(opening) if activate else opening
    backdrop = vector("touch-backdrop", dict(b.FULL), [
        path("fill", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})],
             fill=b.linear((0, 0), (0, {"fraction": 1}), [(0, [0.07, 0.08, 0.04, 1]), (1, [0.015, 0.017, 0.01, 1])]))])
    # The header, the lists and the footer in one column, the lists taking
    # the height the other two leave.
    lists = [*bodies, *parts]
    if bounds is not None:
        lists = [group(bounds, dict(b.FULL), lists)]
    content = group(f"{prefix}-lists", {"position": keyword("relative"), "display": keyword("block"), "width": length(100, "%"),
                                        "flex-grow": number(1), "flex-shrink": number(1), "min-height": length(0)}, lists)
    panel = group(f"{prefix}-panel", {**b.FULL, "display": keyword("flex"), "flex-direction": keyword("column")},
                  [header, content, *footer])
    result = doc.build(group("screen", {**b.FULL, "background-color": colour([0, 0, 0, 1])}, [backdrop, panel, *overlays]))
    # No canvas: a dp is the device's.
    result.pop("canvas", None)
    return result
