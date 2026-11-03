"""The retained Multiplayer page (docs/dev/ui-visual-design.md section 9,
"Menu levels"; FLOW-050).

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. MULTIPLAYER on the title hands off to
guis/menu/multiplayer.q4ui as LOAD GAME does to its page
(idSessionLocal::BeginRetainedPage): the title docks its bands and carries
its label into the title slot, and at 550 ms the page presents in the page
state Single Player holds, that label its title. It is the stock Multiplayer
page's level: its sections are the stock page's GAME BROWSER (as JOIN GAME),
CREATE SERVER, DEMOS (the demo library, which section 9 puts here) and
SETTINGS (as PLAYER SETUP, section 13.7's name, which keeps it apart from the
title's SETTINGS) on the home navigation plates from 181 u, with BACK under
them, and the stock FRIENDS LIST, hidden in the stock menu and handled by
nothing, is gone. JOIN GAME, CREATE SERVER and DEMOS go one level deeper to
their retained sub-pages where the session offers them, as Single Player's
CAMPAIGN does; where it does not, JOIN GAME and CREATE SERVER open their stock
pages (the page leaves as the stock Multiplayer page leaves for one, its plates
fading over 150 ms as it sweeps 640 u toward the trailing edge over 300 ms,
and the stock page takes over at 500 ms; its Back returns here,
multiplayerOpen) and DEMOS opens the legacy demo browser over the page.
PLAYER SETUP edits in place (retained_player_setup). BACK returns the title.
"""
from __future__ import annotations

from build_retained_screens import (ACCEL, BOTTOM_TO_PAGE, CANVAS_W, CRUMB_SCALE, CRUMB_SPAN, FULL, ORANGE, PAGE_TITLE_H, PAGE_TITLE_W, fit_narrow,
                                    SUBPAGE_REVERSE_MS, SUBPAGE_SLOT, TITLE_SLOT, TOP_TO_PAGE, TOP_TO_SUBPAGE, U, Document, absolute,
                                    by_language, colour, crumb_fits, crumb_transform, fixed_text, framing_bands, group, keyword, label,
                                    length, lit_field, navigation_plate, number, page_prompts, reflow_details, rgb, tall_page, OLIVE, track, transform,
                                    typeface, wrap_menu, section_summaries, summary_subject, fit_summary_shots, SUMMARY_JOIN,
                                    SUMMARY_CREATE, SUMMARY_DEMOS, SUMMARY_SETUP)
from retained_difficulty import check_fits

NAV_TOP = 181.0    # the stock Multiplayer page's first button (mp_b1)
SECTIONS = (("join", "#str_200187", "multiplayerJoin"), ("create", "#str_200026", "multiplayerCreate"),
            ("demos", "#str_41500", "multiplayerDemos"), ("setup", "#str_231122", "multiplayerSetup"))
DESCRIPTION = "#str_231550"    # the page's description, naming its four sections
# Each section summarizes itself in the content column while it has focus or
# hover (section 13.7): CREATE SERVER over its map's levelshot.
SUMMARIES = [("join", SUMMARY_JOIN, None), ("create", SUMMARY_CREATE, "summary.create.shot"),
             ("demos", SUMMARY_DEMOS, None), ("setup", SUMMARY_SETUP, None)]
DEPART_MS = 300    # the stock page's slide; the stock page arrives at 500 ms
DETAILS = (210.0, 140.0)    # the content column's box, u
DETAIL_TEXT, DETAIL_LINE = 18, 26
# The sections that go deeper: each's place among the sections, its label
# (which carries to the step's foot) and the name of its enter timeline.
DEEPER = (("create", 1, "#str_200026", "subpageEnterCreate"), ("join", 0, "#str_200187", "subpageEnterJoin"),
          ("demos", 2, "#str_41500", "subpageEnterDemos"))


def check_multiplayer_fits() -> None:
    """Every shipped language's labels fit: the section labels in the plate's
    314 u label box at the focus scale (1.03), the title in its slot and the
    description in the content column's lines."""
    labels = [key for _, key, _ in SECTIONS] + ["#str_230045"]
    check_fits(tuple((key, "marine", 24, U * 314 / 1.03) for key in labels) +
               (("#str_200002", "marine", 18, U * PAGE_TITLE_W),),
               tuple((key, "lowpixel", DETAIL_TEXT, U * DETAILS[0], int(U * DETAILS[1] // DETAIL_LINE))
                     for key in (DESCRIPTION, *(key for _, key, _ in SUMMARIES))))


def multiplayer_document() -> dict:
    """The Multiplayer page in the page state (sections 8 and 9): the bands
    docked, the title in the top band's slot at 39,19 u, the sections on the
    home navigation plates and the page's description in the content column
    at 395,190 u, over the lit field; the prompt bar's Select and Back.
    onActivate   every arrival: the plates at once, the description fading in
                 over 150 ms (content.in) and the focus on JOIN GAME; back
                 from a section (the session sets from_create, from_join,
                 from_demos or from_setup first) the focus is on that section;
    depart       a section opens its stock page: the plates fade over 150 ms
                 while the page sweeps 640 u toward the trailing edge over
                 300 ms, and the title and description go with them;
    subpageEnterCreate, subpageEnterJoin, subpageEnterDemos  CREATE SERVER,
                 JOIN GAME or DEMOS goes
                 deeper, 350 ms: the plates fade over 150 ms while the page
                 sweeps 640 u toward the trailing edge over 300 ms;
                 MULTIPLAYER becomes the crumb (title.crumb, 150 ms); from
                 50 ms the top band steps one notch (frame.step) and the
                 section's label carries into the slot at the step's foot
                 (title.carry, 300 ms);
    subpageReturn  back from a sub-page at 300 ms: the plates and the
                 description fade in over 150 ms;
    subpageReverseN  Back while it goes deeper: every part returns from where
                 it stands over N ms (150-300 ms);
    onDeactivate places it at rest while it is hidden, docked with its title
                 in the slot and its content out, so it never presents moved
                 and a return fades it in from nothing."""
    check_multiplayer_fits()
    doc = Document("openq4.multiplayer")
    doc.session("back", "multiplayerBack")
    doc.events["onBack"] = [{"op": "if", "condition": {"state": "setup.open"},
                             "then": [{"op": "action", "action": "playerSetupClose"}],
                             "else": [{"op": "action", "action": "back"}]}]
    buttons = []
    for ident, key, action in SECTIONS:
        doc.session(action, action)
        buttons.append(navigation_plate(doc, ident, key, action=action, subject=summary_subject(ident, SUMMARIES)))
    buttons.append(navigation_plate(doc, "back", "#str_230045", action="back"))
    wrap_menu(buttons)
    description = label("description", DESCRIPTION, {**absolute(left=0, top=0, width=U * DETAILS[0], height=U * DETAILS[1]),
                                                       **typeface("lowpixel", DETAIL_TEXT, DETAIL_LINE, rgb(OLIVE))})
    bands = framing_bands("band")
    page_top, sub_top = transform(*TOP_TO_PAGE), transform(*TOP_TO_SUBPAGE)
    bands[0]["properties"]["transform"] = page_top
    bands[1]["properties"]["transform"] = transform(*BOTTOM_TO_PAGE)
    canvas = {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}
    details = group("details", {**absolute(left=U * 395, top=U * 190, width=U * DETAILS[0], height=U * DETAILS[1]), "opacity": number(0)},
                    [description])
    summary_keys, shots = section_summaries(doc, details, SUMMARIES)
    # The description, and each summary, grows with the text above 100 % (section 2).
    reflowed = reflow_details(doc, details, (DESCRIPTION, *summary_keys))
    navigation = group("navigation", {**absolute(left=0, top=U * NAV_TOP, width=U * 357), "display": keyword("flex"),
                                      "flex-direction": keyword("column"), "opacity": number(1)}, buttons)
    content = group("content", {**canvas, "transform": transform(), "opacity": number(1)}, [navigation, details, *shots])
    # A tall view stands the plates and the description in one column.
    tall_page(doc, content, navigation, details, (DESCRIPTION, *summary_keys))
    fit_summary_shots(doc, shots, SUMMARIES, reflowed)
    # Player Setup's editor takes the column's place, its levelshots too.
    for shot in shots:
        binding = next(b for b in doc.bindings if b["node"] == shot["id"] and b["property"] == "display")
        binding["value"] = {"op": "select", "args": [{"state": "setup.open"}, "none", binding["value"]]}
    from retained_player_setup import player_setup
    content["children"].append(player_setup(doc))
    for ident in ("navigation", "details"):
        content_node = next(node for node in content["children"] if node["id"]==ident)
        content_node["properties"]["display"] = keyword("block" if ident=="details" else "flex")
        doc.bind(f"{ident}.setup-display",ident,"display",{"op":"select","args":[{"state":"setup.open"},"none","block" if ident=="details" else "flex"]})
    doc.events["setupOpen"] = [{"op":"setState","values":{"setup.open":True}}, {"op":"focus","control":"setup-name"}]
    doc.events["setupClose"] = [{"op":"setState","values":{"setup.open":False}}, {"op":"focus","control":"setup"}]
    # Going deeper, the section's label carries from its row into the slot at
    # the step's foot (title.carry, 300 ms).
    carry_starts = {ident: {"type": "transform", "unit": "dp", "value": [round(U * (44 - SUBPAGE_SLOT[0]), 3),
                                                                         round(U * (NAV_TOP + 30 * row + 2.8 - SUBPAGE_SLOT[1]), 3), 1, 1, 0]}
                    for ident, row, _key, _timeline in DEEPER}
    carry_w, carry_h, carry_scale = U * 314, U * 24.4, 0.75
    carry_end = {"type": "transform", "unit": "dp", "value": [round(-(1 - carry_scale) * carry_w / 2, 3),
                                                              round(-(1 - carry_scale) * carry_h / 2, 3), carry_scale, carry_scale, 0]}
    titles = group("titles", {**canvas, "pointer-events": keyword("none")}, [
        group("title-clip", {**absolute(left=U * TITLE_SLOT[0], top=0, width=U * PAGE_TITLE_W, height=U * 40), "overflow": keyword("hidden")}, [
            label("title", "#str_200002", {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                           **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.5]), "white-space": keyword("nowrap"),
                                           "transform": transform()})]),
        *[label(f"{ident}-carry", key, {**absolute(left=U * SUBPAGE_SLOT[0], top=U * SUBPAGE_SLOT[1], width=carry_w, height=carry_h),
                **typeface("marine", 24, carry_h, rgb(ORANGE)), "white-space": keyword("nowrap"), "opacity": number(0),
                "transform": carry_starts[ident], "pointer-events": keyword("none")}) for ident, _row, key, _timeline in DEEPER],
    ])
    # The title and the carried labels keep their size in the top band's slots.
    fixed_text(doc, titles)
    white = lambda alpha: colour([1, 1, 1, alpha])
    orange = colour(rgb(ORANGE))
    identity, swept = transform(), transform(640 * U, 0)
    fits = crumb_fits("#str_200002")
    variants = [("", CRUMB_SCALE)] + [(f"-{language}", fit) for language, fit in fits.items()]
    doc.timelines.add("arrive", 150, [
        track("content", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
        track("content", "transform", [(0, identity), (1, identity), (150, identity)]),
        track("details", "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5)), (150, white(0.5))]),
        track("prompts", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
    ])
    doc.timelines.add("rest", 1, [
        track("band-top", "transform", [(0, page_top), (1, page_top)]),
        track("content", "transform", [(0, identity), (1, identity)]),
        track("content", "opacity", [(0, number(0)), (1, number(0))]),
        track("details", "opacity", [(0, number(0)), (1, number(0))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
        track("title", "transform", [(0, identity), (1, identity)]),
        track("title-clip", "width", [(0, length(U * PAGE_TITLE_W)), (1, length(U * PAGE_TITLE_W))]),
        track("prompts", "opacity", [(0, number(1)), (1, number(1))]),
        *[part for ident, *_rest in DEEPER for part in (
            track(f"{ident}-carry", "opacity", [(0, number(0)), (1, number(0))]),
            track(f"{ident}-carry", "transform", [(0, carry_starts[ident]), (1, carry_starts[ident])]),
            track(f"{ident}-carry", "color", [(0, orange), (1, orange)]))],
    ])
    doc.timelines.add("depart", DEPART_MS, [
        track("content", "opacity", [(0, number(1)), (150, number(0)), (DEPART_MS, number(0))]),
        track("content", "transform", [(0, identity), (DEPART_MS, swept)]),
        track("title", "color", [(0, white(0.5)), (150, white(0)), (DEPART_MS, white(0))]),
        track("prompts", "opacity", [(0, number(1)), (150, number(0)), (DEPART_MS, number(0))]),
    ])
    for ident, _row, _key, timeline in DEEPER:
        carry_start = carry_starts[ident]
        doc.timelines.add(timeline, 350, [
            track("content", "opacity", [(0, number(1)), (150, number(0)), (350, number(0))]),
            track("content", "transform", [(0, identity), (300, swept), (350, swept)]),
            track("title", "color", [(0, white(0.5)), (150, white(0.4)), (350, white(0.4))]),
            track("title-clip", "width", [(0, length(U * PAGE_TITLE_W)), (150, length(U * CRUMB_SPAN)), (350, length(U * CRUMB_SPAN))]),
            track("band-top", "transform", [(0, page_top), (50, page_top, ACCEL), (350, sub_top)]),
            track(f"{ident}-carry", "opacity", [(0, number(1)), (1, number(1)), (350, number(1))]),
            track(f"{ident}-carry", "transform", [(0, carry_start), (1, carry_start), (50, carry_start, ACCEL), (350, carry_end)]),
            track(f"{ident}-carry", "color", [(0, orange), (1, orange), (50, orange, ACCEL), (350, white(0.5))]),
        ])
    for suffix, scale in variants:
        doc.timelines.add(f"crumbEnter{suffix}", 350, [
            track("title", "transform", [(0, identity), (150, crumb_transform(scale)), (350, crumb_transform(scale))])])
    doc.timelines.add("subpageReturn", 150, [
        track("content", "transform", [(0, identity), (1, identity), (150, identity)]),
        track("content", "opacity", [(0, number(0)), (150, number(1))]),
        track("details", "opacity", [(0, number(0)), (150, number(1))]),
    ])
    for duration in SUBPAGE_REVERSE_MS:
        doc.timelines.add(f"subpageReverse{duration}", duration, [
            track("content", "opacity", [(0, number(0)), (duration, number(1))]),
            track("content", "transform", [(0, identity), (duration, identity)]),
            track("title", "transform", [(0, identity), (duration, identity)]),
            track("title", "color", [(0, white(0.4)), (duration, white(0.5))]),
            track("title-clip", "width", [(0, length(U * CRUMB_SPAN)), (duration, length(U * PAGE_TITLE_W))]),
            track("band-top", "transform", [(0, sub_top, ACCEL), (duration, page_top)]),
            *[track(f"{ident}-carry", "opacity", [(0, number(1)), (duration // 2, number(0)), (duration, number(0))])
              for ident, *_rest in DEEPER],
        ])
        doc.events[f"subpageReverse{duration}"] = [{"op": "playTimeline", "timeline": f"subpageReverse{duration}"}]
    # Back from a section returns onto it (the session sets from_<section>
    # first, PLAYER SETUP's stock page included); arriving from the title
    # focuses JOIN GAME, the primary action (section 13.4).
    returns = [ident for ident, *_rest in DEEPER] + ["setup"]
    for ident in returns:
        doc.state[f"from_{ident}"] = {"type": "boolean", "initial": False}
    doc.events["onActivate"] = [{"op":"setState","values":{"setup.open":False}}, {"op": "playTimeline", "timeline": "arrive"}, {"op": "focus", "control": "join"}] + [
        {"op": "if", "condition": {"state": f"from_{ident}"}, "then": [{"op": "focus", "control": ident}]} for ident in returns]
    doc.events["onDeactivate"] = [{"op":"setState","values":{"setup.open":False}}, {"op": "playTimeline", "timeline": "rest"}]
    doc.events["depart"] = [{"op": "playTimeline", "timeline": "depart"}]
    for _ident, _row, _key, timeline in DEEPER:
        doc.events[timeline] = [{"op": "playTimeline", "timeline": timeline}, *by_language(doc, "crumbEnter", fits)]
    doc.events["subpageReturn"] = [{"op": "playTimeline", "timeline": "subpageReturn"}]
    return fit_narrow(doc.build(group("screen", {**FULL, "background-color": colour([0, 0, 0, 1])}, [
        *lit_field("field", 0.7), *bands, content, titles,
        # The pages' prompt bar, as legends: the page's own BACK is its button.
        page_prompts(doc, in_canvas=False),
    ])))
