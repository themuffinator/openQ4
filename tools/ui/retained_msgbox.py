"""The retained message box (docs/dev/ui-visual-design.md section 6): the
session's guis/msg.gui in the pop-up design, for every notice, question and
wait the engine raises (the UPDATES answer, a failed connection, a password
prompt, "connecting...").

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The stock box is the popup art 320 u wide with a
title in its slot, a message and up to two buttons; the session fills it
through GUI state and reads the button back as a command
(idSessionLocal::MessageBox, HandleMsgCommands). This document takes the same
state under the same names, so the session drives either box alike:

    title, message                       the texts, already localized
    visible_msgbox, visible_waitbox      a box with buttons, or a wait
    visible_left, visible_right, left, right     the two buttons and their labels
    visible_mid, mid                     the lone button and its label
    visible_entry, str_entry             a prompt's field and the text typed

and answers with the session verbs msgLeft, msgRight and msgMid, which the
session maps to the stock box's "left", "right" and "mid". The CD key and
download boxes keep the stock screen.

The dialog is the section 6 modal 560 dp wide, grown to 176 u for six lines of
message, with the pop-up's frame, title tab and energy (confirmation). It
opens as the session activates it and stands until the session takes it
down, so it has no leave of its own.
"""
from __future__ import annotations

from build_retained_screens import (CANVAS_W, FULL, U, Document, absolute, action_plate, colour, confirmation, fit_narrow, font, group,
                                    keyword, label, length, number, typeface, walk_nodes)
from retained_create_server import text_row
from retained_saves import PLACEHOLDER

WIDTH = 560.0                        # dp
HEIGHT_U, TOP_U = 176.0, 135.0       # u: 40 u taller than the stock modal about the same centre
ACTIONS_U = 136.0                    # u: the buttons' row
ACTION_W = 200.0                     # dp: OK, CANCEL, YES, NO and RETRY in every language
BODY = (33.0, 54.0, 6)               # dp: the message's left and top, and its lines
BODY_LINE = 22.0
ENTRY_TOP, ENTRY_ROW = 150.0, 36.0   # dp: a prompt's field under a short message
ENTRY_BYTES = 63                     # a password or an address


def message_box_document() -> dict:
    doc = Document("openq4.msgbox")
    for name in ("title", "message", "left", "right", "mid", "str_entry"):
        doc.state[name] = {"type": "string", "initial": ""}
    for name in ("visible_msgbox", "visible_waitbox", "visible_left", "visible_right", "visible_mid", "visible_entry"):
        doc.state[name] = {"type": "boolean", "initial": False}
    for verb in ("msgLeft", "msgRight", "msgMid"):
        doc.session(verb, verb)
    left, top, lines = BODY
    body = label("msg-body", PLACEHOLDER, {**absolute(left=left, top=top, width=WIDTH - 2 * left, height=lines * BODY_LINE),
                                           **typeface("lowpixel", 17, BODY_LINE, [1, 1, 1, 0.8]), "white-space": keyword("pre-wrap"),
                                           "overflow": keyword("hidden")})
    doc.bind("msg-body.text", "msg-body", "text", {"state": "message"})
    # A prompt's message is a line or two; its field stands under them.
    doc.bind("msg-body.height", "msg-body", "height",
             {"op": "select", "args": [{"state": "visible_entry"}, ENTRY_TOP - top - 6, lines * BODY_LINE]})
    row_w = WIDTH - 2 * left
    doc.events["msgEntered"] = [{"op": "action", "action": "msgLeft"}]
    field = text_row(doc, "msg-entry", "#str_04321", row_w, 29, "str_entry", "msgEntered", ENTRY_BYTES, ENTRY_ROW, allow_empty=True,
                     caption=False)
    entry = group("msg-entry-rows", {**absolute(left=left, top=ENTRY_TOP, width=row_w, height=ENTRY_ROW), "display": keyword("none"),
                                     "flex-direction": keyword("column")}, [field])
    doc.bind("msg-entry-rows.display", "msg-entry-rows", "display", {"op": "select", "args": [{"state": "visible_entry"}, "flex", "none"]})
    mid = action_plate(doc, "msg_mid", PLACEHOLDER, (WIDTH - ACTION_W) / 2, U * ACTIONS_U, event="msgPickMid", width=ACTION_W)
    # Escape answers as the stock box's did: the trailing button of two, or
    # the lone one; a wait has no answer.
    doc.events["msgPickLeft"] = [{"op": "action", "action": "msgLeft"}]
    doc.events["msgPickMid"] = [{"op": "action", "action": "msgMid"}]
    doc.events["msgPickRight"] = [{"op": "if", "condition": {"state": "visible_right"}, "then": [{"op": "action", "action": "msgRight"}],
                                   "else": [{"op": "if", "condition": {"state": "visible_mid"},
                                             "then": [{"op": "action", "action": "msgMid"}]}]}]
    modal = confirmation(doc, "msg", PLACEHOLDER, PLACEHOLDER, None, yes_event="msgPickLeft", no_event="msgPickRight",
                         height_u=HEIGHT_U, top_u=TOP_U, actions_u=ACTIONS_U, contents=[body, entry, mid],
                         yes_key=PLACEHOLDER, no_key=PLACEHOLDER, initial="msg_no", width_dp=WIDTH, action_w=ACTION_W)
    nodes = {node["id"]: node for node in walk_nodes(modal)}
    # The title and its outline copies say the session's title.
    for ident in nodes:
        if ident == "msg-title" or ident.startswith("msg-title-outline-"):
            doc.bind(f"{ident}.text", ident, "text", {"state": "title"})
    for plate, shown, text in (("msg_yes", "visible_left", "left"), ("msg_no", "visible_right", "right"), ("msg_mid", "visible_mid", "mid")):
        nodes[plate]["properties"]["display"] = keyword("none")
        doc.bind(f"{plate}.display", plate, "display", {"op": "select", "args": [{"state": shown}, "block", "none"]})
        doc.bind(f"{plate}-label.text", f"{plate}-label", "text", {"state": text})
    # The session shows the box by activating it. The first control that is
    # there takes the focus: a prompt's field, the lone button, or the
    # trailing one of two (the stock box's cursor stood on it).
    doc.events["msgFocus"] = [{"op": "if", "condition": {"state": "visible_entry"}, "then": [{"op": "focus", "control": "msg-entry"}],
                               "else": [{"op": "if", "condition": {"state": "visible_mid"}, "then": [{"op": "focus", "control": "msg_mid"}],
                                         "else": [{"op": "if", "condition": {"state": "visible_right"},
                                                   "then": [{"op": "focus", "control": "msg_no"}]}]}]}]
    doc.events["msgShown"].append({"op": "call", "event": "msgFocus"})
    doc.events["onActivate"] = [{"op": "call", "event": "msgShow"}]
    doc.events["Prompt"] = [{"op": "setState", "values": {"str_entry": ""}}]
    doc.events["onBack"] = [{"op": "call", "event": "msgPickRight"}]
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [modal])
    return fit_narrow(doc.build(root))
