"""The retained CONTROLS settings page (docs/dev/ui-visual-design.md
sections 7, 9 and 13.7; UI-01): the stock page's key bindings
(guis/menu/settings/controls.gui) in the settings sub-page
(retained_settings_rows), the stock page's four tabs as its tabs: movement,
weapons, attack and look, and other, each in two columns.

Each row names an action and the keys bound to it, which the session
publishes (controls.<row>.keys) as the language's key names. Accept asks the
session to wait for a key (controlsBind); the session takes the next key as
the stock binding field does: Escape, Start or Back cancel, Backspace or
Delete clear, any other key binds (idSessionLocal::RetainedControlsKey).
Delete, Backspace or the west face button on a row clears it
(controlsClear). The session reads the row from the focused control. How to
change a binding stands on the heading line. BACK, or the back input,
returns to the settings screen that opened the page.

On Android with the built-in touch controls the Other tab ends with TOUCH
LAYOUT (controlsTouchLayout), which opens the Android setup's layout editor
(docs/dev/ui-visual-design.md section 13.6) over the paused game; the session
shows it through controls.touchLayout, and elsewhere the row is not shown."""
from __future__ import annotations

from build_retained_screens import Document
from retained_settings_rows import button_row, settings_subpage, value_column

PREFIX = "controls"
# Each section: its id, heading (the stock tab) and rows (label, binding), in
# the stock page's order; the session's RETAINED_CONTROLS_BINDINGS follows it.
SECTIONS = (
    ("move", "#str_200090", (
        ("#str_200094", "_forward"), ("#str_200095", "_back"), ("#str_200096", "_moveleft"), ("#str_200097", "_moveright"),
        ("#str_200098", "_moveUp"), ("#str_200099", "_moveDown"), ("#str_200100", "_left"), ("#str_200101", "_right"),
        ("#str_200102", "_strafe"), ("#str_200103", "_speed"),
    )),
    ("weapons", "#str_200091", (
        ("#str_200966", "_impulse0"), ("#str_200106", "_impulse1"), ("#str_200105", "_impulse2"), ("#str_200107", "_impulse3"),
        ("#str_200108", "_impulse4"), ("#str_200109", "_impulse5"), ("#str_200110", "_impulse6"), ("#str_200111", "_impulse7"),
        ("#str_200112", "_impulse8"), ("#str_200113", "_impulse9"), ("#str_222001", "_impulse10"), ("#str_222014", "buymenu"),
        ("#str_231269", "_quickChat"),
    )),
    ("attack", "#str_200092", (
        ("#str_200114", "_attack"), ("#str_200115", "_impulse15"), ("#str_200116", "_impulse14"), ("#str_200117", "_impulse13"),
        ("#str_200118", "_lookUp"), ("#str_200119", "_lookDown"), ("#str_200120", "_mlook"), ("#str_200121", "centerview"),
        ("#str_200122", "_zoom"), ("#str_200131", "_impulse19"), ("#str_200315", "_impulse50"), ("#str_200125", "screenshot"),
        ("#str_229920", "_weaponWheel"),
    )),
    ("other", "#str_200093", (
        ("#str_200123", "savegame quick"), ("#str_200124", "loadgame quick"), ("#str_200126", "clientMessageMode"),
        ("#str_200127", "clientMessageMode 1"), ("#str_200129", "_impulse28"), ("#str_200130", "_impulse29"),
        ("#str_200132", "_ingamestats"), ("#str_201009", "_impulse17"), ("#str_200311", "emote salute"),
        ("#str_200312", "emote cheer"), ("#str_200313", "emote taunt"), ("#str_200314", "emote grab_a"),
        ("#str_200959", "_voicechat"),
    )),
)
BINDINGS = tuple(binding for _section, _heading, rows in SECTIONS for _label, binding in rows)
TOUCH_LAYOUT_ROW, TOUCH_LAYOUT_LABEL = "controls_touch_layout", "#str_231762"


def controls_document() -> dict:
    doc = Document("openq4.controls")
    doc.state["controls.capture"] = {"type": "number", "initial": -1}
    for row, binding in enumerate(BINDINGS):
        doc.state[f"controls.{row}.keys"] = {"type": "string", "initial": "", "keyBinding": binding}
    doc.session("controlsBind", "controlsBind")
    doc.session("controlsClear", "controlsClear")
    doc.state["controls.touchLayout"] = {"type": "boolean", "initial": False}
    doc.session("controlsTouchLayout", "controlsTouchLayout")
    tabs, row = [], 0
    for section, heading_key, rows in SECTIONS:
        # A tab's keys start where its own labels allow: the keys bound to an
        # action are what the page is for.
        value_x = value_column([label_key for label_key, _binding in rows])
        built = []
        for label_key, _binding in rows:
            built.append(button_row(doc, f"controls_bind_{row}", label_key, value_x, {"state": f"controls.{row}.keys"},
                                    {"action": "controlsBind"}))
            row += 1
        if section == "other":
            # Shown only where the session reports the built-in touch controls;
            # the rows above keep their columns either way.
            built.append(button_row(doc, TOUCH_LAYOUT_ROW, TOUCH_LAYOUT_LABEL, value_x, None, {"action": "controlsTouchLayout"}))
            doc.bind(f"{TOUCH_LAYOUT_ROW}.display", TOUCH_LAYOUT_ROW, "display",
                     {"op": "select", "args": [{"state": "controls.touchLayout"}, "block", "none"]})
        half = (len(built) + 1) // 2
        tabs.append((section, heading_key, [built[:half], built[half:]]))
    # The west face button clears the focused row, as Delete does.
    doc.events["onSecondary"] = [{"op": "action", "action": "controlsClear"}]
    return settings_subpage(doc, PREFIX, "#str_200083", tabs, note="#str_231545")
