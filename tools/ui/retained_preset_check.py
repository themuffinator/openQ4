"""The system check (docs/dev/ui/performance-presets.md "System check"): the
screen before the main menu that offers the detected performance preset
whenever the preset system's version differs from the one the configuration
last checked. The session presents it in front of the title
(src/framework/Session_presetcheck.cpp); the stock interface's counterpart is
guis/presetcheck.gui, which reads the same state.

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The screen is the Marine menus' lit field and
framing bands, the bands drawn in toward the edges, with one section 6 modal
over them grown to a readout (confirmation). Its title is SYSTEM CHECK; its
body asks the question; under it stand the detected hardware, the nine
presets as a ladder of 45-degree cells with the recommended one lit and the
current one outlined, the recommendation's scale and frame cap, and where
the choice can be changed later. The session publishes, already localized:

    check_body, check_primary         the question, and the first action's label
    check_gpu, check_vram, check_ram, check_cpu, check_display
    check_recommended, check_detail, check_current, check_footer
    check_tierLow, check_tierHigh     the ladder's two end names
    check_recommendedTier, check_currentTier   ladder cells, -1 for none
    check_matches                     the settings already are the recommendation
    check_busy                        the apply is restarting the renderer

and the actions answer with the session verbs presetCheckApply (APPLY
RECOMMENDED, or CONTINUE when the settings already match), presetCheckKeep
(KEEP CURRENT) and presetCheckLater (ASK ME NEXT TIME, and Back). The modal
opens as the session activates the screen and stands until the session
replaces it with the main menu, so it has no leave of its own.

Text scale (section 2): the question is content and grows with the text,
the dialog with it; the readout under it is a fixed table of names and
values, like a plate's label, and keeps its size.
"""
from __future__ import annotations

from build_retained_screens import (FULL, LARGER, OLIVE, ORANGE, U, Document, absolute, colour,
                                    confirmation, fit_narrow, fixed_text, font, framing_bands, group, keyword, label, length,
                                    linear, link, lit_field, merged_text, number, path, per_language, rgb, solid, stroke, tall_dialog, tall_source,
                                    text_buckets, text_source, text_sum, text_times, text_width, transform, typeface, vector,
                                    walk_nodes, wrapped_lines)
from retained_saves import PLACEHOLDER

WIDTH = 720.0                         # dp: the modal, grown for the readout
HEIGHT_U, TOP_U = 334.0, 73.0         # u: centred in the 720 dp view
ACTIONS_U = 276.0                     # u: the actions' row (414 dp)
MARGIN = 33.0                         # dp: the stock body's leading and trailing margin
BODY_TOP, BODY_LINE, BODY_LINES = 52.0, 22.0, 3
BODY_KEYS = ("#str_232001", "#str_232002", "#str_232003")
LOWER_TOP = 126.0                     # dp: the readout, under the question's three lines
VALUE_X = 205.0                       # dp: the readout's value column
ROW_PITCH = 20.0
TIERS = 9                             # openq4::PerformancePresetCount
CELL_H, CELL_GAP, CELL_CUT = 22.0, 4.0, 8.0
BANDS_IN = 30.0                       # u: the framing bands drawn in toward the view's edges
HARDWARE = (("gpu", "#str_232004"), ("vram", "#str_232005"), ("ram", "#str_232006"), ("cpu", "#str_232007"),
            ("display", "#str_232008"))
STRINGS = ("check_body", "check_primary", "check_gpu", "check_vram", "check_ram", "check_cpu", "check_display",
           "check_recommended", "check_detail", "check_current", "check_footer", "check_tierLow", "check_tierHigh")


def heading(ident: str, key: str, top: float) -> dict:
    """A readout section's name: Marine 14 dp in olive after the family's
    corner marker, over a rule that fades out toward the trailing edge."""
    width = WIDTH - 2 * MARGIN
    return group(ident, absolute(left=MARGIN, top=top, width=width, height=18), [
        vector(f"{ident}-rule", absolute(left=0, top=17, width=width, height=1), [
            path("rule", [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, 1), (0, 1)],
                 fill=linear((0, 0), ({"fraction": 1}, 0), [(0, rgb(OLIVE, 0.7)), (0.6, rgb(OLIVE, 0.25)), (1, rgb(OLIVE, 0))]))]),
        label(f"{ident}-text", key, {**absolute(left=0, top=0, width=width, height=18),
                                     **typeface("marine", 14, 18, rgb(OLIVE, 0.95)), "white-space": keyword("nowrap"),
                                     "letter-spacing": length(0.05, "em")}),
    ])


def row(doc: Document, ident: str, key: str, state: str, top: float, *, face: str = "lowpixel", size: float = 15,
        tint: list | None = None, height: float = ROW_PITCH) -> list:
    """A readout row: its name in the leading column, the session's value
    after it on one line."""
    name = label(f"{ident}-name", key, {**absolute(left=MARGIN, top=top, width=VALUE_X - MARGIN - 8, height=height),
                                        **typeface("lowpixel", 14, height, [0.72, 0.76, 0.62, 1]), "white-space": keyword("nowrap"),
                                        "overflow": keyword("hidden")})
    value = label(f"{ident}-value", PLACEHOLDER, {**absolute(left=VALUE_X, top=top, width=WIDTH - VALUE_X - MARGIN, height=height),
                                                  **typeface(face, size, height, tint or [1, 1, 1, 0.85]),
                                                  "white-space": keyword("nowrap"), "overflow": keyword("hidden")})
    doc.bind(f"{ident}-value.text", f"{ident}-value", "text", {"state": state})
    return [name, value]


def ladder(doc: Document, top: float) -> list:
    """The nine presets as 45-degree cells, Minimum leading: each a dim olive
    plate, the recommended one lit orange and the current one outlined in
    white with a marker under it; the two end names under the ends."""
    span = WIDTH - 2 * MARGIN
    cell_w = round((span - (TIERS - 1) * CELL_GAP) / TIERS, 3)
    cells = []
    for tier in range(TIERS):
        left = round(MARGIN + tier * (cell_w + CELL_GAP), 3)
        shape = [(CELL_CUT, 0), (cell_w, 0), (cell_w - CELL_CUT, CELL_H), (0, CELL_H)]
        # Each tier a little brighter than the one before, so the ladder reads as a rise.
        rest = 0.14 + 0.03 * tier
        cells.append(vector(f"tier-{tier}", absolute(left=left, top=top, width=cell_w, height=CELL_H), [
            path("plate", shape, fill=solid(rgb(OLIVE, rest)))]))
        lit = vector(f"tier-{tier}-lit", {**absolute(left=left, top=top, width=cell_w, height=CELL_H), "opacity": number(0)}, [
            path("fill", shape, fill=linear((0, 0), (0, CELL_H), [(0, rgb(ORANGE, 0.95)), (1, rgb(ORANGE, 0.7))]))])
        doc.bind(f"tier-{tier}-lit.opacity", f"tier-{tier}-lit", "opacity",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "check_recommendedTier"}, tier]}, 1, 0]})
        now = vector(f"tier-{tier}-current", {**absolute(left=left, top=top, width=cell_w, height=CELL_H + 9), "opacity": number(0)}, [
            path("outline", [(CELL_CUT + 0.75, 0.75), (cell_w - 0.75, 0.75), (cell_w - CELL_CUT - 0.75, CELL_H - 0.75),
                             (0.75, CELL_H - 0.75)], stroke=stroke(solid([1, 1, 1, 0.9]), 1.5)),
            path("marker", [(cell_w / 2 - 4, CELL_H + 8), (cell_w / 2, CELL_H + 3), (cell_w / 2 + 4, CELL_H + 8)],
                 fill=solid([1, 1, 1, 0.9]))])
        doc.bind(f"tier-{tier}-current.opacity", f"tier-{tier}-current", "opacity",
                 {"op": "select", "args": [{"op": "==", "args": [{"state": "check_currentTier"}, tier]}, 1, 0]})
        cells += [lit, now]
    ends_top = top + CELL_H + 10
    style = {**typeface("lowpixel", 12, 14, [1, 1, 1, 0.45]), "white-space": keyword("nowrap"), "overflow": keyword("hidden")}
    low = label("tier-low", PLACEHOLDER, {**absolute(left=MARGIN, top=ends_top, width=span / 2, height=14), **style})
    high = label("tier-high", PLACEHOLDER, {**absolute(left=MARGIN + span / 2, top=ends_top, width=span / 2, height=14), **style,
                                            "text-align": keyword("right")})
    doc.bind("tier-low.text", "tier-low", "text", {"state": "check_tierLow"})
    doc.bind("tier-high.text", "tier-high", "text", {"state": "check_tierHigh"})
    return cells + [low, high]


def preset_check_document() -> dict:
    doc = Document("openq4.presetcheck")
    for name in STRINGS:
        doc.state[name] = {"type": "string", "initial": ""}
    for name in ("check_recommendedTier", "check_currentTier"):
        doc.state[name] = {"type": "number", "initial": -1}
    for name in ("check_matches", "check_busy"):
        doc.state[name] = {"type": "boolean", "initial": False}
    for verb in ("presetCheckApply", "presetCheckKeep", "presetCheckLater"):
        doc.session(verb, verb)
    doc.events["checkPickApply"] = [{"op": "action", "action": "presetCheckApply"}]
    doc.events["checkPickKeep"] = [{"op": "action", "action": "presetCheckKeep"}]
    doc.events["checkPickLater"] = [{"op": "action", "action": "presetCheckLater"}]
    doc.events["presetCheckApplying"] = [{"op": "setState", "values": {"check_busy": True}}]

    # The question: the session's sentence, growing with the text; the dialog
    # grows by what its lines need past three.
    body_w = WIDTH - 2 * MARGIN
    body_h = BODY_LINES * BODY_LINE
    counts = wrapped_lines(BODY_KEYS, "lowpixel", 17, body_w)
    for language, found in counts.items():
        if found[1] > BODY_LINES:
            raise SystemExit(f"presetcheck: the {language} question takes {found[1]} lines of {BODY_LINES}")
    text_source(doc)
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    grow = {"op": "max", "args": [0, text_sum(text_times({"op": "*", "args": [lines, BODY_LINE]}), -body_h)]}
    extra = {"op": "select", "args": [LARGER, grow, 0]}
    body = label("check-body", PLACEHOLDER, {**absolute(left=MARGIN, top=BODY_TOP, width=body_w, height=body_h),
                                             **typeface("lowpixel", 17, BODY_LINE, [1, 1, 1, 0.85]), "overflow": keyword("hidden")})
    doc.bind("check-body.text", "check-body", "text", {"state": "check_body"})
    doc.bind("text.check-body.height", "check-body", "height", text_sum(body_h, extra))

    # The readout, in one group that moves down with the question.
    y = 0.0
    parts = [heading("check-hardware", "#str_232025", y)]
    y += 20
    for ident, key in HARDWARE:
        parts += row(doc, f"check-{ident}", key, f"check_{ident}", y)
        y += ROW_PITCH
    y += 8
    parts.append(heading("check-preset", "#str_232026", y))
    y += 22
    parts += ladder(doc, y)
    y += CELL_H + 26
    parts += row(doc, "check-recommended", "#str_232010", "check_recommended", y, face="marine", size=18, tint=rgb(ORANGE), height=22)
    y += 22
    detail = label("check-detail", PLACEHOLDER, {**absolute(left=VALUE_X, top=y, width=WIDTH - VALUE_X - MARGIN, height=18),
                                                 **typeface("lowpixel", 14, 18, [1, 1, 1, 0.6]), "white-space": keyword("nowrap"),
                                                 "overflow": keyword("hidden")})
    doc.bind("check-detail.text", "check-detail", "text", {"state": "check_detail"})
    parts.append(detail)
    y += 20
    parts += row(doc, "check-current", "#str_232009", "check_current", y, face="marine", size=16, height=22)
    y += 28
    for language, text in merged_text("#str_232023").items():
        # The session fills in the menu's own SETTINGS and SYSTEM names.
        names = [merged_text(key).get(language) or merged_text(key)["english"] for key in ("#str_200009", "#str_229900")]
        if text_width("lowpixel", text.replace("%s", names[0], 1).replace("%s", names[1], 1), 13) > body_w:
            raise SystemExit(f"presetcheck: the {language} footer does not fit its line")
    footer = label("check-footer", PLACEHOLDER, {**absolute(left=MARGIN, top=y, width=body_w, height=16),
                                                 **typeface("lowpixel", 13, 16, [1, 1, 1, 0.45]), "white-space": keyword("nowrap"),
                                                 "overflow": keyword("hidden")})
    doc.bind("check-footer.text", "check-footer", "text", {"state": "check_footer"})
    parts.append(footer)
    lower_h = y + 16
    assert LOWER_TOP + lower_h <= U * ACTIONS_U, (LOWER_TOP + lower_h, U * ACTIONS_U)

    # The two actions take every language's longest label and 50 dp.
    keys = ("#str_232011", "#str_232027", "#str_232012")
    longest = max(text_width("marine", text, 20) for key in keys for text in merged_text(key).values())
    action_w = round(max(180.0, longest + 50), 1)
    if 2 * action_w + 30 + 33 + 12 > WIDTH:
        raise SystemExit(f"presetcheck: two {action_w} dp actions do not fit the {WIDTH} dp dialog")
    busy = label("check-busy", "#str_232022", {**absolute(left=30, top=U * ACTIONS_U, width=WIDTH - 63, height=45),
                                               **typeface("marine", 20, 45, rgb(ORANGE)), "white-space": keyword("nowrap"),
                                               "text-align": keyword("center"), "display": keyword("none")})
    doc.bind("check-busy.display", "check-busy", "display", {"op": "select", "args": [{"state": "check_busy"}, "block", "none"]})
    # ASK ME NEXT TIME is the quiet choice: a secondary link (section 9)
    # centred under the two actions, clear of the foot's chamfer and notch.
    widths = {language: round(text_width("marine", text, 16) + U * 7, 1) for language, text in merged_text("#str_232013").items()}
    later_w = max(widths.values())
    later_top = U * ACTIONS_U + 47
    later = group("check-later-slot", {**absolute(left=round((WIDTH - later_w) / 2, 1), top=later_top, width=later_w + 4,
                                                  height=U * 17), "display": keyword("block")},
                  [link(doc, "check_later", "#str_232013", event="checkPickLater")])
    if len(set(widths.values())) > 1:
        doc.bind("check-later-slot.left", "check-later-slot", "left",
                 per_language(doc, {language: round((WIDTH - width) / 2, 1) for language, width in widths.items()}))
    assert later_top + U * 17 + 10 <= U * HEIGHT_U, (later_top, U * HEIGHT_U)
    lower = group("check-lower", absolute(left=0, top=LOWER_TOP, width=WIDTH, height=lower_h), parts)
    doc.bind("text.check-lower.top", "check-lower", "top", text_sum(LOWER_TOP, extra))
    for node in (lower, busy):
        fixed_text(doc, node)
    modal = confirmation(doc, "check", "#str_232000", PLACEHOLDER, None, yes_event="checkPickApply", no_event="checkPickKeep",
                         height_u=HEIGHT_U, top_u=TOP_U, actions_u=ACTIONS_U, contents=[body, lower, busy, later],
                         yes_key=PLACEHOLDER, no_key="#str_232012", initial="check_yes", width_dp=WIDTH, action_w=action_w,
                         contents_grow=grow)
    nodes = {node["id"]: node for node in walk_nodes(modal)}
    # The busy line and the link move with the actions.
    for ident in ("check-busy", "check-later-slot"):
        doc.bind(f"text.{ident}.top", ident, "top", text_sum(nodes[ident]["properties"]["top"]["value"], extra))
    doc.bind("check_yes-label.text", "check_yes-label", "text", {"state": "check_primary"})
    # Section 13.4: Left and Right wrap between the actions (confirmation);
    # Down reaches the link under them, and Up comes back.
    for ident in ("check_yes", "check_no"):
        nodes[ident]["control"].setdefault("navigation", {})["down"] = "check_later"
    nodes["check_later"]["control"]["navigation"] = {"up": "check_yes"}
    # Applying restarts the renderer: the choices give way to the busy line.
    doc.bind("check-later-slot.display", "check-later-slot", "display",
             {"op": "select", "args": [{"state": "check_busy"}, "none", "block"]})
    for ident in ("check_yes", "check_no"):
        nodes[ident]["properties"]["display"] = keyword("block")
        doc.bind(f"{ident}.display", ident, "display", {"op": "select", "args": [{"state": "check_busy"}, "none", "block"]})
    # Back asks again next time rather than keeping the settings.
    nodes["check"]["modal"]["back"] = "checkPickLater"
    doc.events["onBack"] = [{"op": "call", "event": "checkPickLater"}]
    doc.events["onActivate"] = [{"op": "call", "event": "checkShow"}]
    # A portrait view shows the dialog at its tall scale (section 14.5).
    tall_source(doc)
    tall_dialog(doc, modal)

    # The backdrop: the lit field, and the framing bands drawn in toward the
    # view's edges so the dialog has the middle of the view.
    field = lit_field("field", 0.55)
    top_band, bottom_band = framing_bands("band")
    top_band["properties"]["transform"] = transform(ty=-U * BANDS_IN)
    bottom_band["properties"]["transform"] = transform(ty=U * BANDS_IN)
    root = group("screen", {**FULL, "background-color": colour([0, 0, 0, 1]), "font-family": font("marine"),
                            "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [
        group("backdrop", {**FULL, "pointer-events": keyword("none")}, [*field, top_band, bottom_band]),
        modal,
    ])
    return fit_narrow(doc.build(root))
