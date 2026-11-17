"""The retained CONTROLS settings page (docs/dev/ui-visual-design.md
sections 7, 9 and 13.7; UI-01): the stock page's key bindings
(guis/menu/settings/controls.gui) in the settings sub-page
(retained_settings_rows), the stock page's four tabs as its tabs: movement,
weapons, attack and look, and other, each in two columns. Two tabs follow
them for the devices themselves (issue #220): MOUSE, which leads with the
aim settings (Free Look, Toggle Zoom and Scope Sensitivity), and CONTROLLER,
the stock GAME OPTIONS page's MOUSE INPUT and CONTROLLER sections. Their rows
set CVars as the GAME OPTIONS page's do (retained_settings_rows.cvar_row);
the key hint stands only on the binding tabs.

Each row names an action and draws the keys bound to it as glyphs
(retained_key_glyphs): up to two keyboard or mouse keys, then up to two
controller buttons in the glyphs of the controller in use, each in a slot the
adapter fills (controls.<row>.keys.key0 and on, .pad0 and on); a count stands
for any further keys, and a dim dash for an action with none. An action keeps
two keys of each family (idKeyInput::BindForMenu), so a new key never costs
the controller its buttons.

Accept asks the session to wait for a key (controlsBind); the row then asks
for one in the focus colour, and the session takes the next key as the stock
binding field does: Escape, Start or Back cancel, Backspace or Delete clear,
any other key binds (idSessionLocal::RetainedControlsKey). Delete, Backspace
or the west face button on a row clears it (controlsClear). The session reads
the row from the focused control. How to change a binding stands on the
heading line, and the prompt bar follows (section 13.4): Accept reads CHANGE
on a row, and while the page waits for a key Accept leaves the bar, the key
being the answer. BACK, or the back input, returns to the settings screen that
opened the page.

On Android with the built-in touch controls the Other tab ends with TOUCH
LAYOUT (controlsTouchLayout), which opens the Android setup's layout editor
(docs/dev/ui-visual-design.md section 13.6) over the paused game; the session
shows it through controls.touchLayout, and elsewhere the row is not shown."""
from __future__ import annotations

from build_retained_screens import (ORANGE, U, Document, absolute, group, keyword, label, length,
                                    number, path, rgb, solid, text_width, typeface, vector)
from retained_key_glyphs import bound_keys, em
from retained_mp_menus import PLACEHOLDER, any_text, row_face, row_states
from retained_settings_rows import (COLUMN_W, ROW_PITCH, button_row, choice, cvar_row, cvar_settings, row_chrome, rows_value_column,
                                    settings_subpage, slider, toggle, value_column)

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
        ("#str_229920", "_weaponWheel"), ("#str_42222", "_hook"),
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
# The device tabs: each its id, label, rows that set CVars and the rows that
# fill the first of its two columns. The aim settings apply to every device's
# look but Free Look is the mouse's, so the three lead MOUSE.
DEVICES = (
    ("mouse", "#str_231777", (
        toggle("controls_freelook", "#str_200133", "in_freeLook"),
        toggle("controls_togglezoom", "#str_41108", "in_toggleZoom"),
        slider("controls_scopesensitivity", "#str_229972", "pm_zoomedSlow", 1, 100, 1),
        choice("controls_invertmouse", "#str_200139", "m_pitch", "#str_200059", ("0.022", "-0.022")),
        slider("controls_msmooth", "#str_200140", "m_smooth", 1, 8, 1),
        slider("controls_msensitivity", "#str_200141", "sensitivity", 0.01, 30, 0.01, 2),
        slider("controls_mcpi", "#str_229936", "m_cpi", 0, 32000, 50),
        slider("controls_mfilter", "#str_229937", "m_filter", 0, 31, 1),
        slider("controls_maccel", "#str_229938", "cl_mouseAccel", -5, 5, 0.01, 2),
        slider("controls_macceloffset", "#str_229939", "cl_mouseAccelOffset", 0, 50, 0.1, 1),
        slider("controls_maccelpower", "#str_229940", "cl_mouseAccelPower", 1, 8, 0.05, 2),
        slider("controls_msenscap", "#str_229941", "cl_mouseSensCap", 0, 100, 0.1, 1),
    ), 6),
    ("controller", "#str_229924", (
        toggle("controls_controller", "#str_229925", "in_joystick"),
        choice("controls_southpaw", "#str_229930", "in_joystickSouthpaw", "#str_229934", (0, 1)),
        slider("controls_deadzone", "#str_229926", "in_joystickDeadZone", 0, 0.95, 0.01, 2),
        slider("controls_looksensitivity", "#str_229931", "in_joystickLookSensitivity", 0.1, 4, 0.05, 2),
        slider("controls_wheelsensitivity", "#str_42840", "in_weaponWheelSensitivity", 0.25, 16, 0.25, 2),
        slider("controls_wheeloffset", "#str_230140", "ui_weaponWheelOffset", -40, 40, 1),
        slider("controls_lookcurve", "#str_229932", "in_joystickLookCurve", 1, 3, 0.05, 2),
        toggle("controls_invertlook", "#str_229933", "in_joystickInvertLook"),
        slider("controls_trigger", "#str_229927", "in_joystickTriggerThreshold", 0, 1, 0.01, 2),
        toggle("controls_rumble", "#str_229928", "in_joystickRumble"),
        slider("controls_rumblescale", "#str_229929", "in_joystickRumbleScale", 0, 2, 0.05, 2),
        toggle("controls_gyro", "#str_229964", "in_gyro"),
        slider("controls_gyrosensitivity", "#str_229965", "in_gyroSensitivity", 0, 8, 0.05, 2),
        choice("controls_touchpad", "#str_229966", "in_touchpadMode", "#str_229967", (0, 1, 2, 3)),
        slider("controls_touchpadsensitivity", "#str_229968", "in_touchpadSensitivity", 0, 8, 0.05, 2),
        toggle("controls_touchscreen", "#str_229969", "in_touchscreen"),
        slider("controls_batterythreshold", "#str_229970", "in_joystickLowBatteryRumbleThreshold", 0, 100, 1),
        slider("controls_batteryscale", "#str_229971", "in_joystickLowBatteryRumbleScale", 0, 2, 0.05, 2),
        choice("controls_promptstyle", "#str_231526", "in_promptStyle", "#str_231527", (0, 1, 2, 3, 4, 5)),
        choice("controls_swapconfirm", "#str_231528", "in_swapConfirmBack", "#str_231529", (0, 1, 2)),
    ), 9),
)
TOUCH_LAYOUT_ROW, TOUCH_LAYOUT_LABEL = "controls_touch_layout", "#str_231762"
CAPTURE = "controls.capture"              # the row waiting for a key, or -1
WAITING_TEXT = "#str_107000"              # "Press any key...", the stock field's
VERB_CHANGE = "#str_231766"               # Accept's verb on a row
CHIP = 20.0                               # dp: a key glyph's height in the 36 dp row
KEYS_ROOM = 236.0                         # dp: two long keycaps and two controller buttons (LEFT SHIFT, RIGHT ALT, two paddles)
VALUE = "#FFBE23"


def keys_column(labels) -> float:
    """Where a tab's keys start: past every label on one line where the keys
    keep KEYS_ROOM, and never nearer than two lines of the longest allow."""
    one_line = max(text_width("lowpixel", text, 14) for key in labels for text in any_text(key).values()) + 29
    return max(value_column(labels), min(round(one_line + 0.5), round(U * COLUMN_W - 12 - KEYS_ROOM)))


def setting_rows() -> list:
    """Every device row: (cvar, kind, values or range)."""
    return cvar_settings(row for _ident, _key, rows, _split in DEVICES for row in rows)


def bind_row(doc: Document, row: int, label_key: str, value_x: float) -> dict:
    """An action's row, one button: its label and, from the value column,
    its keys as glyphs, a count of any the row has no slot for, and a dim
    dash when it has none. While the session waits for this row's key the
    glyphs give way to the request in the focus colour."""
    ident, state = f"controls_bind_{row}", f"controls.{row}.keys"
    row_h = U * ROW_PITCH
    room = U * COLUMN_W - value_x - 12
    waiting = {"op": "==", "args": [{"state": CAPTURE}, row]}
    caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True, row_h), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])
    chips = bound_keys(doc, ident, state)
    doc.state[f"{state}.more"] = {"type": "string", "initial": ""}
    more = label(f"{ident}-more", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"),
                 "font-family": {"type": "font", "value": "lowpixel"}, "font-size": em(13 / CHIP), "line-height": em(CHIP / 13),
                 "color": {"type": "color", "value": rgb(VALUE)}, "white-space": keyword("nowrap"), "flex-shrink": number(0)})
    doc.bind(f"{ident}-more.text", f"{ident}-more", "text", {"state": f"{state}.more"})
    none = vector(f"{ident}-none", {"position": keyword("relative"), "display": keyword("none"), "width": em(0.7), "height": em(1),
                                    "flex-shrink": number(0)},
                  [path("dash", [(0, {"fraction": 0.5}), ({"fraction": 1}, {"fraction": 0.5})], closed=False,
                        stroke={"paint": solid([1, 1, 1, 0.3]), "widthDp": 1.5, "minimumPixels": 1, "join": "miter", "cap": "butt"})])
    unbound = {"op": "&&", "args": [{"op": "==", "args": [{"state": f"{state}.keyCount"}, 0]},
                                    {"op": "==", "args": [{"state": f"{state}.padCount"}, 0]}]}
    doc.bind(f"{ident}-none.display", f"{ident}-none", "display", {"op": "select", "args": [unbound, "block", "none"]})
    # The strip's font size is a glyph's height: every glyph is drawn in em of it.
    strip = group(f"{ident}-keys", {**absolute(left=value_x, top=(row_h - CHIP) / 2, width=room, height=CHIP), "display": keyword("flex"),
                                    "flex-direction": keyword("row"), "align-items": keyword("center"), "overflow": keyword("hidden"),
                                    "font-size": length(CHIP), "pointer-events": keyword("none")}, [*chips, more, none])
    doc.bind(f"{ident}-keys.display", f"{ident}-keys", "display", {"op": "select", "args": [waiting, "none", "flex"]})
    asking = label(f"{ident}-waiting", WAITING_TEXT, {**absolute(left=value_x, top=0, width=room, height=row_h),
                   **typeface("lowpixel", 14, row_h, rgb(ORANGE)), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                   "display": keyword("none"), "pointer-events": keyword("none")})
    doc.bind(f"{ident}-waiting.display", f"{ident}-waiting", "display", {"op": "select", "args": [waiting, "block", "none"]})
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(U * COLUMN_W),
                         "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)},
                 [*row_chrome(ident, row_h), head, strip, asking],
                 control={"role": "button", "label": label_key, "action": "controlsBind", "prompt": VERB_CHANGE,
                          "states": row_states(doc, ident)})


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
        value_x = keys_column([label_key for label_key, _binding in rows])
        built = []
        for label_key, _binding in rows:
            built.append(bind_row(doc, row, label_key, value_x))
            row += 1
        if section == "other":
            # Shown only where the session reports the built-in touch controls;
            # the rows above keep their columns either way.
            built.append(button_row(doc, TOUCH_LAYOUT_ROW, TOUCH_LAYOUT_LABEL, value_x, None, {"action": "controlsTouchLayout"}))
            doc.bind(f"{TOUCH_LAYOUT_ROW}.display", TOUCH_LAYOUT_ROW, "display",
                     {"op": "select", "args": [{"state": "controls.touchLayout"}, "block", "none"]})
        half = (len(built) + 1) // 2
        tabs.append((section, heading_key, [built[:half], built[half:]]))
    # The device tabs share one value column, as GAME OPTIONS' rows do.
    value_x = rows_value_column(row for _ident, _key, rows, _split in DEVICES for row in rows)
    for ident, key, rows, split in DEVICES:
        built = [cvar_row(doc, row, value_x) for row in rows]
        tabs.append((ident, key, [built[:split], built[split:]]))
    # The west face button clears the focused row, as Delete does.
    doc.events["onSecondary"] = [{"op": "action", "action": "controlsClear"}]
    # The prompt bar (section 13.4): Accept reads CHANGE on a row; while the
    # page waits for a key, Accept is the key itself and leaves the bar.
    prompts = {"verbs": (VERB_CHANGE,), "accept_shown": {"op": "<", "args": [{"state": CAPTURE}, 0]}}
    note = {section: "#str_231545" for section, _heading, _rows in SECTIONS}
    return settings_subpage(doc, PREFIX, "#str_200083", tabs, note=note, prompts=prompts)
