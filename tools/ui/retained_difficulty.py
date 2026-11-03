"""The retained difficulty choice (docs/dev/ui-visual-design.md sections 9 and
13.7): the Marine pause's Restart Level page.

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. Section 9 "New game": difficulty choices use
section navigation plates beside a description paragraph, and the primary
action is the enlarged plate. The layout is the stock p_newgame (openQ4's own
mainmenu.gui, whose in-game Difficulty page is the same page retitled): five
b2_dark plates on a 30 u pitch from 21,172 u, the chosen one lit; the chosen
difficulty's description at 239,181 u in Lowpixel (textscale 0.25) at 0.60;
the primary plate (b6_light) at 237,222 u, 356x43 u; the prompt bar in the
bottom band, its BACK where the stock BACK plate stood at 532,441 u.

The pause's RESTART LEVEL hands off to restartlevel.q4ui as LOAD GAME does to
its page (idSessionLocal::BeginRetainedPage): the page is the stock page's
content over the departed pause, whose carried label is the page's title. The
choice stays on the page until RESTART LEVEL restarts the level at it
(RestartLevelAtSkill), so Back changes nothing. The in-game page offers no
turbo mode, as the stock one hid it. The Strogg pause hands off to the same
page in the Strogg family (restartlevel_strogg.q4ui, see retained_saves.py).
"""
from __future__ import annotations

from build_retained_screens import fit_narrow
from build_retained_screens import (CANVAS_W, FULL, LANGUAGES, OLIVE, U, Document, absolute, colour, fixed_text, font,
                                    group, keyword, label, length, linear, number, page_prompts, path, rgb,
                                    solid, stroke, text_width, track, transform, typeface, vector, wrap_menu)
from retained_saves import MARINE, STROGG_SHOULDER, Family, backing, lines_needed, section_plate, table_text

# The stock difficulties, easiest first (g_skill 0-4), with their descriptions.
DIFFICULTIES = (("#str_200014", "#str_200183"), ("#str_200015", "#str_200019"), ("#str_200016", "#str_200184"),
                ("#str_200017", "#str_200185"), ("#str_41117", "#str_41118"))
PLATES = ((21.0, 172.0), (20.0, 202.0), (20.0, 232.0), (20.0, 262.0), (20.0, 292.0))   # new_b1-new_b5 (b2_dark)
DESCRIPTION = (239.0, 181.0, 345.0, 139.0)     # new_diffDescript
DESCRIPTION_TEXT = 18.0                        # dp: Lowpixel textscale 0.25
DESCRIPTION_LINE = 23.0                        # dp
PRIMARY = (237.0, 222.0, 356.0, 43.0)          # new_bstart (b6_light)
PRIMARY_LABEL = (28.0, 6.0, 198.0, 25.0)       # new_t_bstart, from the plate's corner
PRIMARY_TEXT = 23.7                            # dp: Marine textscale 0.33
DESCRIPTION_END = 619.0                        # u: above 100 % text the description runs on to the 4:3 view's 32 dp margin


def primary_plate(doc: Document, ident: str, key: str, left: float, top: float, width: float, height: float, *,
                  action: str | None = None, event: str | None = None, label_box: tuple = None, text: float = None,
                  marker: tuple = (11.0, 10.0, 18.0), extra_states: dict | None = None, extra_children: tuple = (),
                  family: Family = MARINE) -> dict:
    """The enlarged primary plate (b6_light, Appendix B.1) that carries START
    GAME on the stock page: its band rows 4-27 of 32, a 45-degree lower-leading
    cut from row 14, the olive rail along the leading edge, the cut and the
    foot, and the olive fill at 0.49 to 31 % of the width, fading to nothing by
    95 %, drawn at 0.40. Its 18 u corner marker stands 11,10 u in and the Marine
    label (textscale 0.33, 23.7 dp) 28,6 u in. The pointer or focus lights the
    plate to full strength and turns the marker and label orange, as the stock
    plate lit; focus adds the orange inset of the home plates. `label_box`,
    `text` and `marker` (its left, top and size in u) suit a smaller plate;
    `extra_states` adds (node, property, value) tracks to each feedback state,
    which may target only the plate's own subtree, where `extra_children`
    places further nodes. The Strogg `family` washes the plate #FF9000 behind
    a #F59512 rail, trades the cut for a 30-degree shoulder and the marker for
    the slanted one, and sets the label in R_Strogg."""
    label_box = label_box or PRIMARY_LABEL
    text = text or PRIMARY_TEXT
    hot, wash = family.hot, (family.light if family.strogg else OLIVE)
    w, h = U * width, U * height
    band_top, band_bottom, cut = round(h * 4 / 32, 4), round(h * 27 / 32, 4), round(h * 13 / 32, 4)
    cut_x, cut_y = (round(cut * 0.6 * STROGG_SHOULDER, 4), round(cut * 0.6, 4)) if family.strogg else (cut, cut)
    fill = [(0, rgb(wash, 0.49)), (0.31, rgb(wash, 0.49)), (0.5, rgb(wash, 0.37)), (0.75, rgb(wash, 0.17)),
            (0.94, rgb(wash, 0.055)), (0.953, rgb(wash, 0)), (1, rgb(wash, 0))]
    line = family.rail if family.strogg else OLIVE
    rail = [(0, rgb(line)), (0.31, rgb(line)), (0.953, rgb(line, 0)), (1, rgb(line, 0))]
    whole = {**absolute(left=0, top=0, width=w, height=h)}
    plate = vector(f"{ident}-plate", {**whole, "opacity": number(0.4)}, [
        path("fill", [(0, band_top), (w, band_top), (w, band_bottom), (cut_x, band_bottom), (0, band_bottom - cut_y)],
             fill=linear((0, 0), (w, 0), fill)),
        path("rail", [(0.75, band_top), (0.75, band_bottom - cut_y), (cut_x, band_bottom - 0.75), (w, band_bottom - 0.75)],
             closed=False, stroke=stroke(linear((0, 0), (w, 0), rail), 1.5)),
    ])
    focus = vector(f"{ident}-focus", {**whole, "opacity": number(0)}, [
        path("inset", [(cut_x + 2, band_bottom - 3.2), (round(w * 0.6, 4), band_bottom - 3.2)], closed=False,
             stroke=stroke(solid(rgb(hot)), 1.2))])
    # The stock corner art is 11 u, drawn here at the marker's size.
    mark_left, mark_top, mark_size = marker
    scale = mark_size / 11
    mark = (round(U * (mark_left + 1.75 * scale), 4), round(U * (mark_top + 2.75 * scale), 4), round(U * 5.5 * scale, 4))
    marker_rest = vector(f"{ident}-marker", {**whole, "opacity": number(0.4)}, [family.mark("mark", *mark, rgb(family.marker))])
    marker_hot = vector(f"{ident}-marker-hot", {**whole, "opacity": number(0)}, [family.mark("mark", *mark, rgb(hot))])
    x, y, label_w, label_h = label_box
    text_node = label(f"{ident}-label", key, {**absolute(left=U * x, top=U * y, width=U * label_w, height=U * label_h),
                      **typeface(family.face, text, U * label_h, list(family.read)), "white-space": keyword("nowrap"),
                      "transform": transform()})
    lit, rest = number(1), number(0)

    def state(plate_alpha, marker, hot, inset, tint, moved):
        return [(f"{ident}-plate", "opacity", number(plate_alpha)), (f"{ident}-marker", "opacity", number(marker)),
                (f"{ident}-marker-hot", "opacity", number(hot)), (f"{ident}-focus", "opacity", number(inset)),
                (f"{ident}-label", "color", colour(tint)), (f"{ident}-label", "transform", moved)]
    states = {
        "default": state(0.4, 0.4, 0, 0, list(family.read), transform()),
        "hover": state(1, 0, 1, 0, rgb(hot), transform(sx=1.03, sy=1.03)),
        "focus": state(1, 0, 1, 1, rgb(hot), transform(sx=1.03, sy=1.03)),
        "pressed": state(1, 0, 1, 1, rgb(hot), transform(tx=1, ty=1, sx=1.03, sy=1.03)),
        "disabled": state(0.4, 0.4, 0, 0, family.tint(0.3), transform()),
    }
    for name, tracks in (extra_states or {}).items():
        states[name] = states[name] + list(tracks)
    ids = doc.states(ident, states)
    control = {"role": "button", "label": key, "states": ids}
    if action:
        control["action"] = action
    else:
        control["event"] = event
    return group(ident, absolute(left=U * left, top=U * top, width=w, height=h),
                 [plate, focus, marker_rest, marker_hot, text_node, *extra_children], control=control)


def difficulty_plates(doc: Document, key: str, family: Family = MARINE) -> list:
    """The five difficulty plates, each choosing its difficulty into `key`
    (the page's own state) and lit while it holds the choice."""
    chosen = {"state": key}
    plates = []
    for index, ((name, _), (left, top)) in enumerate(zip(DIFFICULTIES, PLATES)):
        event = f"chooseSkill{index}"
        doc.events[event] = [{"op": "setState", "values": {key: index}}]
        plates.append(section_plate(doc, f"skill-{index}", name, left, top, event=event,
                                    selected={"op": "==", "args": [chosen, index]}, family=family))
    return plates


def difficulty_description(doc: Document, ident: str, key: str, family: Family = MARINE) -> dict:
    """The chosen difficulty's description beside the plates."""
    left, top, width, height = DESCRIPTION
    node = label(ident, DIFFICULTIES[1][1], {**absolute(left=U * left, top=U * top, width=U * width, height=U * height),
                 **typeface("lowpixel", DESCRIPTION_TEXT, DESCRIPTION_LINE, family.tint(0.6)), "white-space": keyword("normal"),
                 "overflow": keyword("hidden")})
    text = DIFFICULTIES[0][1]
    for index in range(1, len(DIFFICULTIES)):
        text = {"op": "select", "args": [{"op": "==", "args": [{"state": key}, index]}, DIFFICULTIES[index][1], text]}
    doc.bind(f"{ident}.text", ident, "text", text)
    return node


def grow_description(doc: Document, node: dict, below: tuple) -> None:
    """Section 2's reflow of the chosen difficulty's description: above 100 %
    text it runs on to the 4:3 view's margin and takes the lines the running
    language's longest description needs at the running size, and the parts
    under it (`below`, RESTART LEVEL) move down as far as it grew past the
    lines it held at 100 %, never up. Every binding gives the authored value
    at 100 % (check_text_bindings)."""
    from build_retained_screens import LARGER, per_language, text_buckets, text_source, text_sum, text_times, wrapped_lines
    text_source(doc)
    props = node["properties"]
    left, width, height = (props[key]["value"] for key in ("left", "width", "height"))
    wide = U * DESCRIPTION_END - left
    counts = wrapped_lines([key for _, key in DIFFICULTIES], "lowpixel", DESCRIPTION_TEXT, wide)
    lines = text_buckets({scale: per_language(doc, {language: found[scale] for language, found in counts.items()}) for scale in (1.5, 2)})
    needed = text_times({"op": "*", "args": [lines, DESCRIPTION_LINE]})
    held = max(found[1] for found in counts.values()) * DESCRIPTION_LINE
    doc.bind(f"text.{node['id']}.width", node["id"], "width", {"op": "select", "args": [LARGER, wide, width]})
    doc.bind(f"text.{node['id']}.height", node["id"], "height", {"op": "select", "args": [LARGER, needed, height]})
    for part in below:
        # A part the narrow-view fit centres moves by its offset from the middle.
        place = "top" if part["properties"]["top"].get("unit") == "dp" else "margin-top"
        authored = part["properties"][place]["value"]
        moved = {"op": "max", "args": [authored, text_sum(authored - held, needed)]}
        doc.bind(f"text.{part['id']}.{place}", part["id"], place, {"op": "select", "args": [LARGER, moved, authored]})


def focus_chosen(key: str) -> list:
    """Steps that focus the plate holding the choice; the last request wins."""
    steps = [{"op": "focus", "control": "skill-0"}]
    for index in range(1, len(DIFFICULTIES)):
        steps.append({"op": "if", "condition": {"op": "==", "args": [{"state": key}, index]},
                      "then": [{"op": "focus", "control": f"skill-{index}"}]})
    return steps


def check_fits(one_line: tuple, wrapped: tuple) -> None:
    """Every shipped language's text fits its box; generation fails otherwise."""
    for key, face, size, width in one_line:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        for language, text in texts.items():
            if text_width(face, text, size) > width:
                raise SystemExit(f"{key} in {language} ({text!r}) needs {text_width(face, text, size):.0f} dp of {width:.0f}")
    for key, face, size, width, allowed in wrapped:
        texts = table_text(key)
        if sorted(texts) != sorted(LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(LANGUAGES) - set(texts))}")
        for language, text in texts.items():
            if lines_needed(face, text, size, width) > allowed:
                raise SystemExit(f"{key} in {language} needs {lines_needed(face, text, size, width)} lines of {allowed}")


def restart_level_document(family: Family = MARINE) -> dict:
    lines = int(U * DESCRIPTION[3] // DESCRIPTION_LINE)
    check_fits(tuple((name, family.face, 22.3, U * 186) for name, _ in DIFFICULTIES) +
               (("#str_229983", family.face, PRIMARY_TEXT, U * PRIMARY_LABEL[2]),),
               tuple((text, "lowpixel", DESCRIPTION_TEXT, U * DESCRIPTION[2], lines) for _, text in DIFFICULTIES))
    doc = Document(f"openq4.restartlevel{family.suffix}")
    doc.state["restart.skill"] = {"type": "number", "initial": 1}
    doc.session("back", "restartBack")
    doc.session("go", "restartGo")
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    # The Menu button closes the menus and resumes play (section 13.5): the
    # session returns the pause, whose RESUME closes it.
    doc.session("menuResume", "menuResume")
    doc.events["onMenuButton"] = [{"op": "action", "action": "menuResume"}]
    plates = difficulty_plates(doc, "restart.skill", family)
    description = difficulty_description(doc, "restart-description", "restart.skill", family)
    go = primary_plate(doc, "restart_go", "#str_229983", *PRIMARY, action="go", family=family)
    # Above 100 % text the plates' labels keep their size (furniture) and the
    # description grows, RESTART LEVEL moving down under it (section 2).
    for node in [*plates, go]:
        fixed_text(doc, node)
    # The five difficulties wrap through RESTART LEVEL (section 13.4).
    wrap_menu(plates, [go])
    back = page_prompts(doc, "restart_back", action="back", **family.prompts())
    # The plates show at once, under the title the pause carried; the panel
    # and the backing fade in over 150 ms (content.in), as a stock page's do.
    # The plates follow the panel, whose full-size box would take their hits.
    panel = group("restart-panel", {**FULL, "opacity": number(1)}, [description, go, back])
    stage = group("restart-stage", {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"),
                                    "margin-left": length(-CANVAS_W / 2)}, [panel, *plates])
    doc.timelines.add("arrive", 150, [track(node, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))])
                                      for node in ("restart-panel", "restart-backing")])
    doc.timelines.add("rest", 1, [track(node, "opacity", [(0, number(0)), (1, number(0))])
                                  for node in ("restart-panel", "restart-backing")])
    # Each arrival focuses the plate holding the level's difficulty.
    doc.events["onActivate"] = [{"op": "playTimeline", "timeline": "arrive"}, *focus_chosen("restart.skill")]
    doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}]
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])},
                 [backing("restart-backing"), stage])
    result = fit_narrow(doc.build(root), pages=True)
    grow_description(doc, description, (go,))
    return tall_restart_page(result, family)


# A tall view's Restart Level page (retained_tall), as the menu levels'
# column: the five plates, the chosen difficulty's description under them
# from the plates' label inset to the column's margin and RESTART LEVEL under
# that, 1.6 times the landscape size, centred between the docked bands.
TALL_GROW = 1.6
TALL_TOP, TALL_BOTTOM = 110.0, 100.0        # dp: under the docked top band's title, over the docked bottom band


def tall_restart_page(result: dict, family: Family) -> dict:
    from build_retained_screens import TALL_MARGIN, TEXT, VIEW_HEIGHT, evaluate, text_sum, wrapped_lines
    from retained_saves import BACKING_ALPHA
    from retained_tall import MARGIN, TallLayout
    tall = TallLayout(result)
    # The description takes the lines its longest text needs in its width at
    # the running text size, and RESTART LEVEL follows it (section 2).
    width = CANVAS_W - TALL_MARGIN * (1 + TALL_GROW)
    counts = wrapped_lines([key for _, key in DIFFICULTIES], "lowpixel", DESCRIPTION_TEXT, width / TALL_GROW)
    lines = {scale: max(found[scale] for found in counts.values()) for scale in (1, 1.5, 2)}
    line = DESCRIPTION_LINE * TALL_GROW
    description_h = {"op": "*", "args": [{"op": "select", "args": [{"op": ">", "args": [TEXT, 1.5]}, lines[2] * line,
                     {"op": "select", "args": [{"op": ">", "args": [TEXT, 1]}, lines[1.5] * line, lines[1] * line]}]}, TEXT]}
    plates_h = U * (PLATES[-1][1] - PLATES[0][1] + 30) * TALL_GROW
    go_h = U * PRIMARY[3] * TALL_GROW
    fixed = plates_h + 24 + 24 + go_h
    tall.fits(TALL_TOP + fixed + lines[1] * line + TALL_BOTTOM,
              {scale: TALL_TOP + fixed + lines[scale] * line * scale + TALL_BOTTOM for scale in (1.5, 2)})
    # The column centred between the bands, where the view leaves room.
    shift = {"op": "max", "args": [0, {"op": "/", "args": [{"op": "-", "args": [VIEW_HEIGHT, text_sum(TALL_TOP + TALL_BOTTOM + fixed,
                                                                                                            description_h)]}, 2]}]}
    origin = (U * min(left for left, _ in PLATES), U * PLATES[0][1])
    tall.block(tuple(f"skill-{index}" for index in range(len(PLATES))), origin, (origin[0], TALL_TOP), TALL_GROW, shift)
    description_top = TALL_TOP + plates_h + 24
    tall.block(("restart-description",), (U * DESCRIPTION[0], U * DESCRIPTION[1]), (TALL_MARGIN * TALL_GROW, description_top), TALL_GROW,
               shift)
    tall.set("restart-description", "width", width)
    tall.set("restart-description", "height", description_h)
    held = evaluate(description_h, {"view.text": 1})
    tall.block(("restart_go",), (U * PRIMARY[0], U * PRIMARY[1]), (MARGIN, description_top + held + 24), TALL_GROW,
               text_sum(shift, description_h, -held))
    tall.shade("restart-backing", BACKING_ALPHA)
    return result

