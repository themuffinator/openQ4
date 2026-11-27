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
