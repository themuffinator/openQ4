"""The retained restart screen (docs/dev/ui-visual-design.md sections 6 and 9,
FLOW-005): the stock guis/restart.gui that single player shows after the
player dies and the level unloads.

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The stock screen stands over black at 0.94: the
QUAKE 4 wordmark at 145,91 u (q4text, 258x63 u) over the popup art 360 u wide
at 140,129 u (popup_top, popup_mid, popup_btm to 341 u), with no title in its
slot, and four enlarged primary plates (b6_light, 324x38 u from 159,162 u on a
40 u pitch): RESTART, LOAD GAME, MAIN MENU and EXIT GAME. The plate under the
pointer or focus explains itself in the line at 362 u (Lowpixel textscale
0.20, centred). EXIT GAME asks with the stock exit confirmation.

The session presents it in place of the stock screen when the game reports
the player's death (idSessionLocal's "died"), and maps each plate to the stock
screen's own command (HandleRestartMenuCommands).
"""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, GLOW_MODAL, U, Document, colour, confirmation, fit_narrow, font, group, keyword, label,
                                    length, linear, modal_silhouette, number, path, picture, rgb, typeface, vector, absolute,
                                    track)
from retained_difficulty import check_fits, primary_plate

LOGO = (145.0, 91.0, 258.0, 63.0)          # q4text
DIALOG = (140.0, 129.0, 360.0, 212.0)      # popup_top 129, popup_btm to 341
PLATE = (19.0, 33.0, 324.0, 38.0)          # b1 at 159,162 in the dialog; b2-b4 on a 40 u pitch
PLATE_PITCH = 40.0
PLATE_LABEL = (23.0, 4.0, 290.0, 29.0)     # t1 at 160,166 with textalignx 22
PLATE_MARKER = (10.0, 10.0, 14.0)          # corner1 at 169,172, 14 u
PLATE_TEXT = 23.7                          # dp: Marine textscale 0.33
TIP_TOP, TIP_TEXT = 362.0, 14.4            # u, dp: Lowpixel textscale 0.20
PLATES = (("death_restart", "#str_200320", "#str_200321", "restart"), ("death_load", "#str_200001", "#str_200914", "load"),
          ("death_menu", "#str_200188", "#str_200915", "menu"), ("death_exit", "#str_200169", "#str_200323", None))


def restart_wordmark() -> dict:
    """QUAKE 4 as the title draws it: the q4text wordmark as additive light,
    in the stock screen's smaller box."""
    left, top, width, height = LOGO
    return picture("wordmark", "gfx/guis/mainmenu/q4text", {**absolute(left=U * left, top=U * top, width=U * width, height=U * height),
                                                           "image-blend": keyword("additive"), "image-color": colour([1, 1, 1, 1])},
                   fit="fill")


def restart_menu_document() -> dict:
    check_fits(tuple((key, "marine", PLATE_TEXT, U * PLATE_LABEL[2]) for _, key, _, _ in PLATES), ())
    check_fits(tuple((tip, "lowpixel", TIP_TEXT, CANVAS_W - 32) for _, _, tip, _ in PLATES), ())
    doc = Document("openq4.restartmenu")
    for _, _, _, action in PLATES:
        if action:
            doc.session(action, {"restart": "deathRestart", "load": "deathLoad", "menu": "deathMenu"}[action])
    doc.session("quit", "quit")
    left, top, width, height = DIALOG
    # The dialog's frame stands lit: popup_bg's glow column behind the popup art.
    w, h = U * width, U * height
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    glow_top, glow_bottom = -U * 129, U * 351
    glow = vector("death-glow", absolute(left=0, top=glow_top, width=w, height=glow_bottom - glow_top), [
        path("column", whole,
             fill=linear((0, 0), (0, {"fraction": 1}), [(0, rgb(GLOW_MODAL, 0)), (0.28, rgb(GLOW_MODAL, 0.5)), (0.41, rgb(GLOW_MODAL, 0.9)),
                                                        (0.58, rgb(GLOW_MODAL, 0.9)), (0.72, rgb(GLOW_MODAL, 0.5)), (1, rgb(GLOW_MODAL, 0))]),
             blend="additive")])
    frame = vector("death-frame", absolute(left=0, top=0, width=w, height=h), [modal_silhouette(w, h)])
    plates = []
    x, y, plate_w, plate_h = PLATE
    for index, (ident, key, tip, action) in enumerate(PLATES):
        # Feedback reaches only the plate's own subtree, so its line of
        # explanation is the plate's child, placed out on the canvas's line.
        top_u = y + PLATE_PITCH * index
        tip_id = f"{ident}-tip"
        tip_node = label(tip_id, tip, {**absolute(left=16 - U * (left + x), top=U * (TIP_TOP - top - top_u), width=CANVAS_W - 32,
                                                  height=U * 12),
                         **typeface("lowpixel", TIP_TEXT, U * 12, [1, 1, 1, 1]), "text-align": keyword("center"),
                         "white-space": keyword("nowrap"), "opacity": number(0), "pointer-events": keyword("none")})
        shown, hidden = number(1), number(0)
        plates.append(primary_plate(doc, ident, key, x, top_u, plate_w, plate_h, action=action,
                                    event=None if action else "exitModalShow", label_box=PLATE_LABEL, text=PLATE_TEXT,
                                    marker=PLATE_MARKER, extra_states={
                                        "default": [(tip_id, "opacity", hidden)], "hover": [(tip_id, "opacity", shown)],
                                        "focus": [(tip_id, "opacity", shown)], "pressed": [(tip_id, "opacity", shown)],
                                        "disabled": [(tip_id, "opacity", hidden)]}, extra_children=(tip_node,)))
    dialog = group("death-dialog", absolute(left=U * left, top=U * top, width=w, height=h), [glow, frame, *plates])
    stage = group("death-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)},
                  [restart_wordmark(), dialog])
    exit_modal = confirmation(doc, "exitModal", "#str_200169", "#str_200170", "quit", frame_leave_ms=200)
    doc.events["onActivate"] = [{"op": "focus", "control": "death_restart"}]
    # Back keeps the screen, as the stock screen took no Escape: without a
    # level behind it, closing would strand the player. It returns the focus
    # to RESTART.
    doc.events["onBack"] = [{"op": "focus", "control": "death_restart"}]
    root = group("screen", {**FULL, "background-color": colour([0, 0, 0, 0.94]), "font-family": font("marine"), "font-size": length(16),
                            "color": colour([1, 1, 1, 0.8])}, [stage, exit_modal])
    return tall_restart_menu(fit_narrow(doc.build(root)))


TALL_GROW = 1.6       # the menu levels' column scale
TALL_MARGIN = 60.0    # dp: the grown card's least distance from the view's top and foot


def tall_restart_menu(result: dict) -> dict:
    """The screen in a tall view: the wordmark and the card 1.6 times their
    size about the view's middle, as the menu levels' column, and EXIT GAME's
    question with them."""
    from retained_tall import TallLayout
    tall = TallLayout(result)
    # The wordmark's top and the card's foot stand as far from the view's
    # middle as the grown stage puts them, with a margin to the view's edges.
    reach = max(360 - tall.canvas_top("wordmark"), tall.canvas_top("death-dialog") + tall.dp("death-dialog", "height") - 360)
    tall.fits(2 * (reach * TALL_GROW + TALL_MARGIN))
    tall.scale("death-stage", TALL_GROW)
    tall.dialogs(TALL_GROW)
    return result
