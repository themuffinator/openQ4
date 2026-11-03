"""The retained Credits page (docs/dev/ui-visual-design.md sections 8 and 9):
CREDITS on the title opens it as LOAD GAME opens its page.

Built by tools/ui/build_retained_screens.py, which writes and checks it with
the other retained screens. The page is the stock p_credits of openQ4's
mainmenu.gui, read at generation time for its layout and choreography but not
its words: the Raven logo's light-up, then thirteen screens of headings and
names, each line twice, in the rune face and in Lowpixel, translating from
runes to Latin under orange scan bars (section 8, the credits translation),
each screen leaving 640 u toward the trailing edge, then the logo again. The
generator compiles each screen's onTime script (its sets, and its colour,
textscale and rect transitions, a later transition retargeting an earlier one
from where it stands) into tracks, and lays the logo and the screens end to
end in the stock order in one timeline that repeats.

The lines' words stay in the stock menu: the session reads every line's text
from the legacy main menu when the page opens (idSessionLocal::
PublishRetainedCredits) into credits.<window>, which each label shows. The
page stands on an opaque backing, as section 8 gives Credits, under its own
CREDITS title, which fades in with the backing since nothing carried it from
the plinth; the prompt bar is in the bottom band, its BACK, the page's one
control, where the stock BACK plate stood at 532,441 u.
"""
from __future__ import annotations

import re

from build_retained_screens import fit_narrow
from build_retained_screens import (ACCEL, CANVAS_W, FULL, ORANGE, ROOT, TITLE_SLOT, U, Document, absolute, colour, font, group,
                                    keyword, label, length, linear, number, page_prompts, path, picture, rgb, track, transform,
                                    typeface, vector)
from retained_saves import BACKING_STOPS, backing_x

MAINMENU = ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mainmenu.gui"
SCREENS = 13
ORDER = (1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 10, 9, 13)     # resettime's chain, then anim_credits again
POINT = 72.0                                         # dp per unit of textscale (Lowpixel 0.25 is 18 dp)
FACES = {"fonts/lowpixel": "lowpixel", "fonts/strogg": "strogg", "fonts/marine": "marine", "fonts/r_strogg": "r_strogg"}
BAR_TONE = "#FF8000"                                 # the scan bar's additive ramp (section 8)
LOGO = (120.0, 40.0, 400.0, 400.0)                   # raven: gfx/guis/mainmenu/raven_1
LOGO_TINT = (0.839, 1, 0.658, 1)


# ------------------------------------------------------------ the stock script

def blocks(text: str, keyword_name: str):
    """(name, body) for each `<keyword_name> <name> { ... }` in text."""
    for match in re.finditer(rf"{keyword_name}\s+(\w+)\s*\{{", text):
        depth, at = 1, match.end()
        while depth and at < len(text):
            depth += {"{": 1, "}": -1}.get(text[at], 0)
            at += 1
        yield match.group(1), text[match.end():at - 1]


def own_properties(body: str) -> dict:
    """A window's own property lines, outside its children and scripts."""
    flat, depth = [], 0
    for line in body.splitlines():
        if depth == 0 and "{" not in line:
            flat.append(line)
        depth += line.count("{") - line.count("}")
    properties = {}
    for line in flat:
        match = re.match(r'\s*(\w+)\s+(.+?)\s*$', line)
        if match:
            properties[match.group(1).lower()] = match.group(2).strip().strip('"')
    return properties


def vec(text: str) -> list[float]:
    return [float(part) for part in text.replace(" ", "").split(",")]


def parse_desktop(text: str) -> dict:
    """The Desktop's definevec4 colours, by name."""
    return {name: vec(value) for name, value in re.findall(r'definevec4\s+"(\w+)"\s+([-\d.,\s]+?)\s*$', text, re.M)}


def schedule(body: str) -> list:
    """A window's onTime statements at absolute times: (ms, op, args)."""
    clock, out = 0, []
    for match in re.finditer(r"onTime\s+(\+?)(\d+)\s*\{", body):
        depth, at = 1, match.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(body[at], 0)
            at += 1
        clock = clock + int(match.group(2)) if match.group(1) else int(match.group(2))
        for statement in body[match.end():at - 1].split(";"):
            words = re.findall(r'"([^"]*)"|(\w+)', statement)
            words = [quoted or bare for quoted, bare in words]
            if words and words[0] in ("set", "transition", "resettime"):
                out.append((clock, words[0], words[1:]))
    return out


# ------------------------------------------------------------ the compiler

class Tracks:
    """Keys for one node property, with the legacy semantics: a set or a
    transition starting before an earlier one ends retargets it from the
    value it has reached."""

    def __init__(self, start):
        self.keys = [(0.0, start, None)]

    def value_at(self, at: float):
        keys = self.keys
        for (t0, v0, easing), (t1, v1, _) in zip(keys, keys[1:]):
            if t0 <= at <= t1:
                if t1 == t0:
                    return v1
                return [a + (b - a) * (at - t0) / (t1 - t0) for a, b in zip(v0, v1)]
        return keys[-1][1]

    def cut(self, at: float):
        current = self.value_at(at)
        self.keys = [key for key in self.keys if key[0] < at]
        self.keys.append((at, current, None))

    def set(self, at: float, value):
        self.cut(at)
        self.keys.append((at + 1, value, None))

    def transition(self, at: float, start, end, duration: float, easing=None):
        self.cut(at)
        if start is not None:
            self.keys[-1] = (at, start, None)
        self.keys[-1] = (self.keys[-1][0], self.keys[-1][1], easing)
        self.keys.append((at + max(duration, 1), end, None))


def compile_screen(doc: Document, prefix: str, body: str, windows: dict, colours: dict, typed, start_values: dict,
                   duration_floor: float = 0) -> tuple[list, float]:
    """One screen's (or the intro's) onTime script as timeline tracks, and its
    end: the time of its last statement."""
    tracks: dict = {}
    end = duration_floor

    def resolve(value: str, window: str, prop: str):
        if value.startswith("$"):
            name = value[1:]
            if name.startswith("desktop::"):
                return colours[name.split("::", 1)[1]]
            other, other_prop = name.split("::")
            return vec(windows[other].get(other_prop, "0,0,0,0"))
        return vec(value)

    for at, op, args in schedule(body):
        end = max(end, at)
        if op == "resettime":
            continue
        target = args[0]
        window, prop = (target.split("::") + [""])[:2] if "::" in target else (prefix, target)
        if window not in windows and window != prefix:
            continue
        key = (window, prop)
        if key not in tracks:
            tracks[key] = Tracks(start_values.get(key) or resolve(windows.get(window, {}).get(prop, "0,0,0,0") or "0", window, prop))
        if op == "set":
            tracks[key].set(at, resolve(args[1], window, prop))
        else:
            duration = float(args[3])
            easing = ACCEL if len(args) > 4 else None
            tracks[key].transition(at, resolve(args[1], window, prop), resolve(args[2], window, prop), duration, easing)
            end = max(end, at + duration)
    out = []
    for (window, prop), keys in tracks.items():
        converted = typed(window, prop, keys.keys)
        if converted:
            out.append(converted)
    return out, end


# ------------------------------------------------------------ the page

def scan_bar(ident: str, rect: list[float]) -> dict:
    """The scan bar (section 8): an additive #FF8000 ramp brightening linearly
    from its tail to its head, with soft edges, in the stock bar's rect."""
    x, y, w, h = rect
    width, height = U * w, U * h
    body = [(0, 0), (width, 0), (width, height), (0, height)]
    ramp = linear((0, 0), (width, 0), [(0, rgb(BAR_TONE, 0)), (0.85, rgb(BAR_TONE, 0.75)), (1, rgb(BAR_TONE, 1))])
    edge = linear((0, 0), (0, height), [(0, [1, 1, 1, 0]), (0.25, [1, 1, 1, 1]), (0.75, [1, 1, 1, 1]), (1, [1, 1, 1, 0])])
    return vector(ident, {**absolute(left=U * x, top=U * y, width=width, height=height), "opacity": number(0),
                          "transform": transform(), "pointer-events": keyword("none")},
                  [path("ramp", body, fill=ramp, blend="additive")],
                  mask={"paths": [path("edges", body, fill=edge)]})


TEXT_ROOM = (40.0, 436.0)     # u: a screen's lines stand under the title and over the prompt bar
TEXT_END = 619.0              # u: and end before the 4:3 view's 32 dp margin


def grow_screen(doc: Document, body: str, nodes: list) -> None:
    """Section 2's text size on one credits screen: its lines and scan bars
    are a timed composition that cannot reflow into more screens, so above
    100 % text the whole screen grows as one, its lines' boxes, pitch and
    letters, about its lines' middle and leading edge, by the text size or
    as far as its lines stand between the title and the prompt bar and,
    measured in the stock words, end before the 4:3 view's margin, whichever
    is less (a line already past it at 100 % aside). A bar keeps its sweep
    and follows its line's height. Every binding gives the authored value
    at 100 % (check_text_bindings)."""
    from build_retained_screens import LARGER, TEXT, text_source, text_sum, text_width
    text_source(doc)
    words = {name: own_properties(child) for name, child in blocks(body, "windowDef") if not name.startswith("d_")}
    lines = [node for node in nodes if node["type"] == "text" and words[node["id"]].get("text")]
    if not lines:
        return
    def rect(node):
        return [node["properties"][key]["value"] / U for key in ("left", "top", "width", "height")]
    top = min(rect(node)[1] for node in lines)
    bottom = max(rect(node)[1] + rect(node)[3] for node in lines)
    middle, lead = (top + bottom) / 2, min(rect(node)[0] for node in lines)
    reach = lead
    for node in lines:
        props = node["properties"]
        drawn = max(text_width(props["font-family"]["value"], part, props["font-size"]["value"])
                    for part in words[node["id"]]["text"].split("\\n")) / U
        if rect(node)[0] + drawn <= TEXT_END:
            reach = max(reach, rect(node)[0] + drawn)
    fit = min((middle - TEXT_ROOM[0]) / (middle - top), (TEXT_ROOM[1] - middle) / (bottom - middle),
              (TEXT_END - lead) / (reach - lead) if reach > lead else 2)
    grown = {"op": "max", "args": [1, {"op": "min", "args": [TEXT, round(fit, 4)]}]}

    def larger(ident: str, key: str, value) -> None:
        authored = next(node for node in nodes if node["id"] == ident)["properties"][key]["value"]
        doc.bind(f"text.{ident}.{key}", ident, key, {"op": "select", "args": [LARGER, value, authored]})
    for node in nodes:
        x, y, w, h = rect(node)
        if node["type"] == "text":
            larger(node["id"], "left", text_sum(U * lead, {"op": "*", "args": [round(U * (x - lead), 4), grown]}))
            for key in ("font-size", "line-height"):
                larger(node["id"], key, {"op": "/", "args": [{"op": "*", "args": [node["properties"][key]["value"], grown]}, TEXT]})
        larger(node["id"], "top", text_sum(U * middle, {"op": "*", "args": [round(U * (y - middle), 4), grown]}))
        larger(node["id"], "height", {"op": "*", "args": [round(U * h, 4), grown]})


def credits_document() -> dict:
    text = MAINMENU.read_text(encoding="utf-8", errors="replace")
    colours = parse_desktop(text)
    page = dict(blocks(text, "windowDef"))["p_credits"]
    windows = {name: own_properties(body) for name, body in blocks(page, "windowDef")}
    doc = Document("openq4.credits")
    doc.session("back", "creditsBack")
    doc.events["onBack"] = [{"op": "action", "action": "back"}]
    info, screens = {}, []
    screen_bodies = {int(name[len("p_screen"):]): body for name, body in blocks(page, "windowDef") if re.fullmatch(r"p_screen\d+", name)}
    for number_ in range(1, SCREENS + 1):
        nodes = []
        for name, child in blocks(screen_bodies[number_], "windowDef"):
            if name.startswith("d_"):
                continue
            props = own_properties(child)
            rect = vec(props.get("rect", "0,0,0,0"))
            if props.get("background", "").endswith("creditsbar"):
                info[name] = {"kind": "bar", "rect": rect}
                nodes.append(scan_bar(name, rect))
                continue
            face = FACES.get(props.get("font", ""), "lowpixel")
            scale = float(props.get("textscale", "0.25"))
            align = int(float(props.get("textalign", "0")))
            tint = vec(props.get("forecolor", "1,1,1,0"))
            info[name] = {"kind": "text", "rect": rect, "scale": scale, "align": align}
            key = f"credits.{name}"
            doc.state[key] = {"type": "string", "initial": ""}
            x, y, w, h = rect
            nodes.append(label(name, "#str_230030", {**absolute(left=U * x, top=U * y, width=U * w, height=U * h),
                               **typeface(face, scale * POINT, U * h, tint), "white-space": keyword("nowrap"),
                               "text-align": keyword(("left", "center", "right")[align]), "transform": transform(),
                               "pointer-events": keyword("none")}))
            doc.bind(f"{name}.text", name, "text", {"state": key})
        grow_screen(doc, screen_bodies[number_], nodes)
        ident = f"p_screen{number_}"
        info[ident] = {"kind": "screen", "rect": [0, 0, 640, 480]}
        screens.append(group(ident, {**absolute(left=0, top=0, width=CANVAS_W, height=720), "opacity": number(0),
                                     "transform": transform(), "pointer-events": keyword("none")}, nodes))

    def typed(window: str, prop: str, keys: list):
        node = info.get(window)
        if node is None:
            return None
        if node["kind"] == "text" and prop == "forecolor":
            return track(window, "color", [(t, colour([round(c, 4) for c in v]), e) for t, v, e in keys])
        if node["kind"] == "text" and prop == "textscale":
            x, y, w, h = node["rect"]
            shift = (0.5, 0.0, -0.5)[node["align"]]

            def scaled(v):
                s = round(v[0] / node["scale"], 4)
                return transform(U * w * (s - 1) * shift, U * h * (s - 1) * 0.5, s, s)
            return track(window, "transform", [(t, scaled(v), e) for t, v, e in keys])
        if prop == "rect" and node["kind"] in ("bar", "screen"):
            x, y = node["rect"][:2]
            return track(window, "transform", [(t, transform(U * (v[0] - x), U * (v[1] - y)), e) for t, v, e in keys])
        if prop == "matcolor" and node["kind"] == "bar":
            return track(window, "opacity", [(t, number(round(v[3] * max(v[:3]), 4)), e) for t, v, e in keys])
        if prop == "visible" and node["kind"] == "screen":
            return track(window, "opacity", [(t, number(v[0]), e) for t, v, e in keys])
        return None

    # One loop, as the stock chain runs it (anim_credits, then the screens in
    # resettime's order, then anim_credits again): each script's tracks
    # offset to its start and held at their base value before it. A loop of
    # completion programs back to the first would be a cycle, which documents
    # may not hold, so the loop repeats as one timeline.
    loop = []

    def place(tracks: list, offset: float) -> None:
        for item in tracks:
            keys = [dict(key, atMs=round(key["atMs"] + offset, 3)) for key in item["keys"]]
            if keys[0]["atMs"] > 0:
                keys.insert(0, {"atMs": 0, "value": keys[0]["value"]})
            loop.append({**item, "keys": keys})
    intro_body = dict(blocks(text, "windowDef"))["anim_credits"]
    left, top, width, height = LOGO
    logo = picture("raven", "gfx/guis/mainmenu/raven_1", {**absolute(left=U * left, top=U * top, width=U * width, height=U * height),
                   "image-color": colour([0, 0, 0, 1]), "image-blend": keyword("additive")}, fit="contain")

    def typed_intro(window: str, prop: str, keys: list):
        if window == "raven" and prop == "matcolor":
            return track("raven", "image-color", [(t, colour([round(c, 4) for c in v]), e) for t, v, e in keys])
        return None
    intro_tracks, clock = compile_screen(doc, "anim_credits", intro_body, windows | {"raven": {"matcolor": "0,0,0,1"}}, colours,
                                         typed_intro, {})
    place(intro_tracks, 0)
    for number_ in ORDER:
        ident = f"p_screen{number_}"
        tracks, end = compile_screen(doc, ident, screen_bodies[number_], windows | {ident: {"rect": "0,0,640,480", "visible": "0"}},
                                     colours, typed, {(ident, "visible"): [0.0], (ident, "rect"): [0.0, 0.0, 640.0, 480.0]})
        place(tracks, clock)
        clock += end
    doc.timelines.add("creditsLoop", clock, loop, iterations=0)
    # Every arrival starts the loop over from the logo with every screen out;
    # leaving stops it.
    doc.timelines.add("creditsRest", 1, [track(f"p_screen{n}", "opacity", [(0, number(0)), (1, number(0))]) for n in range(1, SCREENS + 1)] +
                      [track("raven", "image-color", [(0, colour([0, 0, 0, 1])), (1, colour([0, 0, 0, 1]))]),
                       track("credits-panel", "opacity", [(0, number(0)), (1, number(0))])])
    doc.timelines.add("arrive", 150, [track("credits-panel", "opacity", [(0, number(0)), (1, number(0)), (150, number(1))])])
    stop = [{"op": "cancelTimeline", "timeline": "creditsLoop", "policy": "hold"}]
    doc.events["onActivate"] = stop + [{"op": "playTimeline", "timeline": "creditsRest"}, {"op": "playTimeline", "timeline": "arrive"},
                                       {"op": "playTimeline", "timeline": "creditsLoop"}, {"op": "focus", "control": "credits_back"}]
    doc.events["onDeactivate"] = stop + [{"op": "playTimeline", "timeline": "creditsRest"}]
    # The opaque backing (section 8), the page's own title and the prompt bar,
    # BACK alone: Accept has nothing else to do here.
    first, last = BACKING_STOPS[0][0], BACKING_STOPS[-1][0]
    stops = [(round((f - first) / (last - first), 4), [0, 0, 0, alpha]) for f, alpha in BACKING_STOPS]
    whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
    backing = vector("credits-backing", {**FULL, "pointer-events": keyword("none")}, [
        path("backing", whole, fill=linear((backing_x(first), 0), (backing_x(last), 0), stops))])
    title = label("credits-title", "#str_200012", {**absolute(left=U * TITLE_SLOT[0], top=U * TITLE_SLOT[1], width=U * 300,
                  height=U * 24.4 * 0.75), **typeface("marine", 18, U * 24.4 * 0.75, [1, 1, 1, 0.5]), "white-space": keyword("nowrap")})
    back = page_prompts(doc, "credits_back", action="back", select=False)
    canvas = {**absolute(top=0, width=CANVAS_W, height=720), "left": length(50, "%"), "margin-left": length(-CANVAS_W / 2)}
    panel = group("credits-panel", {**FULL, "opacity": number(0)}, [
        backing, group("credits-stage", canvas, [logo, *screens]), group("credits-chrome", canvas, [title, back])])
    root = group("screen", {**FULL, "font-family": font("marine"), "font-size": length(16), "color": colour([1, 1, 1, 0.8])}, [panel])
    return tall_credits_page(fit_narrow(doc.build(root), pages=True))


# A tall view's Credits page (retained_tall): the screens' names, which stand
# left of the canvas's middle (about 100 to 560 dp) since the stock page
# showed its art beside them, grow 1.6 times and move to the column's middle;
# the Raven logo grows 1.3 times where it stands; the backing darkens the
# whole width. Each screen's own transform carries its animation, so the
# screens scale as one group about the view's middle.
TALL_TEXT = 1.6
TALL_TEXT_SHIFT = 240.0                      # dp: the names' middle (330 dp) onto the canvas's (480 dp) at that scale
TALL_LOGO = 1.3
TALL_TEXT_REACH = 285.0                      # dp: the names' farthest line from the canvas's middle (79.5 to 649.5 dp)
TALL_BANDS = 110.0                           # dp: the docked top band's title, the deeper of the bands


def tall_credits_page(result: dict) -> dict:
    from retained_tall import TallLayout
    tall = TallLayout(result)
    # The names, about the view's middle, stand clear of the docked bands.
    tall.fits(2 * (TALL_TEXT * TALL_TEXT_REACH + TALL_BANDS))
    stage = tall.nodes["credits-stage"]
    screens = [child for child in stage["children"] if child["id"].startswith("p_screen")]
    # The names already stand 1.6 times their size there, as far as the
    # column takes them, so they keep that size at every text size, as the
    # settings rows keep theirs.
    from build_retained_screens import TEXT, walk_nodes
    inside = {node["id"] for screen in screens for node in walk_nodes(screen)}
    for binding in tall.bindings:
        if binding["id"].startswith("text.") and binding["node"] in inside:
            authored = tall.nodes[binding["node"]]["properties"][binding["property"]]["value"]
            kept = {"op": "/", "args": [authored, TEXT]} if binding["property"] in ("font-size", "line-height") else authored
            binding["value"] = {"op": "select", "args": [tall.condition, kept, binding["value"]]}
    names = group("credits-names", {**FULL, "transform": transform(), "pointer-events": keyword("none")}, screens)
    stage["children"] = [child for child in stage["children"] if child not in screens] + [names]
    tall.nodes["credits-names"] = names
    sized = {"op": "select", "args": [tall.condition, TALL_TEXT, 1]}
    tall.bindings.append({"id": "tall.credits-names.transform", "node": "credits-names", "property": "transform",
                          "value": [{"op": "select", "args": [tall.condition, TALL_TEXT_SHIFT, 0]}, 0, sized, sized, 0]})
    logo = tall.nodes["raven"]
    logo["properties"]["transform"] = transform()
    grown = {"op": "select", "args": [tall.condition, TALL_LOGO, 1]}
    tall.bindings.append({"id": "tall.raven.transform", "node": "raven", "property": "transform", "value": [0, 0, grown, grown, 0]})
    tall.shade("credits-backing", max(alpha for _, alpha in BACKING_STOPS))
    return result
