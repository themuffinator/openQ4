"""Section 13.13's notices for a retained page (docs/dev/ui-visual-design.md):
header-band plates in the top band's trailing section, at most three stacked
downward, newest first. Each holds a status symbol, Lowpixel 14 dp text and
an optional action. The session publishes the stack (`<prefix>.<slot>.shown`, `.text`, `.kind` and
`.action`, the action's label key or empty), times each notice (4 s, or 8 s
with an action) and asks for each entrance (`<prefix>Enter<slot>`:
notice.enter, 160 ms, 12 dp in from the trailing edge with opacity),
departure (`<prefix>Leave<slot>`: notice.leave, 200 ms opacity) and the rest
of a slot a departure empties (`<prefix>Rest<slot>`). A notice's action runs
the session's `<verb><slot>`.

The layer is the session's, not a page's: `with_notices` adds it to each
retained menu once the document is built, so the stack follows the player
from page to page."""
from __future__ import annotations

from build_retained_screens import (EASE_OUT, OLIVE, ORANGE, TEXT_STATE, Document, absolute, colour, fixed_text, group, keyword, label,
                                    linear, number, path, rgb, solid, stroke, text_width, track, transform, typeface, vector)
from retained_saves import PLACEHOLDER, table_text

SLOTS = 3
WIDTH, HEIGHT, GAP = 400.0, 30.0, 6.0      # dp: within section 13.13's 320-480 dp
TOP, TRAILING = 14.0, 24.0                 # dp from the view's top and trailing edges
CUT = 8.0                                  # dp: the header band's upper-leading cut
SYMBOL_SIZE = 16.0                         # dp: the status symbol's tile
TEXT_SIZE = 14.0                           # dp: Lowpixel
ENTER_MS, LEAVE_MS, ENTER_DP = 160, 200, 12.0


def controller_symbol(ident: str) -> dict:
    """A controller crossed out, on the list symbols' dark tile (section
    13.7's device notices): its body and grips in white, the stroke across in
    orange."""
    s = SYMBOL_SIZE
    body = [(2.5, 6.0), (5.0, 4.5), (11.0, 4.5), (13.5, 6.0), (14.5, 11.0), (12.5, 12.5), (10.5, 10.5), (5.5, 10.5),
            (3.5, 12.5), (1.5, 11.0)]
    return vector(ident, {"position": keyword("relative"), "width": {"type": "length", "value": s, "unit": "dp"},
                          "height": {"type": "length", "value": s, "unit": "dp"}}, [
        path("tile", [(3, 0), (s, 0), (s, s), (0, s), (0, 3)], fill=solid([0, 0, 0, 0.5])),
        path("body", body, stroke=stroke(solid([1, 1, 1, 0.9]), 1.2)),
        path("cross", [(2.0, 14.0), (14.0, 2.0)], closed=False, stroke=stroke(solid(rgb(ORANGE)), 1.4)),
    ])


def with_notices(result: dict) -> dict:
    """A built document with the session's notice layer over it: its three
    slots join the root's last children (over the page and its dialogs), and
    their state, actions, events, bindings and timelines the document's. The
    status symbols: 1 the favorite's star, 2 a lost controller."""
    from retained_join_game import symbol
    doc = Document(result["id"])
    slots = notice_stack(doc, "notice", "noticeAction", [(1, lambda ident: symbol(ident, "fav")), (2, controller_symbol)],
                         ("#str_42904",))
    fragment = doc.build(group("notices", {}, slots))
    for key in ("state", "actions", "events"):
        if fragment.get(key):
            merged = result.get(key, {})
            # Only the text size's view source may be declared by both.
            shared = set(merged) & set(fragment[key])
            assert all(name == TEXT_STATE and merged[name] == fragment[key][name] for name in shared), (result["id"], key)
            result[key] = {**merged, **fragment[key]}
    result["bindings"] = result.get("bindings", []) + fragment.get("bindings", [])
    result["timelines"] = result.get("timelines", []) + fragment["timelines"]
    result["root"]["children"].extend(slots)
    order = ("format", "version", "id", "canvas", "state", "actions", "events", "bindings", "detail", "timelines", "root")
    return {**{key: result[key] for key in order if key in result}, **{key: value for key, value in result.items() if key not in order}}


def action_width(keys: tuple) -> float:
    """An action plate's width (dp): its longest label in any shipped
    language, with 8 dp either side."""
    return max(text_width("lowpixel", text, TEXT_SIZE) for key in keys for text in table_text(key).values()) + 16


def notice_stack(doc: Document, prefix: str, verb: str, symbols: list, action_keys: tuple) -> list:
    """The stack's three slots, root-level overlays (no full-size group holds
    them: one would take the hits under it). `symbols` are (kind, builder) pairs,
    each builder making the status symbol's node from its id; `action_keys`
    are the labels an action may carry, which size its plate."""
    act_w = action_width(action_keys)
    shape = [(CUT, 0), (WIDTH, 0), (WIDTH, HEIGHT), (0, HEIGHT), (0, CUT)]
    fill = [(0, rgb(OLIVE, 0.49)), (0.5, rgb(OLIVE, 0.49)), (0.75, rgb(OLIVE, 0.245)), (0.95, rgb(OLIVE, 0.049)), (1, rgb(OLIVE, 0))]
    rail = [(0, rgb(OLIVE)), (0.5, rgb(OLIVE)), (0.75, rgb(OLIVE, 0.5)), (0.95, rgb(OLIVE, 0.1)), (1, rgb(OLIVE, 0))]
    slots = []
    for slot in range(SLOTS):
        ident, key = f"{prefix}-{slot}", f"{prefix}.{slot}"
        doc.state.update({f"{key}.shown": {"type": "boolean", "initial": False}, f"{key}.text": {"type": "string", "initial": ""},
                          f"{key}.kind": {"type": "number", "initial": 0}, f"{key}.action": {"type": "string", "initial": ""}})
        plate = vector(f"{ident}-plate", {**absolute(left=0, top=0, width=WIDTH, height=HEIGHT), "pointer-events": keyword("none")}, [
            path("backing", shape, fill=solid([0, 0, 0, 0.85])),
            path("fill", shape, fill=linear((0, 0), (WIDTH, 0), fill)),
            path("rail", [(0.75, HEIGHT), (0.75, CUT), (CUT, 0.75), (WIDTH, 0.75)], closed=False,
                 stroke=stroke(linear((0, 0), (WIDTH, 0), rail), 1.5)),
        ])
        marks = []
        for kind, build in symbols:
            node = build(f"{ident}-symbol-{kind}")
            node["properties"].update(absolute(left=8, top=(HEIGHT - SYMBOL_SIZE) / 2))
            node["properties"]["display"] = keyword("none")
            doc.bind(f"{ident}-symbol-{kind}.display", f"{ident}-symbol-{kind}", "display",
                     {"op": "select", "args": [{"op": "==", "args": [{"state": f"{key}.kind"}, kind]}, "block", "none"]})
            marks.append(node)
        has_action = {"op": "!=", "args": [{"state": f"{key}.action"}, ""]}
        text = label(f"{ident}-text", PLACEHOLDER, {**absolute(left=32, top=0, width=WIDTH - 32 - 12 - act_w, height=HEIGHT),
                     **typeface("lowpixel", TEXT_SIZE, HEIGHT, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"),
                     "overflow": keyword("hidden"), "text-overflow": keyword("ellipsis"), "pointer-events": keyword("none")})
        doc.bind(f"{ident}-text.text", f"{ident}-text", "text", {"state": f"{key}.text"})
        action_id = f"{ident}-action"
        cut = 5.0
        act_h = HEIGHT - 8
        act_shape = [(0, 0), (act_w, 0), (act_w, act_h), (cut, act_h), (0, act_h - cut)]
        act_whole = absolute(left=0, top=0, width=act_w, height=act_h)
        rest = vector(f"{action_id}-rest", {**act_whole, "opacity": number(0.6), "pointer-events": keyword("none")},
                      [path("fill", act_shape, fill=solid(rgb(OLIVE, 0.6)))])
        hot = vector(f"{action_id}-hot", {**act_whole, "opacity": number(0), "pointer-events": keyword("none")},
                     [path("fill", act_shape, fill=solid(rgb(OLIVE, 0.35))),
                      path("rail", [(0.75, 0), (0.75, act_h - cut), (cut, act_h - 0.75), (act_w, act_h - 0.75)], closed=False,
                           stroke=stroke(solid(rgb(ORANGE)), 1.2))])
        act_label = label(f"{action_id}-label", PLACEHOLDER, {**absolute(left=8, top=0, width=act_w - 16, height=act_h),
                          **typeface("lowpixel", TEXT_SIZE, act_h, [1, 1, 1, 0.9]), "white-space": keyword("nowrap"),
                          "pointer-events": keyword("none")})
        doc.bind(f"{action_id}-label.text", f"{action_id}-label", "text", {"state": f"{key}.action"})
        doc.session(f"{verb}{slot}", f"{verb}{slot}")
        ids = doc.states(action_id, {
            "default": [(f"{action_id}-hot", "opacity", number(0)), (f"{action_id}-label", "color", colour([1, 1, 1, 0.9]))],
            "hover": [(f"{action_id}-hot", "opacity", number(1)), (f"{action_id}-label", "color", colour(rgb(ORANGE)))],
            "focus": [(f"{action_id}-hot", "opacity", number(1)), (f"{action_id}-label", "color", colour(rgb(ORANGE)))],
            "pressed": [(f"{action_id}-hot", "opacity", number(1)), (f"{action_id}-label", "color", colour(rgb(ORANGE)))],
            "disabled": [(f"{action_id}-hot", "opacity", number(0)), (f"{action_id}-label", "color", colour([1, 1, 1, 0.3]))],
        })
        action = group(action_id, {**absolute(left=WIDTH - act_w - 4, top=4, width=act_w, height=act_h), "display": keyword("none")},
                       [rest, hot, act_label], control={"role": "button", "label": PLACEHOLDER, "action": f"{verb}{slot}", "states": ids})
        doc.bind(f"{action_id}.display", action_id, "display", {"op": "select", "args": [has_action, "block", "none"]})
        node = group(ident, {**absolute(right=TRAILING, top=TOP + slot * (HEIGHT + GAP), width=WIDTH, height=HEIGHT),
                             "display": keyword("none"), "opacity": number(1), "transform": transform()},
                     [plate, *marks, text, action])
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"state": f"{key}.shown"}, "block", "none"]})
        # A header-band plate: its text and action keep their size at any
        # text size, the text ending in an ellipsis as it does at 100 %.
        fixed_text(doc, node)
        slots.append(node)
        # notice.enter, notice.leave and the rest a departure leaves; each
        # track holds its start from its first millisecond, since a track runs
        # from the property's present value.
        doc.timelines.add(f"{prefix}Enter{slot}", ENTER_MS, [
            track(ident, "opacity", [(0, number(0)), (1, number(0), EASE_OUT), (ENTER_MS, number(1))]),
            track(ident, "transform", [(0, transform(ENTER_DP, 0)), (1, transform(ENTER_DP, 0), EASE_OUT), (ENTER_MS, transform())])])
        doc.timelines.add(f"{prefix}Leave{slot}", LEAVE_MS, [
            track(ident, "opacity", [(0, number(1)), (1, number(1)), (LEAVE_MS, number(0))])])
        doc.timelines.add(f"{prefix}Rest{slot}", 1, [
            track(ident, "opacity", [(0, number(1)), (1, number(1))]), track(ident, "transform", [(0, transform()), (1, transform())])])
        for event in ("Enter", "Leave", "Rest"):
            doc.events[f"{prefix}{event}{slot}"] = [{"op": "playTimeline", "timeline": f"{prefix}{event}{slot}"}]
    return slots
