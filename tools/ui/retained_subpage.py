"""A retained sub-page's frame (docs/dev/ui-visual-design.md sections 8 and 9,
"Menu levels"), shared by Multiplayer's Create Server and Join Game pages:
the top band one notch on and the bottom band docked, the parent page's title
as the crumb cut at the step and this page's title at the step's foot, the
content over the stock page backing, and the arrival, rest and Back
timelines.
"""
from __future__ import annotations

from build_retained_screens import fit_narrow
from build_retained_screens import (ACCEL, BOTTOM_TO_PAGE, CANVAS_W, CRUMB_SCALE, CRUMB_SLOT, CRUMB_SPAN, FULL, PAGE_TITLE_H,
                                    PAGE_TITLE_W, SUBPAGE_SLOT, TITLE_SLOT, TOP_TO_PAGE, TOP_TO_SUBPAGE, U, Document, absolute,
                                    by_language, colour, crumb_fits, crumb_transform, framing_bands, group, keyword, label, length,
                                    fixed_text, lit_field, number, page_title, track, transform, typeface)
from retained_saves import backing


def subpage_document(doc: Document, prefix: str, crumb_key: str, title_key: str, focus: str, panel_parts: list,
                     overlays: list, stretch: tuple = (), activate: list | None = None, backing_node: dict | None = None) -> dict:
    """The sub-page around `panel_parts` (its content, in `{prefix}-panel`)
    with `overlays` (its dialogs) over everything, over the stock page
    backing or `backing_node` (`{prefix}-backing`).
    onActivate    every arrival: the content and backing fade in over 150 ms
                  (content.in) and `focus` takes the focus, or `activate`
                  runs in its place;
    subpageLeave  Back, 300 ms: its title drops at once, it sweeps 640 u toward
                  the leading edge with its content fading over 250 ms, the
                  top band steps back at once and the crumb returns to the
                  title slot over the last 150 ms;
    onDeactivate  places it at rest while it is hidden."""
    panel_id, backing_id = f"{prefix}-panel", f"{prefix}-backing"
    canvas = {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}
    panel = group(panel_id, {**FULL, "opacity": number(1)}, panel_parts)
    content = group("content", {**canvas, "transform": transform(), "opacity": number(1)}, [panel])

    # The titles: the parent page's title as the crumb, cut at the step, and
    # this page's at its foot.
    titles = group("titles", {**canvas, "pointer-events": keyword("none")}, [
        group("crumb-clip", {**absolute(left=U * CRUMB_SLOT[0], top=0, width=U * CRUMB_SPAN, height=U * 40), "overflow": keyword("hidden")}, [
            label("crumb", crumb_key, {**absolute(left=0, top=U * TITLE_SLOT[1], width=U * PAGE_TITLE_W, height=U * PAGE_TITLE_H),
                                       **typeface("marine", 18, U * PAGE_TITLE_H, [1, 1, 1, 0.4]), "white-space": keyword("nowrap"),
                                       "transform": crumb_transform()})]),
        page_title("title", title_key, *SUBPAGE_SLOT),
    ])
    # The crumb and title keep their size in the band's slots at any text size.
    fixed_text(doc, titles)
    bands = framing_bands("band")
    page_top, sub_top = transform(*TOP_TO_PAGE), transform(*TOP_TO_SUBPAGE)
    bands[0]["properties"]["transform"] = sub_top
    bands[1]["properties"]["transform"] = transform(*BOTTOM_TO_PAGE)

    identity, swept_left = transform(), transform(-640 * U, 0)
    white = lambda alpha: colour([1, 1, 1, alpha])
    fits = crumb_fits(crumb_key)
    variants = [("", CRUMB_SCALE)] + [(f"-{language}", fit) for language, fit in fits.items()]
    doc.timelines.add("arrive", 150, [
        track("content", "opacity", [(0, number(1)), (1, number(1)), (150, number(1))]),
        track("content", "transform", [(0, identity), (1, identity), (150, identity)]),
        track(panel_id, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
        track(backing_id, "opacity", [(0, number(0)), (1, number(0)), (150, number(1))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5)), (150, white(0.5))]),
    ])
    doc.timelines.add("rest", 1, [
        track("band-top", "transform", [(0, sub_top), (1, sub_top)]),
        track("content", "transform", [(0, identity), (1, identity)]),
        track("content", "opacity", [(0, number(1)), (1, number(1))]),
        track(panel_id, "opacity", [(0, number(0)), (1, number(0))]),
        track(backing_id, "opacity", [(0, number(0)), (1, number(0))]),
        track("title", "color", [(0, white(0.5)), (1, white(0.5))]),
        track("crumb", "color", [(0, white(0.4)), (1, white(0.4))]),
        track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (1, length(U * CRUMB_SPAN))]),
    ])
    doc.timelines.add("subpageLeave", 300, [
        track("title", "color", [(0, white(0)), (1, white(0)), (300, white(0))]),
        track("content", "transform", [(0, identity), (300, swept_left)]),
        track(panel_id, "opacity", [(0, number(1)), (250, number(0)), (300, number(0))]),
        track(backing_id, "opacity", [(0, number(1)), (250, number(0)), (300, number(0))]),
        track("band-top", "transform", [(0, sub_top, ACCEL), (300, page_top)]),
        track("crumb-clip", "width", [(0, length(U * CRUMB_SPAN)), (150, length(U * CRUMB_SPAN)), (151, length(U * PAGE_TITLE_W)),
                                      (300, length(U * PAGE_TITLE_W))]),
        track("crumb", "color", [(0, white(0.4)), (150, white(0.4)), (300, white(0.5))]),
    ])
    for suffix, scale in variants:
        fitted = crumb_transform(scale)
        doc.timelines.add(f"crumbRest{suffix}", 1, [track("crumb", "transform", [(0, fitted), (1, fitted)])])
        doc.timelines.add(f"crumbReturn{suffix}", 300, [track("crumb", "transform", [(0, fitted), (150, fitted), (300, identity)])])
    doc.events["onActivate"] = [*by_language(doc, "crumbRest", fits), {"op": "playTimeline", "timeline": "arrive"},
                                *(activate or [{"op": "focus", "control": focus}])]
    doc.events["onDeactivate"] = [{"op": "playTimeline", "timeline": "rest"}, *by_language(doc, "crumbRest", fits)]
    doc.events["subpageLeave"] = [{"op": "playTimeline", "timeline": "subpageLeave"}, *by_language(doc, "crumbReturn", fits)]
    return fit_narrow(doc.build(group("screen", {**FULL, "background-color": colour([0, 0, 0, 1])}, [
        *lit_field("field", 0.7), *bands, backing_node or backing(backing_id), content, titles, *overlays,
    ])), pages=True, stretch=stretch)
