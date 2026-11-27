"""Menu tooltips (docs/dev/ui/tooltips.md; docs/dev/ui-visual-design.md
sections 7 and 8, WID-012; UI-01): a short explanation of a setting that waits until the player rests
on it, and stays out of the way otherwise.

A settings page's rows each name a tooltip (Control::tooltip), a sentence
or two in the language tables saying what the setting does and what it
costs. The page carries one tooltip card (Document.h's TooltipSpec): the
section 7 small popover, a compact cut plate whose black body sits inside
one marine.rail.card rail with an 8 dp cut at its top-trailing and
bottom-leading corners, a second rail inside the top cut, and the text in
the settings rows' 14 dp face, wrapping at 300 dp.

The runtime decides when it shows: 300 ms after the pointer comes to rest
on a row, at once when the pointer moves on from one row's tooltip to the
next, 500 ms after keyboard or controller focus lands on a row, never under
a finger, never while a list is open or a field is being typed in, and
never again on the same row after a press, key, wheel or edit until the
subject changes. It places the card clear of the pointer's arrow, or under
the focused row, inside the safe area; the card takes no input. The card
fades in over the section 8 `tooltip` token (100 ms) and drifts 4 dp into
place; reduced motion shows it at once. `ui_tooltips 0` (Game Options,
Accessibility, Menu Tooltips) turns them off.

Every value row of the pages this module serves must name a tooltip:
generation fails on a row without one, or on a table entry for a row the
page does not have."""
from __future__ import annotations

import build_retained_screens as b
from build_retained_screens import (EASE_OUT, RAIL_CARD, absolute, group, keyword, label, length, number, path, rgb, solid,
                                    stroke, track, transform, typeface, vector)
from retained_mp_menus import PLACEHOLDER

STATE = "tooltip.text"                     # the runtime's: the shown tooltip's text, empty while none shows
ENABLED = "tooltip.enabled"                # ui_tooltips
CARD = "tooltip"
ENTER = "tooltip.enter"
ENTER_MS = 100                             # section 8's tooltip token
DRIFT = 4.0                                # dp the card travels into place
MAX_W = 300.0                              # dp: the text's widest line at 100 % text
PAD_X, PAD_TOP, PAD_BOTTOM = 11.0, 7.0, 8.0   # dp
CUT, NICK = 8.0, 2.0                       # dp: the major and minor corner cuts
BODY = [0, 0, 0, 0.94]                     # the card's body (section 6)
TEXT_TINT = [1, 1, 1, 0.9]
FACE = ("lowpixel", 14, 18)                # the settings rows' labels: face, size, line height (dp)
# The control roles every row must explain; buttons explain themselves by
# their labels unless the table names them.
VALUE_ROLES = ("toggle", "slider", "choice", "number", "text")
# A fine slider's typed number shares its slider's tooltip.
SHARED_SUFFIXES = ("_number",)

# The settings pages' tooltips: control id -> language key.
TOOLTIPS = {
    "game_autoreload": "#str_233001",
    "game_autoswitch": "#str_233002",
    "game_decals": "#str_233003",
    "game_corpsesink": "#str_233004",
    "game_corpsetime": "#str_233005",
    "game_enhancedphysics": "#str_233006",
    "game_skipcinematics": "#str_233007",
    "game_tooltips": "#str_233008",
    "game_language": "#str_233009",
    "game_console": "#str_233010",
    "game_screenshotformat": "#str_233011",
    "game_simpleitems": "#str_233012",
    "game_opponentweapons": "#str_233013",
    "game_forcemodel": "#str_233014",
    "game_videoformat": "#str_233015",
    "game_videoquality": "#str_233016",
    "game_videofps": "#str_233017",
    "game_videoheight": "#str_233018",
    "game_videoaudio": "#str_233019",
    "game_videotiming": "#str_233020",
    "game_hudlayout": "#str_233021",
    "game_hudscale": "#str_233022",
    "game_hudwidth": "#str_233023",
    "game_hudinset": "#str_233024",
    "game_hudopacity": "#str_233025",
    "game_objectives": "#str_233026",
    "game_damagedirection": "#str_233027",
    "game_pickups": "#str_233028",
    "game_weaponstrip": "#str_233029",
    "game_customcrosshair": "#str_233030",
    "game_crosshair": "#str_233031",
    "game_crosshairsize": "#str_233032",
    "game_crosshaircolor": "#str_233033",
    "game_hitmarker": "#str_233034",
    "game_crosshairanimated": "#str_233035",
    "game_showgun": "#str_233036",
    "game_gunposition": "#str_233037",
    "game_gunx": "#str_233038",
    "game_guny": "#str_233039",
    "game_gunz": "#str_233040",
    "game_gunfov": "#str_233041",
    "game_gunfovadjust": "#str_233042",
    "game_weaponkick": "#str_233043",
    "game_viewkick": "#str_233044",
    "game_screenshake": "#str_233045",
    "game_cinematicshake": "#str_233046",
    "game_reducedflashing": "#str_233047",
    "game_opaquebacking": "#str_233048",
    "game_vr": "#str_233049",
    "game_vraim": "#str_233050",
    "game_vrhand": "#str_233051",
    "game_vrturn": "#str_233052",
    "game_vrsnap": "#str_233053",
    "game_vrmove": "#str_233054",
    "game_vrroomscale": "#str_233055",
    "game_vrlaser": "#str_233056",
    "game_vrhaptics": "#str_233057",
    "game_vrtwohanded": "#str_233058",
    "game_vrvignette": "#str_233059",
    "game_vrcrouch": "#str_233060",
    "game_vrplayspace": "#str_233061",
    "game_vrbody": "#str_233062",
    # VR SETUP's rows on GAME OPTIONS (retained_vr) share the tab's tooltips.
    "vrsetup_playspace": "#str_233061",
    "vrsetup_turn": "#str_233052",
    "vrsetup_hand": "#str_233051",
    "vrsetup_vignette": "#str_233059",
    "game_stereo3d": "#str_233063",
    "game_stereodepth": "#str_233064",
    "game_stereoconvergence": "#str_233065",
    "game_stereoswap": "#str_233066",
    "settings_preset": "#str_233067",
    "settings_autodetect": "#str_233068",
    "settings_backend": "#str_233069",
    "settings_gpu": "#str_233070",
    "settings_renderer": "#str_233071",
    "settings_restart_renderer": "#str_233072",
    "settings_ui_scale": "#str_233073",
    "settings_text_scale": "#str_233074",
    "settings_ui_aspect": "#str_233075",
    "settings_reset_sizes": "#str_233076",
    "settings_display_device": "#str_233077",
    "settings_multiscreen": "#str_233078",
    "settings_window_mode": "#str_233079",
    "settings_vsync": "#str_233080",
    "settings_framecap": "#str_233081",
    "settings_window_width": "#str_233082",
    "settings_window_height": "#str_233083",
    "settings_display_mode": "#str_233084",
    "settings_custom_width": "#str_233085",
    "settings_custom_height": "#str_233086",
    "settings_display_refresh": "#str_233087",
    "settings_brightness": "#str_233088",
    "settings_resolution_scale": "#str_233089",
    "settings_msaa": "#str_233090",
    "settings_postaa": "#str_233091",
    "settings_anisotropy": "#str_233092",
    "settings_specular": "#str_233093",
    "settings_bump": "#str_233094",
    "settings_sky": "#str_233095",
    "settings_ambient_enabled": "#str_233096",
    "settings_ambient": "#str_233097",
    "settings_shadows": "#str_233098",
    # The reset marks of the rows a preset fills (retained_system.preset_mark).
    "settings_preset_reset": "#str_233099",
    "settings_msaa_reset": "#str_233099",
    "settings_postaa_reset": "#str_233099",
    "settings_anisotropy_reset": "#str_233099",
    "settings_shadows_reset": "#str_233099",
    "settings_bloom_reset": "#str_233099",
    "settings_ssao_reset": "#str_233099",
    "settings_tonemap_reset": "#str_233099",
    "settings_motionblur_reset": "#str_233099",
    "settings_depthoffield_reset": "#str_233099",
    "settings_crt_reset": "#str_233099",
    "settings_irradiance_reset": "#str_233099",
    "settings_specular_reset": "#str_233099",
    "settings_bump_reset": "#str_233099",
    "settings_sky_reset": "#str_233099",
    "settings_character_self_shadow": "#str_233100",
    "settings_irradiance": "#str_233101",
    "settings_lightgrid_preload": "#str_233102",
    "settings_bloom": "#str_233103",
    "settings_ssao": "#str_233104",
    "settings_tonemap": "#str_233105",
    "settings_motionblur": "#str_233106",
    "settings_depthoffield": "#str_233107",
    "settings_crt": "#str_233108",
    "settings_apply": "#str_233109",
    "audio_volume": "#str_233110",
    "audio_music": "#str_233111",
    "audio_dialogue": "#str_233112",
    "audio_backend": "#str_233113",
    "audio_device": "#str_233114",
    "audio_eax": "#str_233115",
    "audio_speakers": "#str_233116",
    "audio_hrtf": "#str_233117",
    "audio_subtitles": "#str_233118",
    "audio_subtitle_size": "#str_233119",
    "audio_subtitle_background": "#str_233120",
    "audio_subtitle_speakers": "#str_233121",
    "audio_subtitle_untranscribed": "#str_233122",
    "controls_touch_layout": "#str_233123",
    "controls_touchcontrols": "#str_233405",
    "controls_touchpreset": "#str_233408",
    "controls_touchsize": "#str_233406",
    "controls_touchlook": "#str_233407",
    "controls_freelook": "#str_233124",
    "controls_invertmouse": "#str_233125",
    "controls_togglezoom": "#str_233126",
    "controls_scopesensitivity": "#str_233127",
    "controls_msensitivity": "#str_233128",
    "controls_mcpi": "#str_233129",
    "controls_msmooth": "#str_233130",
    "controls_mfilter": "#str_233131",
    "controls_maccel": "#str_233132",
    "controls_macceloffset": "#str_233133",
    "controls_maccelpower": "#str_233134",
    "controls_msenscap": "#str_233135",
    "controls_controller": "#str_233136",
    "controls_device": "#str_233137",
    "controls_test": "#str_233138",
    "controls_southpaw": "#str_233139",
    "controls_wheelsensitivity": "#str_233140",
    "controls_wheeloffset": "#str_233141",
    "controls_layout": "#str_233142",
    "controls_trigger": "#str_233143",
    "controls_togglecrouch": "#str_233144",
    "controls_alwaysrun": "#str_233145",
    "controls_context": "#str_233146",
    "controls_resetcontroller": "#str_233147",
    "controls_resetkeyboard": "#str_233148",
    "controls_rumble": "#str_233149",
    "controls_rumblescale": "#str_233150",
    "controls_triggerrumble": "#str_233151",
    "controls_adaptivetriggers": "#str_233152",
    "controls_directionalhits": "#str_233153",
    "controls_batterythreshold": "#str_233154",
    "controls_batteryscale": "#str_233155",
    "controls_light": "#str_233156",
    "controls_playerlight": "#str_233157",
    "controls_batterynotice": "#str_233158",
    "controls_promptstyle": "#str_233159",
    "controls_swapconfirm": "#str_233160",
    "controls_response": "#str_233161",
    "controls_turnspeed": "#str_233162",
    "controls_pitchspeed": "#str_233163",
    "controls_lookcurve": "#str_233164",
    "controls_invertlook": "#str_233165",
    "controls_turnboost": "#str_233166",
    "controls_aimassist": "#str_233167",
    "controls_lookdeadzone": "#str_233168",
    "controls_lookouterzone": "#str_233169",
    "controls_movedeadzone": "#str_233170",
    "controls_moveouterzone": "#str_233171",
    "controls_moveshape": "#str_233172",
    "controls_movecurve": "#str_233173",
    "controls_gyro": "#str_233174",
    "controls_gyroturn": "#str_233175",
    "controls_gyrotilt": "#str_233176",
    "controls_gyrospace": "#str_233177",
    "controls_gyroactive": "#str_233178",
    "controls_gyroinvertturn": "#str_233179",
    "controls_gyroinverttilt": "#str_233180",
    "controls_gyrotightening": "#str_233181",
    "controls_gyropointer": "#str_233182",
    "controls_gyrocalibrate": "#str_233183",
    "controls_touchpad": "#str_233184",
    "controls_touchpadsensitivity": "#str_233185",
    "controls_touchscreen": "#str_233186",
}
LABEL = "#str_233000"                      # Menu Tooltips, the ACCESSIBILITY tab's row that turns them off
# The pages whose rows explain themselves; their modals' buttons are left out.
PAGES = ("game", "system", "audio", "controls")


def _f(fraction: float, dp: float = 0.0) -> dict:
    return {"fraction": fraction, "dp": dp}


def card_frame() -> dict:
    """The small popover's plate, drawn in fractions of the card's box so it
    fits whatever the text needs."""
    edge = 0.5   # the rail's centre, half its width in from the box
    w, h = _f(1, -edge), _f(1, -edge)
    outline = [(NICK + edge, edge), (_f(1, -CUT - edge), edge), (w, CUT + edge), (w, _f(1, -NICK - edge)),
               (_f(1, -NICK - edge), h), (CUT + edge, h), (edge, _f(1, -CUT - edge)), (edge, NICK + edge)]
    return vector(f"{CARD}-frame", {**b.FULL, "pointer-events": keyword("none")}, [
        path("body", outline, fill=solid(BODY)),
        path("rail", outline, stroke=stroke(solid(rgb(RAIL_CARD, 0.85)), 1)),
        # A second rail inside the top-trailing cut: the 45-degree motif.
        path("chevron", [(_f(1, -CUT - 5), edge), (_f(1, -edge), CUT + 5)], closed=False,
             stroke=stroke(solid(rgb(RAIL_CARD, 0.45)), 1)),
    ])


def card(doc_state: dict, bindings: list) -> tuple:
    """The tooltip card and its entrance, for a document's root."""
    doc_state[STATE] = {"type": "string", "initial": ""}
    doc_state[ENABLED] = {"type": "boolean", "initial": True, "cvar": "ui_tooltips"}
    face, size, line = FACE
    text = label(f"{CARD}-text", PLACEHOLDER, {"position": keyword("relative"), "display": keyword("block"),
                                              **typeface(face, size, line, TEXT_TINT), "white-space": keyword("normal")})
    node = group(CARD, {**absolute(left=0, top=0), "width": keyword("auto"), "max-width": length(MAX_W + 2 * PAD_X),
                        "padding-left": length(PAD_X), "padding-right": length(PAD_X),
                        "padding-top": length(PAD_TOP), "padding-bottom": length(PAD_BOTTOM),
                        "display": keyword("none"), "pointer-events": keyword("none"), "opacity": number(1),
                        "transform": transform()},
                 [card_frame(), text])
    bindings.append({"id": f"{CARD}-text.text", "node": f"{CARD}-text", "property": "text", "value": {"state": STATE}})
    bindings.append({"id": f"{CARD}.display", "node": CARD, "property": "display", "value": {"op": "select", "args": [
        {"op": "&&", "args": [{"state": ENABLED}, {"op": "!=", "args": [{"state": STATE}, ""]}]}, "block", "none"]}})
    enter = {"id": ENTER, "durationMs": ENTER_MS, "tracks": [
        track(CARD, "opacity", [(0, number(0)), (ENTER_MS, number(1))]),
        track(CARD, "transform", [(0, transform(ty=DRIFT), EASE_OUT), (ENTER_MS, transform())]),
    ]}
    return node, enter


def _controls(node: dict):
    if node.get("control"):
        yield node
    for child in node.get("children", []):
        yield from _controls(child)


def _modal_controls(node: dict, inside: bool = False):
    inside = inside or "modal" in node
    if inside and node.get("control"):
        yield node["id"]
    for child in node.get("children", []):
        yield from _modal_controls(child, inside)


def with_tooltips(result: dict) -> dict:
    """A built settings page whose rows name their tooltips, with the card
    over the page as the root's last child."""
    in_modal = set(_modal_controls(result["root"]))
    named, missing = set(), []
    for node in _controls(result["root"]):
        ident, control = node["id"], node["control"]
        owner = next((ident[:-len(suffix)] for suffix in SHARED_SUFFIXES if ident.endswith(suffix) and ident[:-len(suffix)] in TOOLTIPS),
                     ident)
        if owner in TOOLTIPS and ident not in in_modal:
            control["tooltip"] = TOOLTIPS[owner]
            named.add(owner)
        elif control.get("role") in VALUE_ROLES and ident not in in_modal:
            missing.append(ident)
    if missing:
        raise SystemExit(f"{result['id']}: settings rows without a tooltip: {', '.join(missing)}")
    if not named:
        raise SystemExit(f"{result['id']}: no row names a tooltip")
    state = result.setdefault("state", {})
    bindings = result.setdefault("bindings", [])
    node, enter = card(state, bindings)
    result.setdefault("timelines", []).append(enter)
    result["root"].setdefault("children", []).append(node)
    result["tooltip"] = {"state": STATE, "card": CARD, "enter": ENTER}
    return result


def check_table(documents: dict) -> None:
    """Every table entry names a row one of the pages has."""
    present = {node["id"] for name in PAGES for node in _controls(documents[name]["root"])}
    stale = sorted(ident for ident in TOOLTIPS if ident not in present)
    if stale:
        raise SystemExit("tooltips for rows no settings page has: " + ", ".join(stale))
