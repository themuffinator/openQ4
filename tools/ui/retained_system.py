"""The retained SYSTEM settings page (docs/dev/ui-visual-design.md sections 6,
7, 9 and 13.7; UI-01; docs/dev/ui/system-settings-contract.md): the
SETTINGS page's DISPLAY section as a sub-page (retained_settings_rows), its
settings on three tabs of at most two columns: GRAPHICS (performance and
rendering back end | interface sizes), SCREEN (display | display sizing) and
IMAGE QUALITY (image quality | rendering).

Unlike the other settings pages, nothing here applies at once. Every row edits
the settings service's draft (settings.system.edit and its kin, the service's
settings.* state), and APPLY CHANGES, under the columns beside the service's
message, applies the draft; a display change then asks Keep or Revert, which
reverts by itself after the service's 15 s countdown. Back, or the Menu
button with nothing to apply, leaves; with a draft not applied it asks first
(Apply changes, Keep editing, Discard changes). The Menu button applies a
draft. Performance Preset and Auto-Detect fill the draft from the shared
framework presets; the renderer back end and device lists, the display,
resolution and refresh lists are the service's. The window and custom sizes
take typed whole numbers, and Brightness and Ambient brightness pair their
slider with a typed number, which the adapter holds as number drafts until
they are committed.

The session opens the page over the screen that asked for it
(idSessionLocal::OpenSystemSettings): from the SETTINGS page it goes one
level deeper as the other settings pages do, and its Back climbs out onto
DISPLAY. A page that is missing or invalid leaves the stock SYSTEM page in
use."""
from __future__ import annotations

import math

from build_retained_screens import (OLIVE, ORANGE, U, VALUE, Document, absolute, colour, fixed_text, group, keyword, label, length,
                                    number, path, rgb, solid, stroke, text_width, typeface, vector)
from retained_difficulty import primary_plate
from retained_mp_menus import PLACEHOLDER, any_text, choice_popup, row_face, row_states, value_row, wrapped_width
from retained_settings_rows import (COLUMN_W, CONTENT_LEFT, CONTENT_W, ROW_PITCH, ROWS_TOP, button_row, heading, row_chrome,
                                    settings_subpage, value_column)
import system_display_catalog

PREFIX = "settings"
BODY = "settings-body"                    # the pages' bounds, where every list unfolds
SLOTS = 12                                # rows a column holds above APPLY CHANGES
FOOTER_TOP = ROWS_TOP + SLOTS * ROW_PITCH + 6   # u: the message and APPLY CHANGES
APPLY_H = 30.0                            # u: APPLY CHANGES, a 45 dp target
APPLY_TEXT = 20.0                         # dp
NUMBER_W = 66.0                           # dp: a slider's paired number field
ERROR_TINT = [0.8941, 0.4275, 0.3373, 1]
NOTE_FACE = ("lowpixel", 14, [1, 1, 1, 0.6])
NOTE_W = U * COLUMN_W - 29                # dp: a note under the label column, to the row's end
NOTE_LINE = 18.0                          # dp: a note's leading, two lines to a slot
DIALOG_W = 35                             # em: the dialogs grow with the text size

# The settings service's catalog: each setting's draft and baseline state
# (SystemSettingsHost::Schema()), its type and initial value.
CATALOG = {
    **{key: ("string", "") for key in ("com_performancePreset", "r_renderer", "r_rendererBenchmarkPreset", "r_renderApi",
                                       "r_renderDevice")},
    **{key: ("boolean", False) for key in (
        "r_bloom", "r_borderless", "r_crt", "r_fullscreen", "r_fullscreenDesktop", "r_hdrToneMap", "r_lightGridPreload", "r_shadows",
        "r_skipBump", "r_skipSky", "r_skipSpecular", "r_ssao", "r_useLightGrid", "ui_aspectCorrection", "image_downSize",
        "image_downSizeBump", "image_downSizeSpecular", "image_ignoreHighQuality", "image_writeGeneratedImages", "r_motionBlur",
        "r_shadowMapCSM", "r_shadowMapProjectedCSM", "r_shadowMapCascadeStabilize", "r_motionBlurObjectVectors",
        "r_shadowMapHashedAlpha", "r_shadowMapTranslucentReceivers", "r_useShadowMap", "s_useEAXReverb")},
    **{key: ("boolean", True) for key in ("r_shadowMapPointLights", "r_hdrSceneTarget")},
    **{key: ("number", 0) for key in (
        "r_brightness", "r_customHeight", "r_customWidth", "r_displayRefresh", "r_forceAmbient", "r_mode", "r_multiSamples",
        "r_multiScreen", "r_postAA", "r_screen", "r_screenFraction", "r_swapInterval", "r_windowHeight", "r_windowWidth",
        "com_machineSpec", "com_maxfps", "image_anisotropy", "image_downSizeBumpLimit", "image_downSizeLimit",
        "image_downSizeSpecularLimit", "image_usePrecompressedTextures", "r_rendererUploadFrameBuffers", "r_rendererUploadMegs",
        "r_shadowMapMaxUpdatesPerView", "r_shadowMapCascadeCount", "r_shadowMapPointFilterMode", "image_picmip", "r_shadowMapSize",
        "s_maxEmitterChannels", "s_maxSoundsPerShader", "s_numberOfSpeakers")},
    "r_shadowMapPointSize": ("number", 512), "r_shadowMapFilterTaps": ("number", 9), "r_shadowMapPointFilterTaps": ("number", 9),
    "r_shadowMapFilterMode": ("number", 2), "r_shadowMapAtlasSize": ("number", 4096), "r_ssaoSamples": ("number", 20),
    "r_bloomMipCount": ("number", 5), "r_motionBlurSamples": ("number", 8), "ui_retainedScale": ("number", 1),
    "ui_retainedTextScale": ("number", 1),
}
# The settings the page edits through settings.system.edit, each its input.
EDITS = ("com_performancePreset", "r_bloom", "r_borderless", "r_brightness", "r_crt", "r_customHeight", "r_customWidth", "r_forceAmbient",
         "r_fullscreen", "r_fullscreenDesktop", "r_hdrToneMap", "r_lightGridPreload", "r_multiSamples", "r_multiScreen", "r_postAA",
         "r_renderer", "r_screenFraction", "r_shadows", "r_skipBump", "r_skipSky", "r_skipSpecular", "r_ssao", "r_swapInterval",
         "r_useLightGrid", "r_windowHeight", "r_windowWidth", "ui_aspectCorrection", "com_machineSpec", "com_maxfps",
         "image_anisotropy", "image_downSize", "image_downSizeBump", "image_downSizeBumpLimit", "image_downSizeLimit",
         "image_downSizeSpecular", "image_downSizeSpecularLimit", "image_ignoreHighQuality", "image_usePrecompressedTextures",
         "image_writeGeneratedImages", "r_motionBlur", "r_rendererBenchmarkPreset", "r_rendererUploadFrameBuffers",
         "r_rendererUploadMegs", "r_shadowMapMaxUpdatesPerView", "r_shadowMapSize", "r_useShadowMap", "s_maxEmitterChannels",
         "s_maxSoundsPerShader", "s_numberOfSpeakers", "s_useEAXReverb", "ui_retainedScale", "ui_retainedTextScale")
RENDER_SLOTS = 32                         # the renderer device list (RenderingSettings)
PROFILES = ("minimum", "lowpower", "low", "performance", "balanced", "enhanced", "quality", "ultra", "maximum")   # PerformancePreset.cpp
# The service's values as presentation variables: each its name and state.
PRESENTED = (("open", "settings.open"), ("dirty", "settings.dirty"), ("busy", "settings.busy"), ("canApply", "settings.canApply"),
             ("phase", "settings.phase"), ("message", "settings.message"), ("request", "settings.request"),
             ("canConfirm", "settings.canConfirm"), ("canRevert", "settings.canRevert"), ("canRetry", "settings.canRetry"),
             ("remaining", "settings.remaining"), ("confirmationVisible", "settings.confirmationVisible"),
             ("discardVisible", "page.discardVisible"),
             *[(f"{side}{name}", f"settings.{side}.{key}") for name, key in (
                 ("Brightness", "r_brightness"), ("Shadows", "r_shadows"), ("PostAA", "r_postAA"), ("Bloom", "r_bloom"),
                 ("SSAO", "r_ssao"), ("Tonemap", "r_hdrToneMap"), ("CRT", "r_crt"), ("Irradiance", "r_useLightGrid"),
                 ("ResolutionScale", "r_screenFraction"), ("UIAspect", "ui_aspectCorrection"), ("VSync", "r_swapInterval"),
                 ("Preset", "com_performancePreset"), ("UIScale", "ui_retainedScale"), ("TextScale", "ui_retainedTextScale"),
                 ("Fullscreen", "r_fullscreen"), ("Borderless", "r_borderless"), ("FullscreenPolicy", "r_fullscreenDesktop"),
                 ("MSAA", "r_multiSamples")) for side in ("draft", "baseline")],
             ("msaaAvailable", "settings.msaaAvailable"),
             *[(f"{side}{name}", f"settings.{side}.{key}") for name, key in (
                 ("WindowWidth", "r_windowWidth"), ("WindowHeight", "r_windowHeight"), ("CustomWidth", "r_customWidth"),
                 ("CustomHeight", "r_customHeight"), ("Mode", "r_mode")) for side in ("draft", "baseline")],
             ("numberDraftsPending", "ui.numberDraftsPending"),
             *[(f"{side}{name}", f"settings.{side}.{key}") for name, key in (
                 ("LightGridPreload", "r_lightGridPreload"), ("Renderer", "r_renderer"), ("DisplayDevice", "r_screen"),
                 ("MultiScreen", "r_multiScreen"), ("Refresh", "r_displayRefresh")) for side in ("draft", "baseline")])


def st(key: str) -> dict:
    return {"state": key}


def op(name: str, *args) -> dict:
    return {"op": name, "args": list(args)}


def both(*terms) -> dict:
    """Terms joined by && from the left, as the service's conditions read."""
    result = terms[0]
    for term in terms[1:]:
        result = op("&&", result, term)
    return result


def no(term) -> dict:
    return op("!", term)


EDITING = op("&&", st("settings.open"), op("==", st("settings.phase"), 1))
IDLE = op("&&", no(st("settings.busy")), no(st("settings.confirmationVisible")))
# Editable while the draft is open and nothing is under way: `LIVE` while
# no dialog shows either, `QUIET` while no number draft is pending as well.
EDITABLE = both(EDITING, no(st("settings.busy")))
LIVE = both(EDITABLE, no(st("settings.confirmationVisible")), no(st("page.discardVisible")))
QUIET = both(LIVE, no(st("ui.numberDraftsPending")))
CAN_APPLY = op("&&", st("settings.canApply"), no(st("ui.numberDraftsPending")))
DISCARD_OPEN = op("&&", st("page.discardVisible"), IDLE)
# Discard closes an open draft, or the page itself with number drafts
# pending after the draft has closed.
DISCARD_LIVE = op("&&", DISCARD_OPEN, op("||", EDITING, both(no(st("settings.open")), op("==", st("settings.phase"), 0),
                                                             st("ui.numberDraftsPending"))))
APPLY_EXIT = both(DISCARD_OPEN, EDITING, st("settings.dirty"), CAN_APPLY)
SPAN_ALLOWED = op("&&", st("settings.display.spanAvailable"), op("||", op("&&", st("settings.draft.r_fullscreen"),
                                                                         st("settings.draft.r_fullscreenDesktop")),
                                                               op("&&", no(st("settings.draft.r_fullscreen")),
                                                                  st("settings.draft.r_borderless"))))
EXCLUSIVE_LISTS = op("&&", st("settings.display.available"), no(st("settings.draft.r_fullscreenDesktop")))


def dim(enabled, low: float = 0.4) -> dict:
    return op("select", enabled, 1, low)


def page_state() -> dict:
    """Every state the page declares: the service's settings.* schema
    (UI_SettingsStateSchema), the adapter's number drafts and the page's own."""
    state = {}
    for key, (kind, initial) in CATALOG.items():
        state[f"settings.draft.{key}"] = {"type": kind, "initial": initial}
        state[f"settings.baseline.{key}"] = {"type": kind, "initial": initial}
    for key in ("open", "dirty", "busy", "canApply", "canConfirm", "canRevert", "canRetry", "confirmationVisible"):
        state[f"settings.{key}"] = {"type": "boolean", "initial": False}
    state.update({
        "settings.phase": {"type": "number", "initial": 0}, "settings.remaining": {"type": "number", "initial": 0},
        "settings.request": {"type": "string", "initial": ""}, "settings.message": {"type": "string", "initial": "#str_230007"},
        "page.discardVisible": {"type": "boolean", "initial": False},
        "ui.numberDraftsPending": {"type": "boolean", "initial": False},
        "ui.numberDraftMessage": {"type": "string", "initial": "#str_230007"},
        "settings.msaaAvailable": {"type": "boolean", "initial": False},
        "soft_focus": {"type": "boolean", "initial": False, "cvar": "ui_retainedSoftFocus"},
    })
    for key in ("committed", "mapLoaded", "consumed", "effective", "pending"):
        state[f"settings.lightGrid.{key}"] = {"type": "boolean", "initial": False}
    for key in ("available", "fallback"):
        state[f"settings.renderer.{key}"] = {"type": "boolean", "initial": False}
    state.update(system_display_catalog.keys())
    render = {"catalog": "string", "count": "number", "selected": "number", "active": "string", "pending": "boolean",
              "autoDetect": "boolean", "canRestart": "boolean", "canChoose": "boolean", "vulkan": "boolean", "gl": "boolean",
              "gles": "boolean", "notice": "string", "backend": "string"}
    for index in range(RENDER_SLOTS):
        render[f"{index}.label"], render[f"{index}.usable"] = "string", "boolean"
    for key, kind in render.items():
        initial = {"string": "", "boolean": False, "number": -1 if key == "selected" else 0}[kind]
        state[f"settings.render.{key}"] = {"type": kind, "initial": initial}
    return state


def page_actions(doc: Document) -> None:
    for key in EDITS:
        kind = CATALOG[key][0]
        doc.actions[f"edit.{key}"] = {"input": kind, "operation": "settings.system.edit", "arguments": {key: {"input": "value"}}}
    request = {"request": st("settings.request")}
    doc.actions.update({
        "begin": {"operation": "settings.system.begin", "arguments": {}},
        "apply": {"operation": "settings.system.apply", "arguments": {}},
        "cancel": {"operation": "settings.system.cancel", "arguments": {}},
        "defaults": {"operation": "settings.system.defaults", "arguments": {}},
        "discard": {"operation": "settings.system.revert", "arguments": {}},
        "keep": {"operation": "settings.system.confirm", "arguments": dict(request)},
        "revert": {"operation": "settings.system.revert", "arguments": dict(request)},
        "retry": {"operation": "settings.system.retry", "arguments": dict(request)},
        "dismiss": {"operation": "ui.dismiss", "arguments": {}},
        # The Enable rows show the inverse of the stock skip settings.
        **{f"edit.{key}-enabled": {"input": "boolean", "operation": "settings.system.edit",
                                   "arguments": {key: no({"input": "value"})}} for key in ("r_skipSpecular", "r_skipBump", "r_skipSky")},
        "edit.ambient-enabled": {"input": "boolean", "operation": "settings.system.edit",
                                 "arguments": {"r_forceAmbient": op("select", {"input": "value"}, 0.7, 0)}},
        "applyExit": {"operation": "settings.system.applyExit", "arguments": {}},
        "focusNumberDraft": {"operation": "ui.numberDrafts.focus", "arguments": {}},
        "preset": {"input": "string", "operation": "settings.system.preset", "arguments": {"name": {"input": "value"}}},
        "autoDetect": {"operation": "settings.system.autodetect", "arguments": {}},
        "resetSizes": {"operation": "settings.system.edit", "arguments": {"ui_retainedScale": 1, "ui_retainedTextScale": 1}},
        # A custom size selects the custom resolution (r_mode -1), as the
        # stock entry did.
        **{f"size.{key}": {"input": "number", "operation": "settings.system.edit", "arguments": {key: {"input": "value"}, "r_mode": -1}}
           for key in ("r_customWidth", "r_customHeight")},
        **{f"display.{name}": {"input": "number", "operation": f"settings.system.{operation}",
                               "arguments": {"index": {"input": "value"}, "catalog": st("settings.display.catalog")}}
           for name, operation in (("select", "display"), ("mode", "displayMode"), ("refresh", "displayRefresh"))},
        "render.backend": {"input": "string", "operation": "settings.system.backend", "arguments": {"api": {"input": "value"}}},
        "render.device": {"input": "number", "operation": "settings.system.renderDevice",
                          "arguments": {"index": {"input": "value"}, "catalog": st("settings.render.catalog")}},
        "restartRenderer": {"operation": "settings.system.restartRenderer", "arguments": {}},
    })


def page_events(doc: Document) -> None:
    """The draft's programs: Back asks before it drops a draft, the Menu
    button applies one, and each button acts only while it can."""
    when = lambda condition, *steps: [{"op": "if", "condition": condition, "then": list(steps)}]
    action = lambda name: {"op": "action", "action": name}
    call = lambda name: {"op": "call", "event": name}
    discard_hidden = {"op": "setState", "values": {"page.discardVisible": False}}
    doc.events.update({
        "onBack": [{"op": "if", "condition": st("page.discardVisible"), "then": [call("continueEditing")], "else": [
            {"op": "if", "condition": op("&&", no(st("settings.busy")), op("||", op("==", st("settings.phase"), 0),
                                                                          op("==", st("settings.phase"), 1))),
             "then": [{"op": "if", "condition": op("||", st("settings.dirty"), st("ui.numberDraftsPending")),
                       "then": [{"op": "setState", "values": {"page.discardVisible": True}}], "else": [action("dismiss")]}]}]}],
        "onMenuButton": [{"op": "if", "condition": st("settings.confirmationVisible"), "then": [call("confirmationBack")], "else": [
            {"op": "if", "condition": st("page.discardVisible"), "then": [call("continueEditing")], "else": [
                {"op": "if", "condition": st("ui.numberDraftsPending"), "then": [action("focusNumberDraft")], "else": [
                    {"op": "if", "condition": st("settings.dirty"), "then": [call("apply")], "else": [call("onBack")]}]}]}]}],
        "discard": when(DISCARD_LIVE, action("cancel"), discard_hidden, action("dismiss")),
        "continueEditing": [discard_hidden, action("focusNumberDraft")],
        "applyExit": when(APPLY_EXIT, discard_hidden, action("applyExit")),
        "confirmationBack": when(st("settings.canRevert"), action("revert")),
        "apply": when(CAN_APPLY, action("apply")),
        "autoDetect": when(both(QUIET, st("settings.render.autoDetect")), action("autoDetect")),
        "resetSizes": when(QUIET, action("resetSizes")),
        "restartRenderer": when(both(QUIET, st("settings.render.canRestart")), action("restartRenderer")),
    })


def gate(doc: Document, ident: str, enabled: dict, low: float | None = 0.4, opacity: dict | None = None) -> None:
    doc.bind(f"{ident}.enabled", ident, "enabled", enabled)
    if opacity is not None:
        doc.bind(f"{ident}.opacity", ident, "opacity", opacity)
    elif low is not None:
        doc.bind(f"{ident}.opacity", ident, "opacity", dim(enabled, low))


def row(doc: Document, ident: str, label_key: str, kind: str, value_x: float, width: float | None = None, **more) -> dict:
    """A section 7 setting row on the stock pitch whose list unfolds within
    the pages."""
    return value_row(doc, ident, label_key, kind, U * (width or COLUMN_W), value_x, row_h=U * ROW_PITCH, placement=BODY, wrap=True,
                     **more)


def choice(doc: Document, ident: str, label_key: str, value_x: float, action: str, value: dict, entries: list, **more) -> dict:
    return row(doc, ident, label_key, "choice", value_x, value=value, action=action, entries=entries, **more)


def device_list(row_node: dict, entries: list) -> dict:
    """A list of device names, which run long ("NVIDIA GeForce RTX 4060
    Laptop GPU"): the closed value takes two lines, and the list unfolds
    across the whole row."""
    ident, row_h = row_node["id"], U * ROW_PITCH
    children = row_node["children"]
    value = next(child for child in children if child["id"] == f"{ident}-value")
    box = {key: value["properties"][key] for key in ("position", "left", "top", "width", "height")}
    value["properties"] = {"text": value["properties"]["text"], "position": keyword("relative"), "display": keyword("block"),
                           "width": value["properties"]["width"],
                           "max-height": length(row_h - 2), **typeface("lowpixel", 14, 16, rgb(VALUE)), "white-space": keyword("normal"),
                           "word-break": keyword("break-word"), "overflow": keyword("hidden")}
    children[children.index(value)] = group(f"{ident}-value-box", {**box, "display": keyword("flex"), "align-items": keyword("center"),
                                                                   "pointer-events": keyword("none")}, [value])
    popup, options = choice_popup(ident, entries, U * COLUMN_W - 16)
    popup["properties"]["left"] = length(8)
    popup["properties"]["top"] = length(row_h)
    old = next(child for child in children if child["id"] == f"{ident}-popup")
    children[children.index(old)] = popup
    row_node["control"]["options"] = options
    return row_node


def listed(key: str, values: tuple, first: int = 0) -> list:
    """Options labelled from one stock list, `first` its index for the first."""
    return [((key, first + index), value) for index, value in enumerate(values)]


def note(ident: str, key: str, lines: int = 2, tint: list | None = None) -> dict:
    """Up to `lines` lines of text among a column's rows, under the label
    column, two to a row's slot: a row's status or help, or a hint."""
    face, size, base = NOTE_FACE
    height = U * ROW_PITCH * math.ceil(lines * NOTE_LINE / (U * ROW_PITCH))
    return label(ident, key, {"position": keyword("relative"), "display": keyword("block"), "width": length(NOTE_W),
                              "height": length(height), "margin-left": length(21), "padding-top": length(2), "flex-shrink": number(0),
                              **typeface(face, size, NOTE_LINE, tint or base), "white-space": keyword("normal"),
                              "overflow": keyword("hidden"), "pointer-events": keyword("none")})


def number_field(doc: Document, ident: str, label_key: str, action: str, value: dict, minimum: float, maximum: float, *,
                 left: float, width: float, row_h: float, integer: bool, decimal_places: bool,
                 states: dict | None = None) -> dict:
    """A typed number (the adapter's number draft): its field in the row's
    value column, Enter committing the text to `action`. The adapter shows
    the validation under the row while the text cannot be taken."""
    field_h = 22.0
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    ink = lambda name, tint, w, h: vector(name, {**absolute(left=0, top=0, width=w, height=h), "display": keyword("block"),
                                                 "opacity": number(1), "pointer-events": keyword("none")},
                                          [path("ink", whole, fill=solid(tint))])
    parts = {part: f"{ident}-{part}" for part in ("viewport", "text", "selection", "caret", "composition", "validation")}
    plate = vector(f"{ident}-field", {**absolute(left=left, top=(row_h - field_h) / 2, width=width, height=field_h),
                                      "pointer-events": keyword("none")},
                   [path("field", whole, fill=solid([0, 0, 0, 0.6]), stroke=stroke(solid(rgb(OLIVE, 0.45)), 1))])
    focus = vector(f"{ident}-field-focus", {**absolute(left=left, top=(row_h - field_h) / 2, width=width, height=field_h),
                                            "opacity": number(0), "pointer-events": keyword("none")},
                   [path("focus", whole, stroke=stroke(solid(rgb(ORANGE)), 1.5))])
    text = label(parts["text"], PLACEHOLDER, {**absolute(left=6, top=0, width=4096, height=field_h),
                 **typeface("lowpixel", 15, field_h, rgb(VALUE)), "white-space": keyword("pre"), "text-align": keyword("left")})
    viewport = group(parts["viewport"], {**absolute(left=left, top=(row_h - field_h) / 2, width=width, height=field_h),
                                         "display": keyword("block"), "overflow": keyword("hidden")},
                     [ink(parts["selection"], rgb(ORANGE, 0.4), 0, field_h), text,
                      ink(parts["caret"], [1, 1, 1, 1], 1, field_h), ink(parts["composition"], rgb(ORANGE), 0, 1)])
    validation = label(parts["validation"], PLACEHOLDER, {**absolute(left=21, top=row_h + 1, width=U * COLUMN_W - 33, height=16),
                       **typeface("lowpixel", 13, 16, ERROR_TINT), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                       "display": keyword("none"), "pointer-events": keyword("none")})
    if states is None:
        states = doc.states(ident, {state: [(f"{ident}-field-focus", "opacity", number(level))] for state, level in (
            ("default", 0), ("hover", 0.55), ("focus", 1), ("pressed", 1), ("disabled", 0))})
    control = {"role": "number", "label": label_key, "action": action, "value": value, "minimum": minimum, "maximum": maximum,
               "exponent": decimal_places, "maxBytes": 128 if decimal_places else 32, "parts": parts, "states": states}
    if integer:
        control["integer"] = True
    return {"plate": plate, "focus": focus, "viewport": viewport, "validation": validation, "control": control}


def size_row(doc: Document, ident: str, label_key: str, value_x: float, action: str, setting: str, minimum: int) -> dict:
    """A window or custom size: a whole number of pixels, typed, the row
    itself the field, its rail and wash answering the field's focus."""
    row_h = U * ROW_PITCH
    caption = label(f"{ident}-label", label_key, {"position": keyword("relative"), "display": keyword("block"), "flex-shrink": number(1),
                    "min-width": length(0), **row_face(True, row_h), "overflow": keyword("hidden")})
    head = group(f"{ident}-head", {**absolute(left=21, top=0, width=value_x - 29, height=row_h), "display": keyword("flex"),
                                   "flex-direction": keyword("row"), "align-items": keyword("center")}, [caption])
    field = number_field(doc, ident, label_key, action, {"state": f"settings.draft.{setting}"}, minimum, 16384, left=value_x - 8,
                         width=90, row_h=row_h, integer=True, decimal_places=False, states=row_states(doc, ident))
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(U * COLUMN_W),
                         "height": length(row_h), "opacity": number(1), "flex-shrink": number(0)},
                 [*row_chrome(ident, row_h), head, field["plate"], field["viewport"], field["validation"]], control=field["control"])


def slider_pair(doc: Document, ident: str, label_key: str, value_x: float, setting: str, minimum: float, maximum: float, step: float,
                decimals: int) -> dict:
    """A slider with its typed number after it in one row: the slider moves
    in steps, the number takes any value in range, both into one draft."""
    row_h, value = U * ROW_PITCH, {"state": f"settings.draft.{setting}"}
    action = f"edit.{setting}"
    slider_w = U * COLUMN_W - NUMBER_W - 6
    slider = value_row(doc, ident, label_key, "slider", slider_w, value_x, value=value, action=action,
                       slider=(minimum, maximum, step, decimals), row_h=row_h, placement=BODY, wrap=True)
    slider["properties"].update(absolute(left=0, top=0))
    for child in slider["children"]:
        if child["id"] == f"{ident}-value":
            child["properties"]["display"] = keyword("none")
    # The track takes the readout's room: the number field shows the value.
    track_node = next(child for child in slider["children"] if child["id"] == f"{ident}-track")
    track_node["properties"]["width"] = length(track_node["properties"]["width"]["value"] + 48)
    paired = f"{ident}_number"
    field = number_field(doc, paired, label_key, action, value, minimum, maximum, left=0, width=NUMBER_W, row_h=row_h, integer=False,
                         decimal_places=True)
    number_control = group(paired, {**absolute(left=0, top=0, width=NUMBER_W, height=row_h), "opacity": number(1)},
                           [field["plate"], field["focus"], field["viewport"], field["validation"]], control=field["control"])
    field["validation"]["properties"]["left"] = length(-(slider_w + 6) + 21)
    controls = group(f"{ident}_row-controls", absolute(left=slider_w + 6, top=0, width=NUMBER_W, height=row_h), [number_control])
    return group(f"{ident}_row", {"position": keyword("relative"), "display": keyword("block"), "width": length(U * COLUMN_W),
                                  "height": length(row_h), "flex-shrink": number(0)}, [slider, controls])


def dialog_button(doc: Document, ident: str, key: str, width: float = 8, **command) -> dict:
    """A dialog's action: the stock popup's cut plate with an orange focus
    outline, `width` em wide where the row has room, growing with its label."""
    cut = [(6, 0), ({"fraction": 1, "dp": -6}, 0), ({"fraction": 1}, 6), ({"fraction": 1}, {"fraction": 1, "dp": -6}),
           ({"fraction": 1, "dp": -6}, {"fraction": 1}), (0, {"fraction": 1}), (0, 6)]
    plate = vector(f"{ident}-plate", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                      "pointer-events": keyword("none")},
                   [path("field", cut, fill=solid([0.035, 0.047, 0.031, 0.96]), stroke=stroke(solid(rgb(OLIVE, 0.6)), 1))])
    focus = vector(f"{ident}-focus", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                      "opacity": number(0.12), "pointer-events": keyword("none")},
                   [path("focus", cut, stroke=stroke(solid(rgb(ORANGE)), 2))])
    caption = label(f"{ident}-label", key, {"position": keyword("relative"), "display": keyword("block"),
                                            **typeface("marine", 16, 23, [1, 1, 1, 0.88]), "white-space": keyword("normal"),
                                            "text-align": keyword("center"), "word-break": keyword("normal")})
    states = doc.states(ident, {state: [(f"{ident}-focus", "opacity", number(level))] for state, level in (
        ("default", 0.12), ("hover", 0.55), ("focus", 1), ("pressed", 0.8), ("disabled", 0.025))})
    return group(ident, {"position": keyword("relative"), "display": keyword("flex"), "flex-direction": keyword("column"),
                         "justify-content": keyword("center"), "box-sizing": keyword("border-box"), "opacity": number(1),
                         "min-width": length(0),
                         "min-height": length(44), "flex-grow": number(1), "padding": length(10), "width": length(width, "em"),
                         "max-width": length(100, "%")}, [plate, focus, caption],
                 control={"role": "button", "label": key, **command, "states": states})


def body_scrollbar(doc: Document, ident: str, viewport: str, label_key: str) -> dict:
    """A dialog body's scrollbar, for text sizes that outgrow the dialog."""
    trough = [(16, 0), (22, 0), (22, {"fraction": 1}), (14, {"fraction": 1}), (14, 2)]
    thumb = [(14, 0), (26, 0), (26, {"fraction": 1, "dp": -4}), (22, {"fraction": 1}), (10, {"fraction": 1}), (10, 4)]
    whole = {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"), "pointer-events": keyword("none")}
    states = doc.states(ident, {state: [(f"{ident}-thumb-active", "opacity", number(level))] for state, level in (
        ("default", 0), ("hover", 0.55), ("focus", 0.85), ("pressed", 1), ("disabled", 0))})
    return group(ident, {"position": keyword("relative"), "display": keyword("block"), "width": length(36), "min-width": length(36),
                         "flex-grow": number(0), "flex-shrink": number(0), "opacity": number(1)}, [
        group(f"{ident}-track", {"position": keyword("relative"), "display": keyword("block"), "width": length(36),
                                 "height": length(100, "%"), "opacity": number(1)}, [
            vector(f"{ident}-trough", {**whole, "opacity": number(1)}, [
                path("outline", trough, fill=solid([0.0353, 0.0471, 0.0314, 0.95]), stroke=stroke(solid(rgb(OLIVE, 0.4)), 1))]),
            group(f"{ident}-thumb", {**absolute(left=0, top=0, width=36, height=36), "opacity": number(1)}, [
                vector(f"{ident}-thumb-base", {**whole, "opacity": number(1)}, [
                    path("outline", thumb, fill=solid([0.32, 0.36, 0.16, 0.95]), stroke=stroke(solid(rgb(OLIVE)), 1))]),
                vector(f"{ident}-thumb-active", {**whole, "opacity": number(0)}, [
                    path("outline", thumb, fill=solid(rgb(ORANGE)), stroke=stroke(solid(rgb(ORANGE)), 1))]),
            ]),
        ]),
    ], control={"role": "scrollbar", "label": label_key, "viewport": viewport, "orientation": "vertical", "lineStep": 36,
                "minimumThumb": 36, "parts": {"track": f"{ident}-track", "thumb": f"{ident}-thumb"}, "states": states})


def dialog(doc: Document, prefix: str, title_key: str, texts: list, buttons: list, shown: dict, focus: str, back: str) -> dict:
    """A section 6 dialog over the page while `shown`: the soft focus (or,
    with soft focus off, the 0.6 scrim) behind a cut plate 35 em wide at
    most, its title and text in a body that scrolls when a large text size
    outgrows the screen, and its actions in a row that wraps. It holds the
    input while it shows: `focus` takes the focus and Back runs `back`."""
    whole = {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"), "pointer-events": keyword("none")}
    cut = [(12, 0), ({"fraction": 1, "dp": -12}, 0), ({"fraction": 1}, 12), ({"fraction": 1}, {"fraction": 1, "dp": -12}),
           ({"fraction": 1, "dp": -12}, {"fraction": 1}), (0, {"fraction": 1}), (0, 12)]
    title = label(f"{prefix}-panel-title", title_key, {"position": keyword("relative"), "display": keyword("block"),
                  **typeface("marine", 22, 28, [1, 1, 1, 0.88]), "white-space": keyword("normal"), "margin-bottom": length(12)})
    body = group(f"{prefix}-body", {"position": keyword("relative"), "display": keyword("block"), "width": length(0),
                                    "min-width": length(0), "min-height": length(0), "flex-grow": number(1),
                                    "overflow": keyword("auto")}, [title, *texts])
    region = group(f"{prefix}-scroll-region", {"position": keyword("relative"), "display": keyword("flex"), "width": length(100, "%"),
                                               "min-height": length(0), "flex-shrink": number(1), "overflow": keyword("hidden"),
                                               "column-gap": length(8), "align-items": keyword("stretch"), "margin-bottom": length(12)},
                   [body, body_scrollbar(doc, f"{prefix}_body_scrollbar", f"{prefix}-body", title_key)])
    actions = group(f"{prefix}-actions", {"position": keyword("relative"), "display": keyword("flex"), "opacity": number(1),
                                          "flex-wrap": keyword("wrap"), "row-gap": length(8), "column-gap": length(8),
                                          "flex-shrink": number(0)}, buttons)
    # Border-box sizing keeps the padded plate (and panel) within 100 %.
    plate = vector(f"{prefix}-panel-plate", {"position": keyword("relative"), "display": keyword("flex"), "opacity": number(1),
                                             "box-sizing": keyword("border-box"),
                                             "width": length(DIALOG_W, "em"), "max-width": length(100, "%"), "padding": length(16),
                                             "flex-direction": keyword("column"), "max-height": length(100, "%")},
                   # The pop-up's opaque black frame (user rule, 5 November
                   # 2026), beneath the plate: the fill covers its inner half, so
                   # 2 dp of black shows outside the olive outline.
                   [path("rim", cut, stroke=stroke(solid([0, 0, 0, 1]), 4)),
                    path("dialog", cut, fill=solid([0.035, 0.047, 0.031, 1]), stroke=stroke(solid(rgb(OLIVE, 0.7)), 1))],
                   [region, actions])
    soft = group(f"{prefix}-panel-softfocus", {**whole, "display": keyword("none"), "backdrop-blur": length(7.5),
                                               "backdrop-saturate": number(0.8)})
    scrim = group(f"{prefix}-panel-scrim", {**whole, "display": keyword("block"), "background-color": colour([0, 0, 0, 0.6])})
    for node, on in ((f"{prefix}-panel-softfocus", "block"), (f"{prefix}-panel-scrim", "none")):
        doc.bind(f"{node}.display", node, "display", op("select", st("soft_focus"), on, "block" if on == "none" else "none"))
    doc.bind(f"{prefix}-panel.display", f"{prefix}-panel", "display", op("select", shown, "flex", "none"))
    return group(f"{prefix}-panel", {**absolute(left=0, top=0), "width": length(100, "%"), "height": length(100, "%"),
                                     "display": keyword("none"), "box-sizing": keyword("border-box"), "padding": length(16), "align-items": keyword("center"),
                                     "justify-content": keyword("center")}, [soft, scrim, plate],
                 modal={"initialFocus": focus, "back": back})


def dialogs(doc: Document) -> list:
    """Keep or Revert after a display change, and the unapplied draft's
    question on the way out."""
    keep = dialog_button(doc, "settings_keep", "#str_229994", action="keep")
    revert = dialog_button(doc, "settings_revert", "#str_229995", action="revert")
    retry = dialog_button(doc, "settings_retry", "#str_229996", action="retry")
    for ident, state in (("settings_keep", "settings.canConfirm"), ("settings_revert", "settings.canRevert"),
                         ("settings_retry", "settings.canRetry")):
        gate(doc, ident, st(state))
    for ident, state in (("settings_keep", "settings.canConfirm"), ("settings_retry", "settings.canRetry")):
        doc.bind(f"{ident}.display", ident, "display", op("select", st(state), "flex", "none"))
    text = lambda ident, key, size, line, tint, gap, more=None: label(ident, key, {
        "position": keyword("relative"), "display": keyword("block"), **typeface("marine", size, line, tint),
        "white-space": keyword("normal"), "margin-bottom": length(gap), **(more or {})})
    message = text("confirmation-message", "#str_230013", 16, 23, [1, 1, 1, 0.88], 8, {"min-height": length(48)})
    countdown = text("confirmation-countdown", "#str_230007", 28, 34, rgb(ORANGE), 0)
    doc.bind("confirmation-panel-title.text", "confirmation-panel-title", "text",
             op("select", op("==", st("settings.phase"), 3), "#str_230077", "#str_229993"))
    doc.bind("confirmation-message.text", "confirmation-message", "text", st("settings.message"))
    doc.bind("confirmation-countdown.text", "confirmation-countdown", "text",
             {"op": "numberText", "args": [op("ceil", st("settings.remaining"))], "decimals": 0})
    confirm = dialog(doc, "confirmation", "#str_229993", [message, countdown], [keep, revert, retry], st("settings.confirmationVisible"),
                     "settings_revert", "confirmationBack")

    apply_exit = dialog_button(doc, "discard_apply_changes", "#str_200241", 10, event="applyExit")
    keep_editing = dialog_button(doc, "discard_keep_editing", "#str_229999", 10, event="continueEditing")
    drop = dialog_button(doc, "discard_changes", "#str_41750", 10, event="discard")
    gate(doc, "discard_keep_editing", op("&&", DISCARD_OPEN, EDITING))
    gate(doc, "discard_changes", DISCARD_LIVE)
    gate(doc, "discard_apply_changes", APPLY_EXIT)
    discard = dialog(doc, "discard", "#str_230014", [], [apply_exit, keep_editing, drop], DISCARD_LIVE, "discard_keep_editing",
                     "continueEditing")
    return [confirm, discard]


def check_system_text(notes: list, apply_w: float, message_w: float) -> None:
    """Every shipped language's static notes fit their lines under the label
    column, and APPLY CHANGES its plate."""
    over = {}
    face, size, _tint = NOTE_FACE
    for key, lines in notes:
        for language, text in any_text(key).items():
            parts = text.replace("\\n", "\n").split("\n")
            for part in parts:
                if part.strip() and wrapped_width(part, size, lines - len(parts) + 1) > NOTE_W:
                    over[(key, language)] = round(wrapped_width(part, size, lines - len(parts) + 1))
    for language, text in any_text("#str_200241").items():
        if text_width("marine", text, APPLY_TEXT) > apply_w:
            over[("#str_200241", language)] = round(text_width("marine", text, APPLY_TEXT))
    if over:
        raise SystemExit(f"SYSTEM page text past its room: {over}")
    del message_w


def system_document() -> dict:
    doc = Document("openq4.system")
    doc.state.update(page_state())
    page_actions(doc)
    page_events(doc)
    labels = ("#str_229976", "#str_229978", "#str_230141", "#str_230142", "#str_230153", "#str_41091", "#str_230015", "#str_230016",
              "#str_229944", "#str_230017", "#str_229912", "#str_229915", "#str_200147", "#str_229909", "#str_229910", "#str_41093",
              "#str_41092", "#str_229946", "#str_229947", "#str_229975", "#str_229948", "#str_229949", "#str_229945", "#str_200148",
              "#str_200160", "#str_200161", "#str_200162", "#str_223009", "#str_41082", "#str_41088", "#str_41084", "#str_41094",
              "#str_223002", "#str_223003", "#str_41090", "#str_41107", "#str_42820", "#str_41103")
    x = value_column(labels)
    draft = lambda key: st(f"settings.draft.{key}")
    edit = lambda key: f"edit.{key}"

    # GRAPHICS: the preset and the renderer, and the interface's sizes.
    preset = choice(doc, "settings_preset", "#str_229976", x, "preset", draft("com_performancePreset"), listed("#str_229977", PROFILES))
    gate(doc, "settings_preset", QUIET, None)
    autodetect = button_row(doc, "settings_autodetect", "#str_229978", x, None, {"event": "autoDetect"})
    gate(doc, "settings_autodetect", both(QUIET, st("settings.render.autoDetect")))
    backend = choice(doc, "settings_backend", "#str_230141", x, "render.backend", st("settings.render.backend"),
                     [(("#str_230143", 0), "best", True), (("#str_230143", 1), "gl", st("settings.render.gl")),
                      (("#str_230143", 2), "vulkan", st("settings.render.vulkan")), (("#str_230143", 3), "gles", st("settings.render.gles"))])
    gate(doc, "settings_backend", QUIET)
    devices = [("#str_230144", -1)] + [(st(f"settings.render.{index}.label"), index, st(f"settings.render.{index}.usable"))
                                         for index in range(RENDER_SLOTS)] + [("#str_230154", RENDER_SLOTS, False)]
    gpu = device_list(choice(doc, "settings_gpu", "#str_230142", x, "render.device", st("settings.render.selected"), devices,
                             count=op("+", st("settings.render.count"), 1)), devices)
    gate(doc, "settings_gpu", both(QUIET, st("settings.render.canChoose")))
    restart = button_row(doc, "settings_restart_renderer", "#str_230153", x, None,
                         {"event": "restartRenderer"})
    gate(doc, "settings_restart_renderer", both(QUIET, st("settings.render.canRestart")))
    doc.bind("settings_restart_renderer.display", "settings_restart_renderer", "display",
             op("select", st("settings.render.pending"), "block", "none"))
    scale = choice(doc, "settings_resolution_scale", "#str_41091", x, edit("r_screenFraction"), draft("r_screenFraction"),
                   listed("#str_229998", (10, 25, 50, 75, 85, 100, 125, 150, 200)))
    gate(doc, "settings_resolution_scale", EDITABLE)
    active = note("rendering-active", "#str_230152")
    doc.bind("rendering-active.text", "rendering-active", "text", st("settings.render.active"))
    notice = note("rendering-notice", "#str_230145", 3)
    doc.bind("rendering-notice.text", "rendering-notice", "text", st("settings.render.notice"))
    ui_scale = choice(doc, "settings_ui_scale", "#str_230015", x, edit("ui_retainedScale"), draft("ui_retainedScale"),
                      [(("#str_229998", index), value) for index, value in ((3, 0.75), (5, 1), (6, 1.25), (7, 1.5), (8, 2))])
    gate(doc, "settings_ui_scale", QUIET, None)
    text_scale = choice(doc, "settings_text_scale", "#str_230016", x, edit("ui_retainedTextScale"), draft("ui_retainedTextScale"),
                        [(("#str_229998", index), value) for index, value in ((5, 1), (6, 1.25), (7, 1.5), (8, 2))])
    gate(doc, "settings_text_scale", QUIET, None)
    aspect = row(doc, "settings_ui_aspect", "#str_229944", "toggle", x, value=draft("ui_aspectCorrection"), action=edit("ui_aspectCorrection"))
    gate(doc, "settings_ui_aspect", EDITABLE)
    reset = button_row(doc, "settings_reset_sizes", "#str_230017", x, None, {"event": "resetSizes"})
    gate(doc, "settings_reset_sizes", QUIET, None)
    fit = note("interface-fit-note", "#str_230019")

    # SCREEN: the display and its sizes.
    displays = [(("#str_229914", 0), -1)] + [(st(f"settings.display.{index}.label"), index, op("<", index, st("settings.display.count")))
                                             for index in range(system_display_catalog.DEVICE_SLOTS)]
    device = device_list(choice(doc, "settings_display_device", "#str_229912", x, "display.select", draft("r_screen"), displays,
                                count=st("settings.display.optionCount")), displays)
    gate(doc, "settings_display_device", both(QUIET, st("settings.display.available")), None)
    doc.bind("settings_display_device.display", "settings_display_device", "display", op("select", op("&&", st("settings.display.available"), op(
        "||", op("||", op(">", st("settings.display.count"), 1), op(">=", draft("r_screen"), st("settings.display.count"))),
        op("!=", draft("r_screen"), st("settings.baseline.r_screen")))), "block", "none"))
    span = choice(doc, "settings_multiscreen", "#str_229915", x, edit("r_multiScreen"), draft("r_multiScreen"),
                  [(("#str_200059", 0), 0), (("#str_200059", 1), 1, SPAN_ALLOWED)])
    gate(doc, "settings_multiscreen", both(QUIET, st("settings.display.available")), None)
    span_shown = op("&&", st("settings.display.available"), op(
        "||", op("||", op(">", st("settings.display.count"), 1), op("!=", draft("r_multiScreen"), 0)),
        op("!=", draft("r_multiScreen"), st("settings.baseline.r_multiScreen"))))
    doc.bind("settings_multiscreen.display", "settings_multiscreen", "display", op("select", span_shown, "block", "none"))
    span_status = note("settings_multiscreen-status", "#str_230088")
    span_status["properties"]["display"] = keyword("none")
    doc.bind("settings_multiscreen-status.display", "settings_multiscreen-status", "display",
             op("select", op("&&", span_shown, no(SPAN_ALLOWED)), "block", "none"))
    doc.bind("settings_multiscreen-status.text", "settings_multiscreen-status", "text",
             op("select", st("settings.display.spanAvailable"), "#str_230088", "#str_230089"))
    fullscreen = row(doc, "settings_fullscreen", "#str_200147", "toggle", x, value=draft("r_fullscreen"), action=edit("r_fullscreen"))
    gate(doc, "settings_fullscreen", QUIET, None)
    borderless = row(doc, "settings_borderless", "#str_229909", "toggle", x, value=draft("r_borderless"), action=edit("r_borderless"))
    gate(doc, "settings_borderless", QUIET, None)
    policy = choice(doc, "settings_fullscreen_policy", "#str_229910", x, edit("r_fullscreenDesktop"), draft("r_fullscreenDesktop"),
                    [(("#str_229911", 0), True), (("#str_229911", 1), False)])
    gate(doc, "settings_fullscreen_policy", QUIET, None)
    msaa = choice(doc, "settings_msaa", "#str_41093", x, edit("r_multiSamples"), draft("r_multiSamples"),
                  listed("#str_230020", (0, 2, 4, 8, 16)))
    gate(doc, "settings_msaa", both(QUIET, st("settings.msaaAvailable")), opacity=dim(st("settings.msaaAvailable"), 0.45))
    vsync = choice(doc, "settings_vsync", "#str_41092", x, edit("r_swapInterval"), draft("r_swapInterval"), listed("#str_200059", (0, 1)))
    gate(doc, "settings_vsync", EDITABLE)
    windowed = no(draft("r_fullscreen"))
    exclusive = no(draft("r_fullscreenDesktop"))
    sizing_live = both(LIVE)
    width = size_row(doc, "settings_window_width", "#str_229946", x, edit("r_windowWidth"), "r_windowWidth", 320)
    height = size_row(doc, "settings_window_height", "#str_229947", x, edit("r_windowHeight"), "r_windowHeight", 240)
    for ident in ("settings_window_width", "settings_window_height"):
        gate(doc, ident, both(sizing_live, windowed), opacity=dim(windowed, 0.45))
    mode = choice(doc, "settings_display_mode", "#str_229975", x, "display.mode", st("settings.display.mode.selected"),
                  [(st(f"settings.display.mode.{index}.label"), index) for index in range(system_display_catalog.MODE_SLOTS)],
                  count=st("settings.display.mode.optionCount"))
    hint = note("dimensions-hint", "#str_230024", 3)
    custom_w = size_row(doc, "settings_custom_width", "#str_229948", x, "size.r_customWidth", "r_customWidth", 320)
    custom_h = size_row(doc, "settings_custom_height", "#str_229949", x, "size.r_customHeight", "r_customHeight", 240)
    for ident in ("settings_custom_width", "settings_custom_height"):
        gate(doc, ident, both(sizing_live, exclusive), opacity=dim(exclusive, 0.45))
    refresh = choice(doc, "settings_display_refresh", "#str_229945", x, "display.refresh", st("settings.display.refresh.selected"),
                     [(st(f"settings.display.refresh.{index}.label"), index) for index in range(system_display_catalog.REFRESH_SLOTS)],
                     count=st("settings.display.refresh.optionCount"))
    for ident in ("settings_display_mode", "settings_display_refresh"):
        gate(doc, ident, both(QUIET, EXCLUSIVE_LISTS), opacity=dim(EXCLUSIVE_LISTS, 0.45))

    # IMAGE QUALITY: the image's settings and the renderer's.
    brightness = slider_pair(doc, "settings_brightness", "#str_200148", x, "r_brightness", 0.5, 2, 0.1, 2)
    image_rows = [brightness]
    for ident, key, setting, action in (
            ("settings_shadows", "#str_200160", "r_shadows", None), ("settings_specular", "#str_200161", "r_skipSpecular", "-enabled"),
            ("settings_bump", "#str_200162", "r_skipBump", "-enabled"), ("settings_sky", "#str_223009", "r_skipSky", "-enabled"),
            ("settings_bloom", "#str_41082", "r_bloom", None), ("settings_ssao", "#str_41088", "r_ssao", None),
            ("settings_tonemap", "#str_41084", "r_hdrToneMap", None)):
        value = draft(setting) if action is None else no(draft(setting))
        image_rows.append(row(doc, ident, key, "toggle", x, value=value, action=edit(setting) + (action or "")))
    postaa = choice(doc, "settings_postaa", "#str_41094", x, edit("r_postAA"), draft("r_postAA"), listed("#str_41095", (0, 1, 2, 3, 4)))
    image_rows.append(postaa)
    ambient_on = row(doc, "settings_ambient_enabled", "#str_223002", "toggle", x, value=op(">", draft("r_forceAmbient"), 0),
                     action="edit.ambient-enabled")
    ambient = slider_pair(doc, "settings_ambient", "#str_223003", x, "r_forceAmbient", 0, 1, 0.025, 3)
    crt = row(doc, "settings_crt", "#str_41090", "toggle", x, value=draft("r_crt"), action=edit("r_crt"))
    irradiance = row(doc, "settings_irradiance", "#str_41107", "toggle", x, value=draft("r_useLightGrid"), action=edit("r_useLightGrid"))
    preload = row(doc, "settings_lightgrid_preload", "#str_42820", "toggle", x, value=draft("r_lightGridPreload"),
                  action=edit("r_lightGridPreload"))
    gate(doc, "settings_lightgrid_preload", QUIET)
    preload_help = note("settings_lightgrid_preload-help", "#str_42821", 3)
    preload_status = note("settings_lightgrid_preload-status", "#str_230081", tint=rgb(VALUE, 0.85))
    preload_status["properties"]["display"] = keyword("none")
    doc.bind("settings_lightgrid_preload-status.display", "settings_lightgrid_preload-status", "display",
             op("select", st("settings.lightGrid.mapLoaded"), "block", "none"))
    doc.bind("settings_lightgrid_preload-status.text", "settings_lightgrid_preload-status", "text",
             op("select", st("settings.lightGrid.pending"), "#str_230078",
                op("select", st("settings.lightGrid.consumed"), op("select", st("settings.lightGrid.effective"), "#str_230079", "#str_230080"),
                   "#str_230081")))
    renderer = choice(doc, "settings_renderer", "#str_41103", x, edit("r_renderer"), draft("r_renderer"), listed("#str_41104", ("best", "arb2")))
    gate(doc, "settings_renderer", both(QUIET, st("settings.renderer.available")),
         opacity=op("select", st("settings.renderer.available"), dim(QUIET), 0.45))
    renderer_status = note("settings_renderer-status", "#str_230082", tint=rgb(VALUE, 0.85))
    renderer_status["properties"]["display"] = keyword("none")
    doc.bind("settings_renderer-status.display", "settings_renderer-status", "display", op("select", op(
        "&&", st("settings.renderer.fallback"), op("==", draft("r_renderer"), st("settings.baseline.r_renderer"))), "block", "none"))
    for ident in ("settings_brightness", "settings_brightness_number", "settings_shadows", "settings_specular", "settings_bump",
                  "settings_sky", "settings_bloom", "settings_ssao", "settings_tonemap", "settings_postaa", "settings_ambient_enabled",
                  "settings_ambient", "settings_ambient_number", "settings_crt", "settings_irradiance"):
        gate(doc, ident, EDITABLE, None if ident.endswith("_number") else 0.4)

    tabs = [
        ("graphics", "#str_41098", [
            [heading("#str_41098"), preset, autodetect, backend, gpu, renderer, renderer_status, restart, active, notice],
            [heading("#str_230018"), ui_scale, text_scale, aspect, reset, fit]]),
        ("screen", "#str_41099", [
            [heading("#str_229900"), device, span, span_status, fullscreen, borderless, policy, msaa, vsync],
            [heading("#str_229943"), width, height, mode, hint, custom_w, custom_h, refresh]]),
        ("image", "#str_41101", [
            [heading("#str_41101"), *image_rows, scale],
            [heading("#str_41102"), ambient_on, ambient, crt, irradiance, preload, preload_help, preload_status]]),
    ]

    # APPLY CHANGES under the columns' trailing end, the service's message
    # (or the number drafts') before it.
    apply_w = max(text_width("marine", text, APPLY_TEXT) for text in any_text("#str_200241").values()) / U + 40
    apply_left = CONTENT_LEFT + CONTENT_W - apply_w
    apply = primary_plate(doc, "settings_apply", "#str_200241", 0, 0, apply_w, APPLY_H, event="apply",
                          label_box=(28.0, 4.0, apply_w - 32, 22.0), text=APPLY_TEXT, marker=(9.0, 8.0, 14.0))
    apply["properties"]["opacity"] = number(1)
    fixed_text(doc, apply)   # the label keeps its size on the plate
    gate(doc, "settings_apply", CAN_APPLY)
    message_w = U * (apply_left - CONTENT_LEFT - 12)
    # The message: one line centred on APPLY CHANGES' height, more lines (a
    # long status, a large text size) growing up into the room above.
    message = label("settings-message", "#str_230007", {**absolute(left=0, bottom=(U * APPLY_H - 18) / 2, width=message_w - 21),
                    "display": keyword("block"), **typeface("lowpixel", 14, 18, [1, 1, 1, 0.8]), "white-space": keyword("normal")})
    status = group("settings-status", {**absolute(left=21, top=0, width=message_w - 21, height=U * APPLY_H),
                                       "pointer-events": keyword("none")}, [message])
    doc.bind("settings-message.text", "settings-message", "text", op("select", op("&&", op("&&", st("ui.numberDraftsPending"), IDLE),
                                                                                 op("||", op("==", st("settings.phase"), 0),
                                                                                    op("==", st("settings.phase"), 1))),
                                                                      st("ui.numberDraftMessage"), st("settings.message")))
    footer = group("settings-footer", absolute(left=U * CONTENT_LEFT, top=U * FOOTER_TOP, width=U * CONTENT_W, height=U * APPLY_H), [
        status, group("settings-actions", absolute(left=U * (apply_left - CONTENT_LEFT), top=0, width=U * apply_w, height=U * APPLY_H),
                       [apply])])
    check_system_text([("#str_230019", 2), ("#str_230024", 3), ("#str_230088", 2), ("#str_230089", 2), ("#str_42821", 3),
                       ("#str_230078", 2), ("#str_230079", 2), ("#str_230080", 2), ("#str_230081", 2), ("#str_230082", 2),
                       ("#str_230145", 3)], U * apply_w, message_w)

    # The pages' bounds hold every list the rows unfold.
    def activate(focus: list) -> list:
        return [{"op": "setState", "values": {"page.discardVisible": False}}, {"op": "action", "action": "begin"}, *focus]
    # SCREEN's first rows show only with several displays.
    device_shown = next(binding["value"]["args"][0] for binding in doc.bindings if binding["id"] == "settings_display_device.display")
    focus = lambda control: {"op": "focus", "control": control}
    screen_first = [{"op": "if", "condition": device_shown, "then": [focus("settings_display_device")],
                     "else": [{"op": "if", "condition": span_shown, "then": [focus("settings_multiscreen")],
                               "else": [focus("settings_fullscreen")]}]}]
    built = settings_subpage(doc, PREFIX, "#str_229900", tabs, activate=activate, overlays=dialogs(doc), back_event="onBack",
                             back_id="settings_back", slots=SLOTS, footer=[footer], body=BODY, first_focus={"screen": screen_first})
    return finish(built)


def finish(document: dict) -> dict:
    """What the page adds to the sub-page: the dialogs, and a typed number
    not yet committed (which the Menu button and Keep editing return to),
    hold the tab still, and the dialogs take the pointer off the page; Back
    answers only while the service is idle; the presentation variables and
    the contract notes."""
    blocked = op("||", op("||", st("settings.confirmationVisible"), st("page.discardVisible")), st("ui.numberDraftsPending"))
    # Bound properties take explicit base values.
    def node(within: dict, ident: str):
        if within["id"] == ident:
            return within
        return next((found for child in within.get("children", []) if (found := node(child, ident))), None)
    node(document["root"], "settings-panel")["properties"]["pointer-events"] = keyword("auto")
    node(document["root"], "settings_back")["properties"]["opacity"] = number(1)
    for name, steps in list(document["events"].items()):
        if name in ("onTabPrevious", "onTabNext") or name.startswith("tab_"):
            document["events"][name] = [{"op": "if", "condition": no(blocked), "then": steps}]
    bindings = document["bindings"]
    tabs = [name[4:] for name in document["events"] if name.startswith("tab_")]
    for ident in tabs:
        control = f"tab-{ident}"
        found = next((binding for binding in bindings if binding["node"] == control and binding["property"] == "enabled"), None)
        if found:
            found["value"] = op("&&", no(blocked), found["value"])
        else:
            bindings.append({"id": f"{control}.enabled", "node": control, "property": "enabled", "value": no(blocked)})
    bindings.append({"id": "settings-panel.pointer-events", "node": "settings-panel", "property": "pointer-events",
                     "value": op("select", op("||", st("settings.confirmationVisible"), both(
                         both(st("page.discardVisible"), EDITABLE), no(st("settings.confirmationVisible")))), "none", "auto")})
    bindings.append({"id": "settings_back.enabled", "node": "settings_back", "property": "enabled", "value": no(st("settings.busy"))})
    bindings.append({"id": "settings_back.opacity", "node": "settings_back", "property": "opacity", "value": dim(no(st("settings.busy")))})
    document["presentationVariables"] = {name: {"type": document["state"][key]["type"], "initial": document["state"][key]["initial"],
                                                "value": st(key)} for name, key in PRESENTED}
    document["aliases"] = {name: {"variable": name} for name, _key in PRESENTED}
    document["extensions"] = {"openq4": EXTENSIONS}
    return document


EXTENSIONS = {
    "scope": "The SYSTEM settings page, generated by tools/ui/retained_system.py: a SETTINGS sub-page whose GRAPHICS, SCREEN and "
             "IMAGE QUALITY tabs edit the settings service's draft, applied with APPLY CHANGES and confirmed with Keep or Revert. "
             "Every stock setting works here, so ui_retained opens it; ui_retainedSystem opens it without ui_retained.",
    "sourceContract": "docs/dev/ui/system-settings-contract.md",
    "visualContract": "docs/dev/ui-visual-design.md",
    "numberFields": {"pairs": {"settings_brightness": "settings_brightness_number", "settings_ambient": "settings_ambient_number"},
                     "proposalPolicy": "Explicit typed commit to the same settings draft; no slider-step quantization.",
                     "scope": "Each slider's typed number shares its row; the adapter holds typed text as a number draft until "
                              "Enter commits it, and Apply waits for every draft."},
    "presets": {"source": "src/framework/PerformancePreset.cpp",
                "scope": "Source-backed preset and Auto-Detect draft editing. Apply runs the batch as one journaled attempt: one "
                         "device restart, a sound restart for a layout or EFX change, and the status notice #str_230076 while "
                         "the draft holds a change that finishes at the next start.",
                "localization": ["#str_229976", "#str_229977", "#str_229978"],
                "proposalPolicy": "Popup highlight is transient. Selected value follows authoritative draft readback before "
                                  "acknowledgement."},
    "interfaceSettings": {"scope": "UI and text sizes edit the SYSTEM draft and take effect through Apply. Reset sizes changes only "
                                   "these two draft values.",
                          "localization": ["#str_230015", "#str_230016", "#str_230017", "#str_230018", "#str_230019"],
                          "catalogVersion": 2},
    "displayControls": {"scope": "Display Device, Expand Across Displays, fullscreen, borderless, fullscreen policy and MSAA edit the "
                                 "draft. Apply uses owned Keep/Revert recovery; unsupported device requests do not bypass validation.",
                        "displayLists": "settings.display.* keys from system_display_catalog.py: owner-only lists the settings "
                                        "service builds from the display topology.",
                        "deviceRow": "Auto and the device list slots; hidden on one display unless the draft names a display the "
                                     "list lacks or holds a change not yet applied, and hidden without lists.",
                        "spanRow": "No or Yes over r_multiScreen; spanning needs the list to allow it and a borderless window or "
                                   "desktop fullscreen, and a line under the row says which is missing.",
                        "msaaAvailability": "Read-only renderer observation; an unavailable renderer disables MSAA.",
                        "localization": ["#str_229900", "#str_229912", "#str_229914", "#str_229915", "#str_200059", "#str_230088",
                                         "#str_230089", "#str_200147", "#str_229909", "#str_229910", "#str_229911", "#str_41093",
                                         "#str_230020"]},
    "dimensionControls": {"source": "Legacy custom-size entry selects r_mode=-1; new selection and dimension changes share one owned "
                                    "draft patch.",
                          "validation": "Exact decimal integers within renderer bounds; intermediate size tuples remain drafts and "
                                        "cannot Apply until the complete display request validates.",
                          "availability": "Window size is editable in a windowed draft. Custom fullscreen size, Display Resolution "
                                          "and Refresh Rate are editable with Exclusive policy.",
                          "lists": "Display Resolution and Refresh Rate pick a slot of the published mode and refresh lists with the "
                                   "list token (display.mode, display.refresh)."},
    "renderOptions": {"scope": "Preload Light Grids edits the draft; Apply completes it without Keep/Revert and the next map load "
                               "uses it. The status line names what the loaded map does with its light grids.",
                      "confirmationTitle": {"recoveryPhase": 3, "recovery": "#str_230077", "otherwise": "#str_229993"},
                      "preloadRow": {"control": "settings_lightgrid_preload", "setting": "r_lightGridPreload",
                                     "status": {"pending": "#str_230078", "preloads": "#str_230079", "streams": "#str_230080",
                                                "noGrids": "#str_230081"}},
                      "rendererRow": {"control": "settings_renderer", "setting": "r_renderer", "values": ["best", "arb2"],
                                      "fallbackNotice": "#str_230082"},
                      "localization": ["#str_229993", "#str_230077", "#str_230073", "#str_230074", "#str_230075", "#str_230076",
                                       "#str_42820", "#str_42821", "#str_230078", "#str_230079", "#str_230080", "#str_230081",
                                       "#str_41103", "#str_41104", "#str_230082"]},
}
