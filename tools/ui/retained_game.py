"""The retained GAME OPTIONS settings page (docs/dev/ui-visual-design.md
sections 7, 9 and 13.7; UI-01): the stock page's rows
(guis/menu/settings/game.gui) in the settings sub-page
(retained_settings_rows), in eleven sections on five tabs of at most two
columns: GENERAL (gameplay | interface, items and players, video capture),
HUD (HUD | crosshair), VIEW WEAPON (its place | its view), ACCESSIBILITY
(motion | visibility) and VIRTUAL REALITY (its rows | the rest of them,
stereoscopic 3D). The stock page's mouse and
controller sections, with Free Look, Toggle Zoom and Scope Sensitivity, are
the CONTROLS page's MOUSE and CONTROLLER tabs (retained_controls, issue
#220).

The stock page grouped its rows by where they were added; the tabs group
them by what they change (the settings tree audit of 20 November 2026). The
comfort settings that were spread over three sections (view kick, screen and
cinematic shake, reduced flashing, the opaque menu backing) share
ACCESSIBILITY; Show Gun and Gun Position join the view weapon's offsets and
field of view; Language, Console Access and Screenshot Format leave the
gameplay rows for INTERFACE.

Each row applies at once, as the stock page's controls do. A row that names
a CVar reads it back through the page's CVar state and sets it with the
adapter's allowlisted settings.player.set (retained_settings_rows.cvar_row).
The rows the stock page computes go through the session
(idSessionLocal::HandleRetainedGameRequest): the gun position, the forced
pro skins, the custom crosshair, the corpse time and the language, which
reloads the language and the menus. A slider with more than a hundred steps
(the gun's field of view) takes the value typed as well. BACK, or the back
input, returns to the settings screen that opened the page."""
from __future__ import annotations

from build_retained_screens import U, Document, absolute, image, keyword, length, number
from retained_mp_menus import value_row
from retained_settings_rows import (COLUMN_W, ROW_PITCH, choice, cvar_row, cvar_settings, heading, requires, rows_value_column,
                                    setting_row, settings_subpage, show_tab, slider, toggle)

PREFIX = "game"
CROSSHAIRS = 20                            # the custom crosshairs (player_marine_mp_ui's mtr_crosshair keys)
# The crosshair colours the stock page offers (#str_229942), as g_crosshairColor.
CROSSHAIR_COLORS = ("1 1 1 1", "1 0 0 1", "1 0.5 0 1", "1 1 0 1", "0 1 0 1", "0 1 1 1", "0 0 1 1", "1 0 0.5 1")
LANGUAGES = 12                             # #str_229908, in sys_lang's order (RETAINED_GAME_LANGUAGES)
# The video capture rows' values (src/framework/VideoCapture.cpp): every
# container, the common frame rates, the window's own size (0) or a height it
# is scaled down to, and the timings.
VIDEO_FORMATS = ("mp4", "mkv", "webm", "mov", "avi", "gif")
VIDEO_FPS = (24, 30, 60, 120)
VIDEO_HEIGHTS = (0, 480, 720, 1080, 1440, 2160)
VIDEO_TIMINGS = ("auto", "fixed", "realtime")


def verb_choice(ident: str, label: str, verb: str, key: str, count: int, enabled: int | None = None) -> tuple:
    """A list the session applies; options from `enabled` on are shown, not chosen."""
    return ("verb_choice", ident, label, verb, (key, count, enabled))


# Each section: its id, heading and rows.
SECTIONS = (
    ("gameplay", "#str_231544", (
        toggle("game_autoreload", "#str_200134", "ui_autoReload"),
        choice("game_autoswitch", "#str_200135", "ui_autoSwitch", "#str_232540", (0, 1, 2)),
        toggle("game_decals", "#str_200136", "g_decals"),
        choice("game_corpsesink", "#str_229917", "g_corpseSink", "#str_229921", (0, 1, 2)),
        verb_choice("game_corpsetime", "#str_229918", "gameCorpseTime", "#str_229919", 8, enabled=7),
        toggle("game_enhancedphysics", "#str_231720", "g_enhancedPhysics"),
        toggle("game_skipcinematics", "#str_229951", "g_autoSkipCinematics"),
    )),
    ("interface", "#str_230018", (
        verb_choice("game_language", "#str_229907", "gameLanguage", "#str_229908", LANGUAGES),
        choice("game_console", "#str_229922", "con_allowConsole", "#str_229923", (0, 1)),
        choice("game_screenshotformat", "#str_231775", "r_screenshotFormat", "#str_231776", (0, 1)),
    )),
    ("players", "#str_232545", (
        choice("game_simpleitems", "#str_41215", "g_simpleItems", "#str_41216", (0, 1, 2, 3)),
        choice("game_opponentweapons", "#str_41217", "g_mpFlatOpponentWeapons", "#str_41218", (0, 1)),
        ("verb_toggle", "game_forcemodel", "#str_223006", "gameForceModel", None),
    )),
    ("hud", "#str_231235", (
        choice("game_hudlayout", "#str_231236", "ui_hudLayout", "#str_231237", (0, 1, 2, 3)),
        slider("game_hudscale", "#str_231238", "ui_hudScale", 0.5, 2, 0.05, 2),
        choice("game_hudwidth", "#str_231239", "ui_hudWidth", "#str_231240", (0, 1, 2)),
        slider("game_hudinset", "#str_231241", "ui_hudSafeInset", 0, 10, 0.5, 1),
        slider("game_hudopacity", "#str_231270", "ui_hudOpacity", 0.25, 1, 0.05, 2),
        toggle("game_objectives", "#str_231247", "g_objectivesToggle"),
        toggle("game_damagedirection", "#str_231271", "ui_hudDamageDirection"),
        toggle("game_pickups", "#str_231272", "ui_hudPickups"),
        toggle("game_weaponstrip", "#str_231273", "ui_hudWeaponStrip"),
    )),
    ("crosshair", "#str_231543", (
        choice("game_customcrosshair", "#str_200307", "g_crosshairCustom", "#str_200296", ("0", "1")),
        ("crosshair", "game_crosshair", "#str_200308", "gameCrosshair", None),
        choice("game_crosshairsize", "#str_231764", "g_crosshairSize", "#str_201022", (16, 24, 32, 40, 48)),
        choice("game_crosshaircolor", "#str_231765", "g_crosshairColor", "#str_229942", CROSSHAIR_COLORS),
        toggle("game_hitmarker", "#str_229979", "hud_hitMarker"),
        toggle("game_crosshairanimated", "#str_231763", "hud_crosshairAnimated"),
    )),
    # The weapon's place on screen (VIEW_PLACE rows), then how it is viewed.
    ("viewweapon", "#str_229952", (
        toggle("game_showgun", "#str_200138", "ui_showGun"),
        verb_choice("game_gunposition", "#str_223007", "gameGunPosition", "#str_223008", 3),
        slider("game_gunx", "#str_229956", "cl_gun_x", -5, 5, 0.05, 2),
        slider("game_guny", "#str_229957", "cl_gun_y", -5, 5, 0.05, 2),
        slider("game_gunz", "#str_229958", "cl_gun_z", -5, 5, 0.05, 2),
        slider("game_gunfov", "#str_229953", "cl_gunfov", 0, 179, 1),
        choice("game_gunfovadjust", "#str_229954", "cl_gunfov_adjust", "#str_229955", (0, 1)),
        toggle("game_weaponkick", "#str_229982", "g_weaponMuzzleKick"),
    )),
    ("motion", "#str_232543", (
        slider("game_viewkick", "#str_231773", "g_viewKick", 0, 1, 0.1, 1),
        slider("game_screenshake", "#str_231774", "g_screenShake", 0, 1, 0.1, 1),
        slider("game_cinematicshake", "#str_230157", "ui_cinematicShake", 0, 1, 0.1, 1),
    )),
    ("visibility", "#str_232544", (
        toggle("game_reducedflashing", "#str_231274", "ui_reducedFlashing"),
        toggle("game_opaquebacking", "#str_231592", "ui_retainedOpaqueBacking"),
    )),
    # On and off settings are toggles here as everywhere else; the stock
    # page listed them as Disabled and Enabled.
    ("vr", "#str_230120", (
        toggle("game_vr", "#str_230121", "vr_enable"),
        choice("game_vraim", "#str_230122", "vr_aimMode", "#str_230123", (1, 0)),
        choice("game_vrhand", "#str_230124", "vr_leftHanded", "#str_230125", (0, 1)),
        choice("game_vrturn", "#str_230126", "vr_turnMode", "#str_230127", (0, 1)),
        choice("game_vrsnap", "#str_230128", "vr_snapTurnAngle", "#str_230129", (30, 45, 60, 90)),
        choice("game_vrmove", "#str_230130", "vr_moveDirection", "#str_230131", (0, 1)),
        toggle("game_vrroomscale", "#str_230132", "vr_roomScale"),
        choice("game_vrlaser", "#str_230133", "vr_aimLaser", "#str_230134", (0, 1, 2)),
        choice("game_vrhaptics", "#str_230135", "vr_hapticStrength", "#str_230136", ("0", "0.5", "1")),
        toggle("game_vrtwohanded", "#str_230137", "vr_twoHanded"),
        choice("game_vrvignette", "#str_230138", "vr_comfortVignette", "#str_230136", ("0", "0.5", "1")),
        toggle("game_vrcrouch", "#str_230139", "vr_physicalCrouch"),
        choice("game_vrplayspace", "#str_231760", "vr_playSpace", "#str_231761", (0, 1, 2)),
        choice("game_vrbody", "#str_232514", "vr_body", "#str_232515", (0, 1, 2)),
    )),
    # Stereoscopic 3D without a headset (r_stereo3D, docs/user/stereoscopic-3d.md),
    # under the VR rows on VIRTUAL REALITY's second column.
    ("stereo", "#str_232920", (
        choice("game_stereo3d", "#str_232921", "r_stereo3D", "#str_232922", (0, 1, 2, 3, 4, 5)),
        slider("game_stereodepth", "#str_232923", "r_stereoSeparation", 0, 8, 0.5, 1),
        slider("game_stereoconvergence", "#str_232924", "r_stereoConvergence", 32, 512, 16),
        toggle("game_stereoswap", "#str_232925", "r_stereoSwapEyes"),
    )),
    # Video capture (recordVideo, docs/user/video-capture.md), on GENERAL
    # under Screenshot Format's column: the strip has no room for a sixth tab
    # in every language. The codec, bitrates, encoder and FFmpeg's path stay
    # console settings.
    ("video", "#str_232900", (
        choice("game_videoformat", "#str_232903", "com_videoFormat", "#str_232904", VIDEO_FORMATS),
        slider("game_videoquality", "#str_232905", "com_videoQuality", 1, 100, 1),
        choice("game_videofps", "#str_232906", "com_videoFPS", "#str_232907", VIDEO_FPS),
        choice("game_videoheight", "#str_232908", "com_videoHeight", "#str_232909", VIDEO_HEIGHTS),
        toggle("game_videoaudio", "#str_232910", "com_videoAudio"),
        choice("game_videotiming", "#str_232911", "com_videoTiming", "#str_232912", VIDEO_TIMINGS),
    )),
)
VIEW_PLACE = 5                             # the view weapon's rows that place it: Show Gun to the Z offset
# The session's verbs, in the order HandleRetainedGameRequest takes them.
VERBS = ("gameGunPosition", "gameForceModel", "gameCrosshair", "gameCorpseTime", "gameLanguage")


def setting_rows() -> list:
    """Every row that sets a CVar: (cvar, kind, values or range)."""
    return cvar_settings(row for _section, _heading, rows in SECTIONS for row in rows)


# The tabs (section 13.7): each its id, label and two columns, a column
# whole sections under their headings, or a run of one section's rows
# (section, first, end) where the tab is that section alone.
TABS = (
    ("general", "#str_231542", (("gameplay",), ("interface", "players", "video"))),
    ("hud", "#str_231235", (("hud",), ("crosshair",))),
    ("viewweapon", "#str_229952", (("viewweapon", 0, VIEW_PLACE), ("viewweapon", VIEW_PLACE, None))),
    ("accessibility", "#str_232542", (("motion",), ("visibility",))),
    ("vr", "#str_230120", (("vr", 0, 6), (("vr", 6, None), "stereo"))),
)


def _cvar(name: str) -> dict:
    return {"state": f"cvar.{name}"}


def _all(*terms) -> dict:
    result = terms[0]
    for term in terms[1:]:
        result = {"op": "&&", "args": [result, term]}
    return result


def dependencies() -> dict:
    """The rows another setting turns off (retained_settings_rows.requires),
    each the condition under which it does anything:
    - the HUD's sizes and opacity with the Classic layout, which draws the
      stock HUD unchanged (idLegacyHudLayout::Read), and Damage Direction,
      Pickup Messages and the Weapon Strip with Classic or Minimal, which
      leaves them out;
    - Weapon FOV with Show Gun off (the view weapon is not drawn), and
      Weapon FOV Aspect while Weapon FOV is 0 too (it adjusts only a set FOV);
    - every VR row with VR Mode off (no OpenXR session), Snap Turn Angle with
      smooth turning, and the Aim Laser and Two-Handed Grip with head aim;
    - Record Sound while the video format is GIF, which has no sound;
    - the stereo depth, convergence and eye swap with Stereoscopic 3D off.
    The gun's offsets, position and kick stay: they still move the hidden
    weapon, whose barrel some weapons fire from."""
    layout = _cvar("ui_hudLayout")
    remastered = {"op": "!=", "args": [layout, 0]}
    trimmed = _all(remastered, {"op": "!=", "args": [layout, 2]})
    shown = _cvar("ui_showGun")
    vr = _cvar("vr_enable")
    hand_aim = _all(vr, {"op": "==", "args": [_cvar("vr_aimMode"), 1]})
    return {
        **{ident: remastered for ident in ("game_hudscale", "game_hudwidth", "game_hudinset", "game_hudopacity")},
        **{ident: trimmed for ident in ("game_damagedirection", "game_pickups", "game_weaponstrip")},
        "game_gunfov": shown,
        "game_gunfovadjust": _all(shown, {"op": ">", "args": [_cvar("cl_gunfov"), 0]}),
        **{row[1]: vr for row in next(rows for section, _heading, rows in SECTIONS if section == "vr")[1:]},
        "game_vrsnap": _all(vr, {"op": "!=", "args": [_cvar("vr_turnMode"), 1]}),
        "game_vrlaser": hand_aim,
        "game_vrtwohanded": hand_aim,
        "game_videoaudio": {"op": "!=", "args": [_cvar("com_videoFormat"), "gif"]},
        **{ident: {"op": "!=", "args": [_cvar("r_stereo3D"), 0]}
           for ident in ("game_stereodepth", "game_stereoconvergence", "game_stereoswap")},
    }


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
    if kind == "verb_choice":
        key, count, enabled = extra
        state = {"gameGunPosition": "game.gunPosition", "gameCorpseTime": "game.corpseTime", "gameLanguage": "game.language"}[binding]
        entries = [((key, index), index, *([False] if enabled is not None and index >= enabled else [])) for index in range(count)]
        return setting_row(doc, ident, label, "choice", value_x, value={"state": state}, action=binding, entries=entries)
    if kind == "verb_toggle":
        return setting_row(doc, ident, label, "toggle", value_x, value={"state": "game.forceModel"}, action=binding)
    if kind == "crosshair":
        return crosshair_row(doc, ident, label, binding, value_x)
    return cvar_row(doc, row, value_x)


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
    value_x = rows_value_column(row for _section, _heading, rows in SECTIONS for row in rows)
    built = {row[1]: game_row(doc, row, value_x) for _section, _heading, rows in SECTIONS for row in rows}
    for ident, condition in dependencies().items():
        requires(doc, built[ident], condition)
    tabs = []
    for ident, key, columns in TABS:
        laid = []
        for column in columns:
            # a column is one run of a section's rows (section, first, end),
            # or parts: whole sections under their headings, and runs
            parts = (column,) if len(column) == 3 and isinstance(column[1], int) else column
            items = []
            for part in parts:
                if isinstance(part, tuple):
                    section, first, end = part
                    items += [built[row[1]] for row in sections[section][1][first:end]]
                    continue
                heading_key, rows = sections[part]
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
