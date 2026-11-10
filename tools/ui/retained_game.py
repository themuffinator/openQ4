"""The retained GAME OPTIONS settings page (docs/dev/ui-visual-design.md
sections 7, 9 and 13.7; UI-01): the stock page's rows
(guis/menu/settings/game.gui) in the settings sub-page
(retained_settings_rows), its eight sections on five tabs of at most two
columns: GENERAL (general | gameplay and view weapon), MOUSE INPUT,
CONTROLLER, HUD (HUD | crosshair) and VIRTUAL REALITY.

Each row applies at once, as the stock page's controls do. A row that names
a CVar reads it back through the page's CVar state and sets it with the
adapter's allowlisted settings.player.set (UserInterfaceRetained.cpp
FindPlayerSetting, which takes each stock control's range or values). The
rows the stock page computes go through the session
(idSessionLocal::HandleRetainedGameRequest): the gun position, the forced
pro skins, the custom crosshair, the corpse time and the language, which
reloads the language and the menus. A slider with more than a hundred steps
(the mouse's sensitivity, CPI, acceleration and cap, the gyro and touchpad
sensitivities, the gun's field of view and offsets) takes the value typed as
well, in a number after it to the slider's places (retained_settings_rows.
entry_row); the adapter acknowledges it once the CVar reads it back. BACK, or
the back input, returns to the settings screen that opened the page."""
from __future__ import annotations

from build_retained_screens import U, Document, absolute, image, keyword, length, number
from retained_mp_menus import value_row
from retained_settings_rows import COLUMN_W, ROW_PITCH, entry_row, heading, setting_row, settings_subpage, show_tab, value_column

PREFIX = "game"
CROSSHAIRS = 20                            # the custom crosshairs (player_marine_mp_ui's mtr_crosshair keys)
# The crosshair colours the stock page offers (#str_229942), as g_crosshairColor.
CROSSHAIR_COLORS = ("1 1 1 1", "1 0 0 1", "1 0.5 0 1", "1 1 0 1", "0 1 0 1", "0 1 1 1", "0 0 1 1", "1 0 0.5 1")
LANGUAGES = 12                             # #str_229908, in sys_lang's order (RETAINED_GAME_LANGUAGES)
FINE_STEPS = 100                           # a slider with more steps than this takes a typed number too


def toggle(ident: str, label: str, cvar: str) -> tuple:
    return ("toggle", ident, label, cvar, None)


def slider(ident: str, label: str, cvar: str, minimum: float, maximum: float, step: float, decimals: int = 0) -> tuple:
    return ("slider", ident, label, cvar, (minimum, maximum, step, decimals))


def choice(ident: str, label: str, cvar: str, key: str, values: tuple) -> tuple:
    """A list whose values are numbers, or text where a value has a fraction:
    a float CVar reads back inexactly (0.022 as 0.0219999...), its text
    exactly, as the stock list compares it."""
    return ("choice", ident, label, cvar, (key, values))


def verb_choice(ident: str, label: str, verb: str, key: str, count: int, enabled: int | None = None) -> tuple:
    """A list the session applies; options from `enabled` on are shown, not chosen."""
    return ("verb_choice", ident, label, verb, (key, count, enabled))


# Each section: its id, heading and rows, in the stock page's order.
SECTIONS = (
    ("general", "#str_231542", (
        toggle("game_freelook", "#str_200133", "in_freeLook"),
        toggle("game_autoreload", "#str_200134", "ui_autoReload"),
        toggle("game_autoswitch", "#str_200135", "ui_autoSwitch"),
        toggle("game_togglezoom", "#str_41108", "in_toggleZoom"),
        slider("game_scopesensitivity", "#str_229972", "pm_zoomedSlow", 1, 100, 1),
        toggle("game_decals", "#str_200136", "g_decals"),
        toggle("game_showgun", "#str_200138", "ui_showGun"),
        verb_choice("game_gunposition", "#str_223007", "gameGunPosition", "#str_223008", 3),
        choice("game_simpleitems", "#str_41215", "g_simpleItems", "#str_41216", (0, 1, 2, 3)),
        choice("game_opponentweapons", "#str_41217", "g_mpFlatOpponentWeapons", "#str_41218", (0, 1)),
        ("verb_toggle", "game_forcemodel", "#str_223006", "gameForceModel", None),
    )),
    ("mouse", "#str_229935", (
        choice("game_invertmouse", "#str_200139", "m_pitch", "#str_200059", ("0.022", "-0.022")),
        slider("game_msmooth", "#str_200140", "m_smooth", 1, 8, 1),
        slider("game_msensitivity", "#str_200141", "sensitivity", 0.01, 30, 0.01, 2),
        slider("game_mcpi", "#str_229936", "m_cpi", 0, 32000, 50),
        slider("game_mfilter", "#str_229937", "m_filter", 0, 31, 1),
        slider("game_maccel", "#str_229938", "cl_mouseAccel", -5, 5, 0.01, 2),
        slider("game_macceloffset", "#str_229939", "cl_mouseAccelOffset", 0, 50, 0.1, 1),
        slider("game_maccelpower", "#str_229940", "cl_mouseAccelPower", 1, 8, 0.05, 2),
        slider("game_msenscap", "#str_229941", "cl_mouseSensCap", 0, 100, 0.1, 1),
    )),
    ("controller", "#str_229924", (
        toggle("game_controller", "#str_229925", "in_joystick"),
        choice("game_southpaw", "#str_229930", "in_joystickSouthpaw", "#str_229934", (0, 1)),
        slider("game_deadzone", "#str_229926", "in_joystickDeadZone", 0, 0.95, 0.01, 2),
        slider("game_looksensitivity", "#str_229931", "in_joystickLookSensitivity", 0.1, 4, 0.05, 2),
        slider("game_wheelsensitivity", "#str_42840", "in_weaponWheelSensitivity", 0.25, 16, 0.25, 2),
        slider("game_wheeloffset", "#str_230140", "ui_weaponWheelOffset", -40, 40, 1),
        slider("game_lookcurve", "#str_229932", "in_joystickLookCurve", 1, 3, 0.05, 2),
        toggle("game_invertlook", "#str_229933", "in_joystickInvertLook"),
        slider("game_trigger", "#str_229927", "in_joystickTriggerThreshold", 0, 1, 0.01, 2),
        toggle("game_rumble", "#str_229928", "in_joystickRumble"),
        slider("game_rumblescale", "#str_229929", "in_joystickRumbleScale", 0, 2, 0.05, 2),
        toggle("game_gyro", "#str_229964", "in_gyro"),
        slider("game_gyrosensitivity", "#str_229965", "in_gyroSensitivity", 0, 8, 0.05, 2),
        choice("game_touchpad", "#str_229966", "in_touchpadMode", "#str_229967", (0, 1, 2, 3)),
        slider("game_touchpadsensitivity", "#str_229968", "in_touchpadSensitivity", 0, 8, 0.05, 2),
        toggle("game_touchscreen", "#str_229969", "in_touchscreen"),
        slider("game_batterythreshold", "#str_229970", "in_joystickLowBatteryRumbleThreshold", 0, 100, 1),
        slider("game_batteryscale", "#str_229971", "in_joystickLowBatteryRumbleScale", 0, 2, 0.05, 2),
        choice("game_promptstyle", "#str_231526", "in_promptStyle", "#str_231527", (0, 1, 2, 3, 4, 5)),
        choice("game_swapconfirm", "#str_231528", "in_swapConfirmBack", "#str_231529", (0, 1, 2)),
    )),
    ("crosshair", "#str_231543", (
        choice("game_customcrosshair", "#str_200307", "g_crosshairCustom", "#str_200296", ("0", "1")),
        ("crosshair", "game_crosshair", "#str_200308", "gameCrosshair", None),
        choice("game_crosshairsize", "#str_231764", "g_crosshairSize", "#str_201022", (16, 24, 32, 40, 48)),
        choice("game_crosshaircolor", "#str_231765", "g_crosshairColor", "#str_229942", CROSSHAIR_COLORS),
        toggle("game_hitmarker", "#str_229979", "hud_hitMarker"),
        toggle("game_crosshairanimated", "#str_231763", "hud_crosshairAnimated"),
    )),
    ("hud", "#str_231235", (
        choice("game_hudlayout", "#str_231236", "ui_hudLayout", "#str_231237", (0, 1, 2, 3)),
        slider("game_hudscale", "#str_231238", "ui_hudScale", 0.5, 2, 0.05, 2),
        choice("game_hudwidth", "#str_231239", "ui_hudWidth", "#str_231240", (0, 1, 2)),
        slider("game_hudinset", "#str_231241", "ui_hudSafeInset", 0, 10, 0.5, 1),
        toggle("game_objectives", "#str_231247", "g_objectivesToggle"),
        slider("game_hudopacity", "#str_231270", "ui_hudOpacity", 0.25, 1, 0.05, 2),
        toggle("game_damagedirection", "#str_231271", "ui_hudDamageDirection"),
        toggle("game_pickups", "#str_231272", "ui_hudPickups"),
        toggle("game_weaponstrip", "#str_231273", "ui_hudWeaponStrip"),
        toggle("game_reducedflashing", "#str_231274", "ui_reducedFlashing"),
        toggle("game_opaquebacking", "#str_231592", "ui_retainedOpaqueBacking"),
    )),
    ("gameplay", "#str_231544", (
        choice("game_corpsesink", "#str_229917", "g_corpseSink", "#str_229921", (0, 1, 2)),
        verb_choice("game_corpsetime", "#str_229918", "gameCorpseTime", "#str_229919", 8, enabled=7),
        verb_choice("game_language", "#str_229907", "gameLanguage", "#str_229908", LANGUAGES),
        choice("game_console", "#str_229922", "con_allowConsole", "#str_229923", (0, 1)),
        toggle("game_skipcinematics", "#str_229951", "g_autoSkipCinematics"),
        slider("game_cinematicshake", "#str_230157", "ui_cinematicShake", 0, 1, 0.1, 1),
    )),
    ("viewweapon", "#str_229952", (
        slider("game_gunfov", "#str_229953", "cl_gunfov", 0, 179, 1),
        choice("game_gunfovadjust", "#str_229954", "cl_gunfov_adjust", "#str_229955", (0, 1)),
        slider("game_gunx", "#str_229956", "cl_gun_x", -5, 5, 0.05, 2),
        slider("game_guny", "#str_229957", "cl_gun_y", -5, 5, 0.05, 2),
        slider("game_gunz", "#str_229958", "cl_gun_z", -5, 5, 0.05, 2),
        toggle("game_weaponkick", "#str_229982", "g_weaponMuzzleKick"),
    )),
    ("vr", "#str_230120", (
        choice("game_vr", "#str_230121", "vr_enable", "#str_200246", (0, 1)),
        choice("game_vraim", "#str_230122", "vr_aimMode", "#str_230123", (1, 0)),
        choice("game_vrhand", "#str_230124", "vr_leftHanded", "#str_230125", (0, 1)),
        choice("game_vrturn", "#str_230126", "vr_turnMode", "#str_230127", (0, 1)),
        choice("game_vrsnap", "#str_230128", "vr_snapTurnAngle", "#str_230129", (30, 45, 60, 90)),
        choice("game_vrmove", "#str_230130", "vr_moveDirection", "#str_230131", (0, 1)),
        choice("game_vrroomscale", "#str_230132", "vr_roomScale", "#str_200246", (0, 1)),
        choice("game_vrlaser", "#str_230133", "vr_aimLaser", "#str_230134", (0, 1, 2)),
        choice("game_vrhaptics", "#str_230135", "vr_hapticStrength", "#str_230136", ("0", "0.5", "1")),
        choice("game_vrtwohanded", "#str_230137", "vr_twoHanded", "#str_200246", (0, 1)),
        choice("game_vrvignette", "#str_230138", "vr_comfortVignette", "#str_230136", ("0", "0.5", "1")),
        choice("game_vrcrouch", "#str_230139", "vr_physicalCrouch", "#str_200246", (0, 1)),
        choice("game_vrplayspace", "#str_231760", "vr_playSpace", "#str_231761", (0, 1, 2)),
    )),
)
# The session's verbs, in the order HandleRetainedGameRequest takes them.
VERBS = ("gameGunPosition", "gameForceModel", "gameCrosshair", "gameCorpseTime", "gameLanguage")


def setting_rows() -> list:
    """Every row that sets a CVar: (cvar, kind, values or range)."""
    rows = []
    for _section, _heading, section_rows in SECTIONS:
        for kind, _ident, _label, binding, extra in section_rows:
            if kind in ("toggle", "slider", "choice"):
                rows.append((binding, kind, extra))
    return rows


def cvar_state(doc: Document, cvar: str, kind: str) -> dict:
    """A setting the page reads back from its CVar."""
    key = f"cvar.{cvar}"
    doc.state[key] = {"type": kind, "initial": {"string": "", "number": 0, "boolean": False}[kind], "cvar": cvar}
    return {"state": key}


def setting_action(doc: Document, cvar: str, kind: str) -> str:
    action = f"set.{cvar}"
    doc.actions[action] = {"operation": "settings.player.set", "input": kind, "arguments": {"cvar": cvar, "value": {"input": "value"}}}
    return action


# The tabs (section 13.7): each its id, label and two columns, a column
# whole sections under their headings, or a run of one section's rows
# (section, first, end) where the tab is that section alone.
TABS = (
    ("general", "#str_231542", (("general",), ("gameplay", "viewweapon"))),
    ("mouse", "#str_229935", (("mouse", 0, 5), ("mouse", 5, None))),
    ("controller", "#str_229924", (("controller", 0, 9), ("controller", 9, None))),
    ("hud", "#str_231235", (("hud",), ("crosshair",))),
    ("vr", "#str_230120", (("vr", 0, 6), ("vr", 6, None))),
)


def crosshair_row(doc: Document, ident: str, label: str, verb: str, value_x: float) -> dict:
    """The custom crosshair: a slider over the crosshairs, its number and the
    crosshair it names, which the session publishes (game.crosshairImage);
    usable while the custom crosshair is on, as on the stock page."""
    row_h, width, size = U * ROW_PITCH, U * COLUMN_W, 26.0
    # The slider gives the picture its room at the row's end.
    row = value_row(doc, ident, label, "slider", width - size - 6, value_x, value={"state": "game.crosshair"}, action=verb,
                    slider=(1, CROSSHAIRS, 1, 0), row_h=row_h, placement="screen", wrap=True)
    row["properties"]["width"] = length(width)
    row["children"].append({"id": f"{ident}-preview", "type": "image", "properties": {
        **absolute(left=width - 12 - size, top=(row_h - size) / 2, width=size, height=size), "image": image(""),
        "image-fit": keyword("contain"), "pointer-events": keyword("none")}})
    doc.bind(f"{ident}-preview.image", f"{ident}-preview", "image", {"state": "game.crosshairImage"})
    custom = {"op": "==", "args": [{"state": "cvar.g_crosshairCustom"}, "1"]}
    doc.bind(f"{ident}.enabled", ident, "enabled", custom)
    doc.bind(f"{ident}.opacity", ident, "opacity", {"op": "select", "args": [custom, 1, 0.38]})
    return row


def game_row(doc: Document, row: tuple, value_x: float) -> dict:
    """One of SECTIONS' rows as a setting row."""
    kind, ident, label, binding, extra = row
    if kind == "toggle":
        return setting_row(doc, ident, label, "toggle", value_x, value=cvar_state(doc, binding, "boolean"),
                           action=setting_action(doc, binding, "boolean"))
    if kind == "slider":
        minimum, maximum, step, decimals = extra
        shown = cvar_state(doc, binding, "number")
        if decimals:
            # A float CVar reads back inexactly (5.01 as 5.010000228): the
            # slider shows it to the row's places.
            scale = 10 ** decimals
            shown = {"op": "/", "args": [{"op": "round", "args": [{"op": "*", "args": [shown, scale]}]}, scale]}
        if (maximum - minimum) / step > FINE_STEPS:
            # Too many steps to reach a value by stepping: the row takes the
            # value typed as well, to the slider's places.
            return entry_row(doc, ident, label, value_x, shown, setting_action(doc, binding, "number"), (minimum, maximum, step, decimals))
        return setting_row(doc, ident, label, "slider", value_x, value=shown, action=setting_action(doc, binding, "number"),
                           slider=(minimum, maximum, step, decimals))
    if kind == "choice":
        key, values = extra
        kind_of = "string" if isinstance(values[0], str) else "number"
        return setting_row(doc, ident, label, "choice", value_x, value=cvar_state(doc, binding, kind_of),
                           action=setting_action(doc, binding, kind_of),
                           entries=[((key, index), value) for index, value in enumerate(values)])
    if kind == "verb_choice":
        key, count, enabled = extra
        state = {"gameGunPosition": "game.gunPosition", "gameCorpseTime": "game.corpseTime", "gameLanguage": "game.language"}[binding]
        entries = [((key, index), index, *([False] if enabled is not None and index >= enabled else [])) for index in range(count)]
        return setting_row(doc, ident, label, "choice", value_x, value={"state": state}, action=binding, entries=entries)
    if kind == "verb_toggle":
        return setting_row(doc, ident, label, "toggle", value_x, value={"state": "game.forceModel"}, action=binding)
    return crosshair_row(doc, ident, label, binding, value_x)


def game_document() -> dict:
    doc = Document("openq4.game")
    doc.state.update({
        "game.gunPosition": {"type": "number", "initial": -1}, "game.forceModel": {"type": "boolean", "initial": False},
        "game.crosshair": {"type": "number", "initial": 1}, "game.crosshairImage": {"type": "string", "initial": ""},
        "game.corpseTime": {"type": "number", "initial": 0}, "game.language": {"type": "number", "initial": 0},
        "game.languageReloaded": {"type": "boolean", "initial": False},
    })
    for verb in VERBS:
        doc.actions[verb] = {"operation": "session.menuValue", "input": "boolean" if verb == "gameForceModel" else "number",
                             "arguments": {"command": verb, "value": {"input": "value"}}}
    sections = {section: (heading_key, rows) for section, heading_key, rows in SECTIONS}
    value_x = value_column([row[2] for _section, _heading, rows in SECTIONS for row in rows])
    built = {row[1]: game_row(doc, row, value_x) for _section, _heading, rows in SECTIONS for row in rows}
    tabs = []
    for ident, key, columns in TABS:
        laid = []
        for column in columns:
            if len(column) == 3 and isinstance(column[1], int):
                section, first, end = column
                laid.append([built[row[1]] for row in sections[section][1][first:end]])
                continue
            items = []
            for section in column:
                heading_key, rows = sections[section]
                items += [heading(heading_key), *(built[row[1]] for row in rows)]
            laid.append(items)
        tabs.append((ident, key, laid))
    if sorted(node["id"] for node in built.values()) != sorted(node["id"] for _ident, _key, laid in tabs for column in laid for node in column
                                                              if isinstance(node, dict)):
        raise SystemExit("the GAME OPTIONS tabs must hold every row once")
    # The language reload re-activates the page (idUserInterfaceManagerLocal::
    # ReloadGuis); the session marks it, so the page returns to Language.
    language_tab = next(index for index, (_ident, _key, laid) in enumerate(tabs)
                        if any(isinstance(node, dict) and node["id"] == "game_language" for column in laid for node in column))
    activate = lambda focus: [{"op": "if", "condition": {"state": "game.languageReloaded"},
                               "then": [*show_tab(doc, "game", tabs, language_tab), {"op": "focus", "control": "game_language"}],
                               "else": focus}]
    return settings_subpage(doc, PREFIX, "#str_200084", tabs, activate=activate)
