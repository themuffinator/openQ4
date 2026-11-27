"""The retained CONTROLS settings page (docs/dev/ui-visual-design.md
sections 7, 9 and 13.7; UI-01): the stock page's key bindings
(guis/menu/settings/controls.gui) in the settings sub-page
(retained_settings_rows), the stock page's four tabs as its tabs: movement,
weapons, attack and look, and other, each in two columns. Two tabs follow
them for the devices themselves (issue #220): MOUSE, AIM and MOUSE INPUT
beside ACCELERATION, and CONTROLLER, STICKS and PROMPTS beside TRIGGERS AND
RUMBLE and GYRO AND TOUCH: the stock GAME OPTIONS page's MOUSE INPUT and
CONTROLLER sections, sorted under headings so that a setting stands with
those it works with (Sensitivity beside CPI, the low battery rumble under
Rumble). Their rows set CVars as the GAME OPTIONS page's do
(retained_settings_rows.cvar_row); the key hint stands only on the binding
tabs, the device tabs' headings on their heading line.

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

On Android with the built-in touch controls the MOUSE tab ends with
ON-SCREEN CONTROLS (section 13.6): whether they show in play, their layout's
preset, size and look speed, and TOUCH LAYOUT (controlsTouchLayout), which
opens the Android setup's layout editor over the paused game; the session
shows them through controls.touchLayout, and elsewhere they are not shown.
Without a mouse (the runtime's view.mouse) the rows only a mouse uses step
aside, and where the touch controls are there the tab reads TOUCH and opens
on them (device_fit; docs/dev/ui/interface-variants.md)."""
from __future__ import annotations

from build_retained_screens import (ORANGE, U, Document, absolute, group, keyword, label, length, mouse_present,
                                    number, path, per_language, rgb, solid, text_width, typeface, vector)
from retained_controller_test import ROW_LABEL as TEST_LABEL, SHOW as TEST_SHOW, controller_test_modal
from retained_key_glyphs import bound_keys, em
from retained_mp_menus import (KEYCAP_GAP, KEYCAP_W, PLACEHOLDER, STRIP_EDGE, TAB_FLARE, TAB_PAD, TAB_SHOULDER, TAB_SIZE, any_text,
                               row_face, row_states)
from retained_settings_rows import (COLUMN_W, CONTENT_W, ROW_PITCH, button_row, choice, cvar_row, cvar_settings, heading, requires,
                                    row_chrome, rows_value_column, settings_subpage, slider, toggle, value_column)

PREFIX = "controls"
TOUCH_HEADING = "#str_233409"             # ON-SCREEN CONTROLS


def button(ident: str, label_key: str, action: str) -> tuple:
    """A device tab's row that is one button running a session action."""
    return ("button", ident, label_key, action, None)


def opener(ident: str, label_key: str, event: str) -> tuple:
    """A device tab's row that is one button running one of the page's own
    events: it opens a panel."""
    return ("opener", ident, label_key, event, None)


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
        ("#str_231269", "_quickChat"), ("#str_232541", "_impulse51"),
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
        ("#str_200959", "_voicechat"), ("#str_232913", "toggleVideo"),
    )),
)
BINDINGS = tuple(binding for _section, _heading, rows in SECTIONS for _label, binding in rows)
# The device tabs: each its id, label and two columns of headings and the
# rows that set CVars. The aim settings apply to every device's look but Free
# Look is the mouse's, so they lead MOUSE.
DEVICES = (
    ("mouse", "#str_231777", (
        (heading("#str_232555"),
         toggle("controls_freelook", "#str_200133", "in_freeLook"),
         choice("controls_invertmouse", "#str_200139", "m_pitch", "#str_200059", ("0.022", "-0.022")),
         toggle("controls_togglezoom", "#str_41108", "in_toggleZoom"),
         slider("controls_scopesensitivity", "#str_229972", "pm_zoomedSlow", 1, 100, 1),
         heading("#str_229935"),
         slider("controls_msensitivity", "#str_200141", "sensitivity", 0.01, 30, 0.01, 2),
         slider("controls_mcpi", "#str_229936", "m_cpi", 0, 32000, 50),
         slider("controls_msmooth", "#str_200140", "m_smooth", 1, 8, 1),
         slider("controls_mfilter", "#str_229937", "m_filter", 0, 31, 1)),
        (heading("#str_232556"),
         slider("controls_maccel", "#str_229938", "cl_mouseAccel", -5, 5, 0.01, 2),
         slider("controls_macceloffset", "#str_229939", "cl_mouseAccelOffset", 0, 50, 0.1, 1),
         slider("controls_maccelpower", "#str_229940", "cl_mouseAccelPower", 1, 8, 0.05, 2),
         slider("controls_msenscap", "#str_229941", "cl_mouseSensCap", 0, 100, 0.1, 1),
         # The built-in touch controls (section 13.6), where the session
         # reports them: shown, their layout's preset, size and look speed,
         # and the layout editor.
         heading(TOUCH_HEADING),
         toggle("controls_touchcontrols", "#str_233400", "in_touchControls"),
         choice("controls_touchpreset", "#str_233403", "in_touchLayoutPreset", "#str_233404", (0, 1, 2)),
         slider("controls_touchsize", "#str_233401", "in_touchControlsScale", 0.75, 1.5, 0.05, 2),
         slider("controls_touchlook", "#str_233402", "in_touchLookSensitivity", 0.1, 4, 0.05, 2),
         button("controls_touch_layout", "#str_231762", "controlsTouchLayout")),
    )),
    # The controller has two tabs: the strip has room for one more beside the
    # stock tabs in every language. CONTROLLER holds the device, its buttons,
    # what its motors and lights do and the prompts; AIM holds how the stick
    # and the gyro turn the view. Gyro smoothing stays a console setting.
    # Test Controller opens the CONTROLLER TEST panel (retained_controller_test).
    ("controller", "#str_229924", (
        (heading("#str_232557"),
         toggle("controls_controller", "#str_229925", "in_joystick"),
         choice("controls_device", "#str_232750", "in_joystickDevicePin", "#str_232751", (0, 1)),
         opener("controls_test", TEST_LABEL, TEST_SHOW),
         choice("controls_southpaw", "#str_229930", "in_joystickSouthpaw", "#str_229934", (0, 1)),
         slider("controls_wheelsensitivity", "#str_42840", "in_weaponWheelSensitivity", 0.25, 16, 0.25, 2),
         slider("controls_wheeloffset", "#str_230140", "ui_weaponWheelOffset", -40, 40, 1),
         heading("#str_232749"),
         choice("controls_layout", "#str_232742", "in_joystickLayout", "#str_232743", (0, 1, 2, 3, 4)),
         slider("controls_trigger", "#str_229927", "in_joystickTriggerThreshold", 0, 1, 0.01, 2),
         toggle("controls_togglecrouch", "#str_232745", "in_toggleCrouch"),
         toggle("controls_alwaysrun", "#str_232746", "in_alwaysRun"),
         toggle("controls_context", "#str_232744", "in_joystickContextActions"),
         button("controls_resetcontroller", "#str_232747", "controlsResetController"),
         button("controls_resetkeyboard", "#str_232748", "controlsResetKeyboard")),
        (heading("#str_232734"),
         toggle("controls_rumble", "#str_229928", "in_joystickRumble"),
         slider("controls_rumblescale", "#str_229929", "in_joystickRumbleScale", 0, 2, 0.05, 2),
         toggle("controls_triggerrumble", "#str_232736", "in_joystickTriggerRumble"),
         toggle("controls_adaptivetriggers", "#str_232737", "in_joystickAdaptiveTriggers"),
         toggle("controls_directionalhits", "#str_232738", "in_joystickDirectionalRumble"),
         slider("controls_batterythreshold", "#str_229970", "in_joystickLowBatteryRumbleThreshold", 0, 100, 1),
         slider("controls_batteryscale", "#str_229971", "in_joystickLowBatteryRumbleScale", 0, 2, 0.05, 2),
         heading("#str_232735"),
         toggle("controls_light", "#str_232739", "in_joystickLight"),
         toggle("controls_playerlight", "#str_232740", "in_joystickPlayerLight"),
         slider("controls_batterynotice", "#str_232741", "in_joystickLowBatteryNotice", 0, 100, 1),
         heading("#str_232560"),
         choice("controls_promptstyle", "#str_231526", "in_promptStyle", "#str_231527", (0, 1, 2, 3, 4, 5)),
         choice("controls_swapconfirm", "#str_231528", "in_swapConfirmBack", "#str_231529", (0, 1, 2))),
    )),
    ("aim", "#str_232702", (
        (heading("#str_232705"),
         choice("controls_response", "#str_232707", "in_joystickResponse", "#str_232708", (0, 1, 2, 3)),
         slider("controls_turnspeed", "#str_232709", "in_joystickYawSpeed", 30, 720, 5, 1),
         slider("controls_pitchspeed", "#str_232710", "in_joystickPitchSpeed", 30, 720, 5, 1),
         slider("controls_lookcurve", "#str_229932", "in_joystickLookCurve", 1, 3, 0.05, 2),
         toggle("controls_invertlook", "#str_229933", "in_joystickInvertLook"),
         slider("controls_turnboost", "#str_232711", "in_joystickTurnBoost", 0, 400, 10),
         choice("controls_aimassist", "#str_232712", "in_aimAssist", "#str_232713", (0, 1, 2, 3)),
         heading("#str_232706"),
         slider("controls_lookdeadzone", "#str_232714", "in_joystickLookDeadZone", 0, 0.95, 0.01, 2),
         slider("controls_lookouterzone", "#str_232715", "in_joystickLookOuterDeadZone", 0, 0.5, 0.01, 2),
         slider("controls_movedeadzone", "#str_232716", "in_joystickMoveDeadZone", 0, 0.95, 0.01, 2),
         slider("controls_moveouterzone", "#str_232717", "in_joystickMoveOuterDeadZone", 0, 0.5, 0.01, 2),
         choice("controls_moveshape", "#str_232718", "in_joystickMoveDeadZoneAxial", "#str_232719", (0, 1)),
         slider("controls_movecurve", "#str_232720", "in_joystickMoveCurve", 1, 3, 0.05, 2)),
        (heading("#str_232752"),
         toggle("controls_gyro", "#str_229964", "in_gyro"),
         slider("controls_gyroturn", "#str_232721", "in_gyroYawRatio", 0.1, 8, 0.05, 2),
         slider("controls_gyrotilt", "#str_232722", "in_gyroPitchRatio", 0.1, 8, 0.05, 2),
         choice("controls_gyrospace", "#str_232723", "in_gyroSpace", "#str_232724", (0, 1, 2)),
         choice("controls_gyroactive", "#str_232725", "in_gyroActivation", "#str_232726", (0, 1, 2, 3)),
         toggle("controls_gyroinvertturn", "#str_232727", "in_gyroInvertYaw"),
         toggle("controls_gyroinverttilt", "#str_232728", "in_gyroInvertPitch"),
         slider("controls_gyrotightening", "#str_232729", "in_gyroDeadZone", 0, 0.2, 0.005, 3),
         toggle("controls_gyropointer", "#str_232731", "in_gyroMenuPointer"),
         button("controls_gyrocalibrate", "#str_232732", "controlsCalibrateGyro"),
         heading("#str_232733"),
         choice("controls_touchpad", "#str_229966", "in_touchpadMode", "#str_229967", (0, 1, 2, 3)),
         slider("controls_touchpadsensitivity", "#str_229968", "in_touchpadSensitivity", 0, 8, 0.05, 2),
         toggle("controls_touchscreen", "#str_229969", "in_touchscreen")),
    )),
)
# The session actions the device tabs' buttons run (HandleRetainedControlsRequest).
DEVICE_ACTIONS = ("controlsResetController", "controlsResetKeyboard", "controlsCalibrateGyro")
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


def device_rows():
    """Every device tab's rows that set CVars, in reading order."""
    return (item for _ident, _key, columns in DEVICES for column in columns for item in column if item[0] != "heading")


def dependencies() -> dict:
    """The device rows another setting turns off (retained_settings_rows.
    requires), each the condition under which it does anything:
    - Accel Offset, Accel Curve and Sensitivity Cap while Mouse Accel is 0
      (UsercmdGen reads them only with acceleration);
    - every controller row with Controller Input off, which closes the
      controllers, but those that also serve the keyboard or VR (Toggle
      Crouch, Always Run, the weapon wheel's sensitivity and offset), the
      reset buttons and the touchscreen;
    - Rumble Strength, trigger and directional rumble, adaptive triggers and
      the low battery rumble with Rumble off, and the low battery rumble's
      strength while its threshold is 0;
    - the gyro's tuning with both Gyro and the gyro menu pointer off (the
      pointer keeps the sensor and its tuning), Gyro Activation with Gyro off;
    - Touchpad Sensitivity with the touchpad off or buttons only (modes 0
      and 3)."""
    cvar = lambda name: {"state": f"cvar.{name}"}
    both = lambda *terms: terms[0] if len(terms) == 1 else {"op": "&&", "args": [terms[0], both(*terms[1:])]}
    pad = cvar("in_joystick")
    accel = {"op": "!=", "args": [cvar("cl_mouseAccel"), 0]}
    rumble = both(pad, cvar("in_joystickRumble"))
    sensor = both(pad, {"op": "||", "args": [cvar("in_gyro"), cvar("in_gyroMenuPointer")]})
    mode = cvar("in_touchpadMode")
    padless = {"controls_controller", "controls_wheelsensitivity", "controls_wheeloffset", "controls_togglecrouch", "controls_alwaysrun",
               "controls_resetcontroller", "controls_resetkeyboard", "controls_touchscreen"}
    needs = {ident: pad for ident in (item[1] for tab, _key, columns in DEVICES if tab in ("controller", "aim")
                                      for column in columns for item in column if item[0] != "heading") if ident not in padless}
    needs.update({ident: accel for ident in ("controls_macceloffset", "controls_maccelpower", "controls_msenscap")})
    needs.update({ident: rumble for ident in ("controls_rumblescale", "controls_triggerrumble", "controls_adaptivetriggers",
                                              "controls_directionalhits", "controls_batterythreshold")})
    needs["controls_batteryscale"] = both(rumble, {"op": ">", "args": [cvar("in_joystickLowBatteryRumbleThreshold"), 0]})
    needs.update({ident: sensor for ident in ("controls_gyroturn", "controls_gyrotilt", "controls_gyrospace", "controls_gyroinvertturn",
                                              "controls_gyroinverttilt", "controls_gyrotightening", "controls_gyrocalibrate")})
    needs["controls_gyroactive"] = both(pad, cvar("in_gyro"))
    needs["controls_touchpadsensitivity"] = both(pad, {"op": ">", "args": [mode, 0]}, {"op": "<", "args": [mode, 3]})
    return needs


def setting_rows() -> list:
    """Every device row: (cvar, kind, values or range)."""
    return cvar_settings(device_rows())


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


# The MOUSE tab fits the device (docs/dev/ui/interface-variants.md): the rows
# that only a mouse uses, with their MOUSE INPUT and ACCELERATION headings,
# step aside where no mouse is attached or has been used; the ON-SCREEN
# CONTROLS section shows only where the session reports the built-in touch
# controls (controls.touchLayout, Android). With those controls and no mouse
# the tab reads TOUCH and opens on its first touch row. The headings' ids are
# settings_subpage's: a column's first heading is <prefix>-<tab>-head-<side>,
# a later one <prefix>-<tab>-head-<side>-<position among the rest>.
MOUSE_ROWS = ("controls_invertmouse", "controls_msensitivity", "controls_mcpi", "controls_msmooth", "controls_mfilter",
              "controls_maccel", "controls_macceloffset", "controls_maccelpower", "controls_msenscap")
TOUCH_ROWS = ("controls_touchcontrols", "controls_touchpreset", "controls_touchsize", "controls_touchlook", "controls_touch_layout")
TOUCH_TAB_LABEL = "#str_232733"           # TOUCH


def _heading_ids(tab: str) -> dict:
    """Each heading key of a device tab and the id settings_subpage gives it."""
    ids = {}
    for ident, _key, columns in DEVICES:
        if ident != tab:
            continue
        for side, column in enumerate(columns):
            rest = list(column)
            lead = rest.pop(0) if rest and rest[0][0] == "heading" else None
            if lead:
                ids[lead[1]] = f"{PREFIX}-{tab}-head-{side}"
            for position, item in enumerate(rest):
                if item[0] == "heading":
                    ids[item[1]] = f"{PREFIX}-{tab}-head-{side}-{position}"
    return ids


def device_fit(doc: Document, first_focus: dict, flow: dict) -> None:
    """`flow` maps each device row's control id to its node in the column's
    flow: a fine slider's row (<id>_row) holds the slider and its number."""
    touch = {"state": "controls.touchLayout"}
    mouse = mouse_present(doc)
    shown = lambda condition: {"op": "select", "args": [condition, "block", "none"]}
    headings = _heading_ids("mouse")
    for ident in (*(flow[row] for row in MOUSE_ROWS), headings["#str_229935"], headings["#str_232556"]):
        doc.bind(f"device.{ident}.display", ident, "display", shown(mouse))
    for ident in (*(flow[row] for row in TOUCH_ROWS), headings[TOUCH_HEADING]):
        doc.bind(f"device.{ident}.display", ident, "display", shown(touch))
    swap = {"op": "&&", "args": [touch, {"op": "!", "args": [mouse]}]}
    # TOUCH takes MOUSE's place in the strip where it fits as
    # check_settings_text measures the strip; a language (Turkish) whose
    # strip has no room for it keeps MOUSE's label over the same rows.
    keys = [key for _section, key, _rows in SECTIONS] + [TOUCH_TAB_LABEL if key == "#str_231777" else key for _i, key, _c in DEVICES]
    room = U * CONTENT_W - 2 * (STRIP_EDGE + KEYCAP_W + KEYCAP_GAP)
    tight = {language: int(sum(text_width("marine", any_text(key)[language], TAB_SIZE) + TAB_FLARE + 2 * TAB_PAD + TAB_SHOULDER
                               for key in keys) > room) for language in any_text(TOUCH_TAB_LABEL)}
    relabel = {"op": "&&", "args": [swap, {"op": "<", "args": [per_language(doc, tight), 0.5]}]}
    for node in ("tab-mouse-label", "tab-mouse-lit"):
        doc.bind(f"device.{node}.text", node, "text", {"op": "select", "args": [relabel, TOUCH_TAB_LABEL, "#str_231777"]})
    first_focus["mouse"] = [{"op": "if", "condition": swap, "then": [{"op": "focus", "control": TOUCH_ROWS[0]}],
                             "else": [{"op": "focus", "control": "controls_freelook"}]}]


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
        half = (len(built) + 1) // 2
        tabs.append((section, heading_key, [built[:half], built[half:]]))
    # The device tabs share one value column, as GAME OPTIONS' rows do.
    value_x = rows_value_column(device_rows())
    for action in DEVICE_ACTIONS:
        doc.session(action, action)

    def device_row(item):
        if item[0] == "heading":
            return item
        if item[0] == "button":
            _kind, ident, label_key, action, _extra = item
            return button_row(doc, ident, label_key, value_x, None, {"action": action})
        if item[0] == "opener":
            _kind, ident, label_key, event, _extra = item
            return button_row(doc, ident, label_key, value_x, None, {"event": event})
        return cvar_row(doc, item, value_x)

    needs = dependencies()
    first_focus, flow = {}, {}
    for ident, key, columns in DEVICES:
        laid = [[device_row(item) for item in column] for column in columns]
        flow.update({node["id"].removesuffix("_row"): node["id"] for column in laid for node in column if isinstance(node, dict)})
        for node in (node for column in laid for node in column if isinstance(node, dict)):
            if node["id"] in needs:
                requires(doc, node, needs[node["id"]])
        tabs.append((ident, key, laid))
        # A tab whose first row another setting can turn off opens on its
        # first row that answers.
        rows = [item for column in columns for item in column if item[0] != "heading"]
        if rows[0][1] in needs:
            spare = next(item[1] for item in rows if item[1] not in needs)
            first_focus[ident] = [{"op": "if", "condition": needs[rows[0][1]], "then": [{"op": "focus", "control": rows[0][1]}],
                                   "else": [{"op": "focus", "control": spare}]}]
    device_fit(doc, first_focus, flow)
    # The west face button clears the focused row, as Delete does.
    doc.events["onSecondary"] = [{"op": "action", "action": "controlsClear"}]
    # The prompt bar (section 13.4): Accept reads CHANGE on a row; while the
    # page waits for a key, Accept is the key itself and leaves the bar.
    prompts = {"verbs": (VERB_CHANGE,), "accept_shown": {"op": "<", "args": [{"state": CAPTURE}, 0]}}
    note = {section: "#str_231545" for section, _heading, _rows in SECTIONS}
    return settings_subpage(doc, PREFIX, "#str_200083", tabs, note=note, prompts=prompts, overlays=[controller_test_modal(doc)],
                            first_focus=first_focus)
