"""The retained interface in virtual reality (docs/user/vr.md; roadmap VR-09):
what the menus add, take away or open first while the game presents to a
headset.

The VR layouts key on the shared interface variant
(docs/dev/ui/interface-variants.md): the runtime's view source `variant`
reads "vr" while a VR session presents (idVRSystem::IsActive), or while
ui_interfaceVariant forces it for testing.

VR SETUP is the comfort dialog the session opens over the title or the pause
the first time VR presents (vr_setupDone), and that the pause's VR SETUP
link and the Virtual Reality tab's Comfort Setup row open again: the play
space (seated or standing), the standing height measured now, a recentre,
the turning, the weapon hand and the comfort vignette. Its rows set the same
CVars the Virtual Reality tab does; DONE, or Back, closes it. Its two buttons
run the session verbs vrCalibrateHeight and vrRecenter.

The notice symbol for a headset that Automatic VR Mode found after launch
(the session's RETAINED_NOTICE_VR_HEADSET, whose action is Play in VR)."""
from __future__ import annotations

from build_retained_screens import (ST_READ, U, Document, absolute, confirmation, group, keyword, label, length, path, rgb, solid, stroke,
                                    typeface, variant_is, vector)
from retained_settings_rows import COLUMN_W, ROW_PITCH, button_row, choice, cvar_row, requires, rows_value_column

MODAL = "vrSetupModal"
SHOW = f"{MODAL}Show"
TITLE = "#str_233500"                      # VR SETUP
NOTE = "#str_233501"                       # what the dialog is for
DONE = "#str_232772"                       # DONE
CALIBRATE = "#str_233502"                  # Measure My Height
RECENTER = "#str_233503"                   # Recentre View
PLAY_IN_VR = "#str_233504"                 # a headset notice's action
WIDTH = 490.0                              # dp: the settings column and its margins
MARGIN = (WIDTH - U * COLUMN_W) / 2        # dp
NOTE_TOP, NOTE_H = 44.0, 54.0              # dp: the note under the title, two lines at the VR text size
ROWS_TOP = NOTE_TOP + NOTE_H + 6           # dp
# The setup's rows: the play space and the height it measures, a recentre,
# then how the player turns, holds the gun and takes the vignette.
ROWS = (
    choice("vrsetup_playspace", "#str_231760", "vr_playSpace", "#str_231761", (0, 1, 2)),
    ("button", "vrsetup_calibrate", CALIBRATE, "vrCalibrateHeight", None),
    ("button", "vrsetup_recenter", RECENTER, "vrRecenter", None),
    choice("vrsetup_turn", "#str_230126", "vr_turnMode", "#str_230127", (0, 1)),
    choice("vrsetup_hand", "#str_230124", "vr_leftHanded", "#str_230125", (0, 1)),
    choice("vrsetup_vignette", "#str_230138", "vr_comfortVignette", "#str_230136", ("0", "0.5", "1")),
)
# The tooltips the dialog's rows share with the Virtual Reality tab's.
TOOLTIPS = {"vrsetup_playspace": "#str_233061", "vrsetup_turn": "#str_233052", "vrsetup_hand": "#str_233051",
            "vrsetup_vignette": "#str_233059"}
VERBS = ("vrCalibrateHeight", "vrRecenter")


def vr_state(doc: Document) -> dict:
    """The VR variant, the document's view.variant source declared once; its condition."""
    return variant_is(doc, "vr")


def desktop_only(doc: Document, condition: dict | None = None) -> dict:
    """A desktop setting's condition: `condition`, and not presenting to a headset."""
    flat = {"op": "!", "args": [vr_state(doc)]}
    return flat if condition is None else {"op": "&&", "args": [condition, flat]}


def setup_modal(doc: Document, *, strogg: bool = False) -> dict:
    """VR SETUP: the section 6 modal grown to hold the settings column's rows
    under a two-line note, DONE closing it as a notice's lone action."""
    for verb in VERBS:
        doc.session(verb, verb)
    value_x = rows_value_column(row for row in ROWS if row[0] != "button")
    built = []
    for row in ROWS:
        if row[0] == "button":
            _kind, ident, key, verb, _extra = row
            built.append(button_row(doc, ident, key, value_x, None, {"action": verb}))
        else:
            built.append(cvar_row(doc, row, value_x))
    # The height is measured only for standing play (vr_playSpace not 1).
    requires(doc, built[1], {"op": "!=", "args": [{"state": "cvar.vr_playSpace"}, 1]})
    rows_h = U * ROW_PITCH * len(built)
    note_face = typeface("r_strogg", 14, 20, rgb(ST_READ, 0.85)) if strogg else typeface("lowpixel", 15, 20, [1, 1, 1, 0.75])
    parts = [
        label(f"{MODAL}-note", NOTE, {**absolute(left=MARGIN, top=NOTE_TOP, width=U * COLUMN_W, height=NOTE_H), **note_face,
                                      "white-space": keyword("normal"), "overflow": keyword("hidden")}),
        group(f"{MODAL}-rows", {**absolute(left=MARGIN, top=ROWS_TOP, width=U * COLUMN_W, height=rows_h), "display": keyword("flex"),
                                "flex-direction": keyword("column")}, built),
    ]
    actions_dp = ROWS_TOP + rows_h + 14
    height_u = round((actions_dp + 45 + 24) / U, 1)
    modal = confirmation(doc, MODAL, TITLE, "#str_230030", None, close_key=DONE, contents=parts, width_dp=WIDTH,
                         height_u=height_u, top_u=round((480 - height_u) / 2, 1), actions_u=round(actions_dp / U, 1),
                         initial="vrsetup_playspace", strogg=strogg)
    return modal


def headset_symbol(ident: str) -> dict:
    """A headset on the list symbols' dark tile (section 13.13's notices): its
    visor and strap in white, the lenses cut out."""
    s = 16.0
    return vector(ident, {"position": keyword("relative"), "width": length(s), "height": length(s)}, [
        path("tile", [(3, 0), (s, 0), (s, s), (0, s), (0, 3)], fill=solid([0, 0, 0, 0.5])),
        path("visor", [(2.0, 5.5), (14.0, 5.5), (14.5, 10.5), (10.0, 11.5), (8.0, 9.5), (6.0, 11.5), (1.5, 10.5)],
             fill=solid([1, 1, 1, 0.9])),
        path("strap", [(2.0, 6.5), (1.0, 4.5), (15.0, 4.5), (14.0, 6.5)], closed=False, stroke=stroke(solid([1, 1, 1, 0.6]), 1.0)),
    ])


# ----------------------------------------------------------- the hand menu

# The pause as the off hand holds it (vr_menuAnchor 1): one card centred on
# the panel over the hand, the world around it. Its buttons are big pointer
# targets that grow and light up as the laser comes to rest on them (and the
# hand ticks), with every action the desktop pause has.
CARD_W = 380.0                              # dp
CARD_PAD, CARD_HEAD = 22.0, 52.0            # dp: the margins, the title's band
BUTTON_H, BUTTON_GAP = 52.0, 6.0            # dp
CARD_CUT = 18.0                             # dp: the 45-degree corners
HOVER_SCALE = 1.07                          # the button under the pointer grows by this
HOVER_MS = 90                               # and takes this long
CARD_ITEMS = (("vr_resume", "#str_200381", "resume", None), ("vr_objectives", "#str_200380", None, "objectivesShow"),
              ("vr_savegame", "#str_200003", "saveGame", None), ("vr_loadgame", "#str_200001", "loadGame", None),
              ("vr_restart", "#str_229983", "restartLevel", None), ("vr_settings", "#str_200009", "settings", None),
              ("vr_vrsetup", TITLE, None, SHOW), ("vr_quit", "#str_230025", None, "quitModalShow"),
              ("vr_exit", "#str_200013", None, "exitModalShow"))


def card_button(doc: Document, ident: str, key: str, top: float, *, action: str | None, event: str | None,
                tint: str, hot: str, face: str) -> dict:
    """One of the card's buttons: a plate with the family's lower-leading cut,
    its marker and label. Under the pointer or focus it grows by HOVER_SCALE
    about its middle over HOVER_MS, fills with the hot colour and rims it."""
    from build_retained_screens import (OLIVE as _OLIVE, colour, linear, marker_path, number, transform)
    width = CARD_W - 2 * CARD_PAD
    cut = 14.0
    shape = [(0, 0), (width, 0), (width, BUTTON_H), (cut, BUTTON_H), (0, BUTTON_H - cut)]
    rest = vector(f"{ident}-rest", {**absolute(left=0, top=0, width=width, height=BUTTON_H), "opacity": number(1)}, [
        path("fill", shape, fill=linear((0, 0), (width, 0), [(0, rgb(tint, 0.42)), (1, rgb(tint, 0.16))])),
        path("rim", shape, stroke=stroke(solid(rgb(tint, 0.75)), 1.2)),
    ])
    lit = vector(f"{ident}-lit", {**absolute(left=0, top=0, width=width, height=BUTTON_H), "opacity": number(0)}, [
        path("fill", shape, fill=linear((0, 0), (width, 0), [(0, rgb(hot, 0.55)), (1, rgb(hot, 0.22))])),
        path("rim", shape, stroke=stroke(solid(rgb(hot)), 2.0)),
    ])
    marker = vector(f"{ident}-marker", absolute(left=16, top=(BUTTON_H - 10) / 2, width=10, height=10),
                    [marker_path("mark", 0, 0, 10, rgb(hot))])
    text = label(f"{ident}-label", key, {**absolute(left=36, top=0, width=width - 48, height=BUTTON_H),
                                         **typeface(face, 22, BUTTON_H, [1, 1, 1, 0.88]), "white-space": keyword("nowrap"),
                                         "overflow": keyword("hidden")})
    grown, still = transform(sx=HOVER_SCALE, sy=HOVER_SCALE), transform()
    hot_text = colour([*rgb(hot)[:3], 1])
    ids = doc.states(ident, {
        "default": [(ident, "transform", still), (f"{ident}-lit", "opacity", number(0)), (f"{ident}-rest", "opacity", number(1)),
                    (f"{ident}-label", "color", colour([1, 1, 1, 0.88]))],
        "hover": [(ident, "transform", grown, HOVER_MS), (f"{ident}-lit", "opacity", number(1), HOVER_MS),
                  (f"{ident}-rest", "opacity", number(0), HOVER_MS), (f"{ident}-label", "color", hot_text, HOVER_MS)],
        "focus": [(ident, "transform", grown, HOVER_MS), (f"{ident}-lit", "opacity", number(1), HOVER_MS),
                  (f"{ident}-rest", "opacity", number(0), HOVER_MS), (f"{ident}-label", "color", hot_text, HOVER_MS)],
        "pressed": [(ident, "transform", transform(sx=1.02, sy=1.02)), (f"{ident}-lit", "opacity", number(1)),
                    (f"{ident}-rest", "opacity", number(0)), (f"{ident}-label", "color", colour([1, 1, 1, 1]))],
        "disabled": [(ident, "transform", still), (f"{ident}-lit", "opacity", number(0)), (f"{ident}-rest", "opacity", number(0.4)),
                     (f"{ident}-label", "color", colour([1, 1, 1, 0.4]))],
    })
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, {**absolute(left=CARD_PAD, top=top, width=width, height=BUTTON_H), "transform": transform()},
                 [rest, lit, marker, text], control=control)


def hand_menu(doc: Document, *, strogg: bool = False) -> dict:
    """The card (CARD_ITEMS on a dark plate with the family's 45-degree
    corners, GAME PAUSED in its head), shown only while the game presents to
    a headset and the Objectives page is closed. Up and Down wrap through it."""
    from build_retained_screens import ST_LINE, ST_SELECT, OLIVE as _OLIVE, ORANGE as _ORANGE, number
    tint, hot, face = (ST_LINE, ST_SELECT, "r_strogg") if strogg else (_OLIVE, _ORANGE, "marine")
    height = CARD_HEAD + len(CARD_ITEMS) * (BUTTON_H + BUTTON_GAP) - BUTTON_GAP + CARD_PAD
    c = CARD_CUT
    body = [(c, 0), (CARD_W, 0), (CARD_W, height - c), (CARD_W - c, height), (0, height), (0, c)]
    plate = vector("vr-card-plate", absolute(left=0, top=0, width=CARD_W, height=height), [
        path("body", body, fill=solid([0, 0, 0, 0.82])),
        path("rim", body, stroke=stroke(solid(rgb(tint, 0.9)), 1.5)),
        path("head-rule", [(CARD_PAD, CARD_HEAD - 10), (CARD_W - CARD_PAD, CARD_HEAD - 10)], closed=False,
             stroke=stroke(solid(rgb(tint, 0.45)), 1.0)),
    ])
    title = label("vr-card-title", "#str_230031", {**absolute(left=CARD_PAD + 4, top=8, width=CARD_W - 2 * CARD_PAD, height=30),
                                                   **typeface(face, 18, 30, [1, 1, 1, 0.6]), "white-space": keyword("nowrap")})
    buttons = []
    for index, (ident, key, action, event) in enumerate(CARD_ITEMS):
        buttons.append(card_button(doc, ident, key, CARD_HEAD + index * (BUTTON_H + BUTTON_GAP), action=action, event=event,
                                   tint=tint, hot=hot, face=face))
    ids = [button["id"] for button in buttons]
    for index, button in enumerate(buttons):
        button["control"]["navigation"] = {"up": ids[index - 1], "down": ids[(index + 1) % len(ids)], "left": ids[index],
                                           "right": ids[index]}
    card = group("vr-card", {"position": keyword("absolute"), "left": length(50, "%"), "top": length(50, "%"),
                             "margin-left": length(-CARD_W / 2), "margin-top": length(-height / 2), "width": length(CARD_W),
                             "height": length(height), "display": keyword("none")}, [plate, title, *buttons])
    shown = {"op": "&&", "args": [vr_state(doc), {"op": "!", "args": [{"state": "objectives.visible"}]}]}
    doc.bind("vr-card.display", "vr-card", "display", {"op": "select", "args": [shown, "block", "none"]})
    return card
