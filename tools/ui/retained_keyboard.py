"""The on-screen keyboard (docs/dev/roadmap.md PAD-05; the UI register's
WID-022 and INP-012): typing into a retained text or number field with only
a controller or a touch screen, where the system offers no keyboard of its
own.

It is a document of its own, drawn by the session over whichever retained
screen holds the field being edited (idSessionLocal::UpdateOnScreenKeyboard).
A field's edit lives only while the field keeps the focus in its own
document, so the keyboard cannot be controls beside it; here its keys are
ordinary buttons with their own focus, pointer and touch, and the session
types what they ask for into the field.

Four rows of eleven keys show the current layer's characters, which the
session publishes (osk.key0 to osk.key43, from src/framework/OnScreenKeyboard.h:
Latin, two layers of accented letters, Cyrillic and symbols). Under them:
SHIFT (lit while on), the layer key naming the next layer, SPACE, the caret's
two arrows, DELETE and DONE. A key's Accept types its character (oskKey with
osk.pick); the controller also has each function on a button, drawn on its
key while a controller is in use (section 13.5): west deletes, north adds a
space, the bumpers move the caret, the triggers are SHIFT and the layer, Menu
is DONE, and Back cancels the edit. The panel stands at the view's foot, or
at its head while the field is in the lower half (osk.top), so the field
stays clear of it."""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, ORANGE, Document, absolute, colour, device_glyphs, fit_narrow, font, group,
                                    keyword, label, length, number, path, rgb, solid, stroke, text_width, typeface, vector)
from retained_mp_menus import table_text
from retained_saves import PLACEHOLDER, bind_selected, row_plates, row_states

COLUMNS, ROWS = 11, 4
KEYS = COLUMNS * ROWS
KEY_W, KEY_H, GAP = 50.0, 38.0, 4.0          # dp
PAD = 12.0                                    # dp: the panel's inset
INNER_W = COLUMNS * KEY_W + (COLUMNS - 1) * GAP
PANEL_W = INNER_W + 2 * PAD
PANEL_H = (ROWS + 1) * KEY_H + ROWS * GAP + 2 * PAD
MARGIN = 20.0                                 # dp: from the view's foot or head
CUT = 12.0                                    # dp: the panel's 45-degree corners
FIRST_FOCUS = 2 * COLUMNS + 5                 # the third row's middle key
KEY_TINT = [1, 1, 1, 0.92]
SPACE, DONE, SHIFT, DELETE = "#str_232771", "#str_232772", "#str_232773", "#str_232774"
# The function row: (id, session verb, width in dp, label key or None, the
# controller button that does the same, the document event that button raises).
FUNCTIONS = (
    ("shift", "oskShift", 86.0, SHIFT, "left-trigger", "onSectionPrevious"),
    ("layer", "oskLayer", 70.0, None, "right-trigger", "onSectionNext"),
    ("space", "oskSpace", 120.0, SPACE, "north", "onTertiary"),
    ("left", "oskLeft", 50.0, None, "left", "onTabPrevious"),
    ("right", "oskRight", 50.0, None, "right", "onTabNext"),
    ("delete", "oskBackspace", 100.0, DELETE, "west", "onSecondary"),
    ("done", "oskDone", 90.0, DONE, "menu", "onMenuButton"),
)
assert abs(sum(width for _i, _v, width, _k, _g, _e in FUNCTIONS) + (len(FUNCTIONS) - 1) * GAP - INNER_W) < 0.01
HINT = 13.0                                   # dp: a function key's controller button


def key_label(ident: str, key: str, width: float, size: float, left: float = 0.0) -> dict:
    return label(f"{ident}-label", key, {**absolute(left=left, top=0, width=width - left, height=KEY_H),
                 **typeface("lowpixel", size, KEY_H, KEY_TINT), "text-align": keyword("center"), "white-space": keyword("pre"),
                 "overflow": keyword("hidden"), "pointer-events": keyword("none")})


def key_node(doc: Document, ident: str, left: float, top: float, width: float, parts: list, control: dict, worded: bool = True) -> dict:
    """A key: a list row's lights under its parts; a worded key's label takes the focus colour."""
    labels, tints = ((f"{ident}-label",), (KEY_TINT,)) if worded else ((), ())
    return group(ident, {**absolute(left=left, top=top, width=width, height=KEY_H), "opacity": number(1)},
                 [*row_plates(ident, width, KEY_H, selectable=True), *parts],
                 control={"role": "button", **control, "states": row_states(doc, ident, labels, tints)})


def arrow(ident: str, width: float, forward: bool) -> dict:
    """The caret's arrow: a chevron in the key's middle."""
    mid_x, mid_y, reach = width / 2, KEY_H / 2, 6.0
    tip = mid_x + (reach / 2 if forward else -reach / 2)
    tail = mid_x - (reach / 2 if forward else -reach / 2)
    return vector(f"{ident}-arrow", {**absolute(left=0, top=0, width=width, height=KEY_H), "pointer-events": keyword("none")},
                  [path("chevron", [(tail, mid_y - reach), (tip, mid_y), (tail, mid_y + reach)], closed=False,
                        stroke=stroke(solid(KEY_TINT), 2, minimum=1))])


def check_labels() -> None:
    """Every language's word fits its function key at the label's size."""
    for ident, _verb, width, key, _glyph, _event in FUNCTIONS:
        if key is None:
            continue
        for language, text in table_text(key).items():
            needed = text_width("lowpixel", text, 14) + HINT + 12
            if needed > width:
                raise SystemExit(f"keyboard: {language} {key} {text!r} needs {needed:.1f} dp of the {ident} key's {width:.0f}")


def keyboard_document() -> dict:
    check_labels()
    doc = Document("openq4.keyboard")
    doc.state.update({"osk.pick": {"type": "number", "initial": -1}, "osk.shift": {"type": "boolean", "initial": False},
                      "osk.top": {"type": "boolean", "initial": False}, "osk.layerName": {"type": "string", "initial": ""}})
    doc.session("oskKey", "oskKey")
    doc.session("oskCancel", "oskCancel")
    keys = []
    for index in range(KEYS):
        ident, state = f"osk-key-{index}", f"osk.key{index}"
        doc.state[state] = {"type": "string", "initial": ""}
        doc.events[f"oskKey{index}"] = [{"op": "setState", "values": {"osk.pick": index}}, {"op": "action", "action": "oskKey"}]
        row, column = divmod(index, COLUMNS)
        caption = key_label(ident, PLACEHOLDER, KEY_W, 20)
        doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": state})
        keys.append(key_node(doc, ident, PAD + column * (KEY_W + GAP), PAD + row * (KEY_H + GAP), KEY_W, [caption],
                             {"label": SPACE, "event": f"oskKey{index}"}))
    left, top = PAD, PAD + ROWS * (KEY_H + GAP)
    for name, verb, width, key, glyph, shortcut in FUNCTIONS:
        ident = f"osk-{name}"
        doc.session(verb, verb)
        doc.events[f"{verb}Pressed"] = [{"op": "action", "action": verb}]
        doc.events[shortcut] = [{"op": "action", "action": verb}]
        # The controller's button for this key, in the family's art, at its leading edge.
        hint = group(f"{ident}-hint", {**absolute(left=4, top=(KEY_H - HINT) / 2, width=HINT * 1.6, height=HINT), "display": keyword("flex"),
                                       "flex-direction": keyword("row"), "align-items": keyword("center"), "pointer-events": keyword("none")},
                     device_glyphs(doc, f"{ident}-button", glyph, [1, 1, 1, 0.7], HINT, HINT * 1.5 if "trigger" in glyph or glyph in ("left", "right") else None))
        if name in ("left", "right"):
            parts = [arrow(ident, width, name == "right")]
            hint["properties"].update(absolute(left=2, top=2, width=HINT * 1.6, height=HINT))
        elif key is None:
            parts = [key_label(ident, PLACEHOLDER, width, 15, HINT + 6)]
            doc.bind(f"{ident}-label.text", f"{ident}-label", "text", {"state": "osk.layerName"})
        else:
            parts = [key_label(ident, key, width, 14, HINT + 6)]
        node = key_node(doc, ident, left, top, width, [*parts, hint], {"label": key or SPACE, "event": f"{verb}Pressed"},
                        worded=name not in ("left", "right"))
        keys.append(node)
        left += width + GAP
    bind_selected(doc, "osk-shift", {"state": "osk.shift"})
    doc.events["onBack"] = [{"op": "action", "action": "oskCancel"}]
    doc.events["onActivate"] = [{"op": "focus", "control": f"osk-key-{FIRST_FOCUS}"}]

    shape = [(CUT, 0), (PANEL_W, 0), (PANEL_W, PANEL_H - CUT), (PANEL_W - CUT, PANEL_H), (0, PANEL_H), (0, CUT)]
    backing = vector("osk-backing", {**absolute(left=0, top=0, width=PANEL_W, height=PANEL_H)}, [
        path("fill", shape, fill=solid([0.03, 0.035, 0.025, 0.94])),
        path("frame", shape, stroke=stroke(solid([0, 0, 0, 1]), 3, minimum=1)),
        path("rail", [(0, CUT), (CUT, 0), (PANEL_W, 0)], closed=False, stroke=stroke(solid(rgb(ORANGE, 0.8)), 1.5, minimum=1))])
    foot, head = 720 - PANEL_H - MARGIN, MARGIN
    panel = group("osk-panel", absolute(left=(CANVAS_W - PANEL_W) / 2, top=foot, width=PANEL_W, height=PANEL_H), [backing, *keys])
    doc.bind("osk-panel.top", "osk-panel", "top", {"op": "select", "args": [{"state": "osk.top"}, head, foot]})
    stage = group("osk-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"),
                                "margin-left": length(-CANVAS_W / 2)}, [panel])
    # The session sends this screen a click only while the pointer is over the panel
    # (UI_RetainedPointerOver), so the stage around it never takes one meant for the page.
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [stage])
    return fit_narrow(doc.build(root))
