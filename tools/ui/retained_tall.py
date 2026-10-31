"""Tall views for the retained pages (docs/dev/ui-visual-design.md sections 3,
13.1 "Adapt, don't shrink" and 13.3; UI-02): in a view of 3:4 and taller (the
runtime's view.aspect source) a page stands its parts down the view in one
column, each block grown to read as it does in a landscape view rather than
shrunk with the 4:3 composition, and its list runs to the bottom band and
scrolls.

The layout is a pass over the page as built and fitted for narrow views
(build_retained_screens.fit_narrow with a stretched list, so the canvas runs
the view's full height, 960 dp wide, and the parts above the list keep the
view's top). Each block of parts is scaled about its own origin and moved to
its place in the column, and the list widened across the content, by
bindings whose landscape value is the fitted document's own: a view of 3:4 or
wider presents exactly as before. Lengths the runtime writes, or that
something already binds or animates, are left alone; vector art takes
fractions of its box so that it scales with it."""
from __future__ import annotations

from build_retained_screens import ASPECT_STATE, CANVAS_W, TALL, U, VIEW_HEIGHT
from retained_settings_rows import _in_fractions, _runtime_owned

MARGIN = U * 25                       # dp: the column's leading edge, a picture's own landscape inset
CONTENT_W = CANVAS_W - 2 * MARGIN     # dp: the column's width


class TallLayout:
    """The fitted document `result`, its nodes by id and the lengths nothing
    else may own, to which tall bindings are added."""

    def __init__(self, result: dict):
        self.result = result
        result.setdefault("state", {})[ASPECT_STATE] = {"type": "number", "initial": round(4 / 3, 4), "view": "aspect"}
        self.bindings = result.setdefault("bindings", [])
        self.nodes: dict = {}

        def index(node: dict) -> None:
            self.nodes[node["id"]] = node
            for child in node.get("children", []):
                index(child)
        index(result["root"])
        self.owned: set = set()
        _runtime_owned(result["root"], self.owned)
        for node in self.nodes.values():
            control = node.get("control") or {}
            if control.get("role") == "scrollbar":
                # The runtime sizes and places a scrollbar's thumb.
                self.owned.update((control["parts"]["thumb"], key) for key in ("top", "height", "left", "width"))
        self.owned.update((binding["node"], binding["property"]) for binding in self.bindings)
        self.owned.update((entry["node"], entry["property"]) for timeline in result.get("timelines", []) for entry in timeline["tracks"])

    def dp(self, ident: str, prop: str) -> float | None:
        found = self.nodes[ident].get("properties", {}).get(prop)
        if isinstance(found, dict) and found.get("type") == "length" and found.get("unit") == "dp":
            return found["value"]
        return None

    def bind(self, ident: str, prop: str, tall: float, wide: float | None = None) -> None:
        """`ident`'s `prop` is `tall` dp in a tall view, its fitted value (or
        `wide`) otherwise."""
        if (ident, prop) in self.owned:
            raise SystemExit(f"the tall layout cannot place {ident}.{prop}: something else owns it")
        landscape = self.dp(ident, prop) if wide is None else wide
        if landscape is None:
            raise SystemExit(f"{ident}.{prop} has no dp length for the tall layout to keep in landscape views")
        self.owned.add((ident, prop))
        self.bindings.append({"id": f"tall.{ident}.{prop}", "node": ident, "property": prop,
                              "value": {"op": "select", "args": [TALL, round(tall, 4) if isinstance(tall, (int, float)) else tall,
                                                                 landscape]}})

    def top(self, ident: str, tall: float) -> None:
        """`ident` stands `tall` dp below the view's top edge in a tall view:
        its top where the narrow-view fit keeps the view's top, else its
        margin from the middle where the fit centres it."""
        if self.dp(ident, "top") is not None:
            self.bind(ident, "top", tall)
        elif self.dp(ident, "margin-top") is not None:
            self.bind(ident, "margin-top", {"op": "-", "args": [round(tall, 4), {"op": "/", "args": [VIEW_HEIGHT, 2]}]})
        else:
            raise SystemExit(f"{ident} has neither a dp top nor a centred margin for the tall layout to set")

    def canvas_top(self, ident: str) -> float:
        """`ident`'s landscape top on the 720 dp canvas, centred or not."""
        top = self.dp(ident, "top")
        return top if top is not None else self.dp(ident, "margin-top") + 360

    def set(self, ident: str, prop: str, tall: float) -> None:
        """`ident`'s `prop` is `tall` dp in a tall view, replacing what this
        layout gave it before (a grown length a block rearranges)."""
        for binding in self.bindings:
            if binding["id"] == f"tall.{ident}.{prop}":
                binding["value"]["args"][1] = round(tall, 4)
                return
        self.bind(ident, prop, tall)

    def show(self, ident: str, tall: str, wide: str) -> None:
        """`ident`'s display in a tall view and otherwise."""
        if (ident, "display") in self.owned:
            raise SystemExit(f"the tall layout cannot show {ident}: something else owns its display")
        self.owned.add((ident, "display"))
        # A bound property needs its base value in the document.
        self.nodes[ident].setdefault("properties", {}).setdefault("display", {"type": "keyword", "value": wide})
        self.bindings.append({"id": f"tall.{ident}.display", "node": ident, "property": "display",
                              "value": {"op": "select", "args": [TALL, tall, wide]}})

    def shade(self, backing: str, alpha: float) -> None:
        """A tall view darkens the view's whole width at `alpha` behind the
        page, as its `backing` darkens the trailing side where a landscape
        view keeps the content (the column also covers the leading side that
        the backing leaves clear), and the shade fades with the backing."""
        from build_retained_screens import FULL, keyword, number, path, solid, vector
        parent = next(node for node in self.nodes.values() if any(child["id"] == backing for child in node.get("children", [])))
        ident = f"{backing}-tall"
        whole = [(0, 0), ({"fraction": 1}, 0), ({"fraction": 1}, {"fraction": 1}), (0, {"fraction": 1})]
        opacity = self.nodes[backing]["properties"]["opacity"]["value"]
        node = vector(ident, {**FULL, "display": keyword("none"), "opacity": number(opacity), "pointer-events": keyword("none")},
                      [path("shade", whole, fill=solid([0, 0, 0, alpha]))])
        siblings = parent["children"]
        siblings.insert(next(index for index, child in enumerate(siblings) if child["id"] == backing) + 1, node)
        self.nodes[ident] = node
        self.show(ident, "block", "none")
        for timeline in self.result.get("timelines", []):
            timeline["tracks"] += [{**entry, "node": ident} for entry in timeline["tracks"]
                                   if entry["node"] == backing and entry["property"] == "opacity"]
            self.owned.update((entry["node"], entry["property"]) for entry in timeline["tracks"] if entry["node"] == ident)

    def grow(self, ident: str, factor: float, keep_place: bool = False) -> None:
        """Every dp length in `ident`'s subtree `factor` times larger in a
        tall view; with `keep_place` its own left, top, right and bottom
        stay for place() or block() to set."""
        def walk(node: dict, parent: tuple, root: bool) -> None:
            props = node.get("properties", {})
            size = []
            for key, extent in (("width", parent[0]), ("height", parent[1])):
                found = props.get(key) or {}
                unit = found.get("unit") if found.get("type") == "length" else None
                size.append(found["value"] if unit == "dp" else found["value"] * extent / 100 if unit == "%" and extent else None)
            if node.get("type") == "vector" and size[0] and size[1]:
                _in_fractions(node, *size)
            for key, found in props.items():
                if not isinstance(found, dict) or found.get("type") != "length" or found.get("unit") != "dp" or not found["value"]:
                    continue
                if root and keep_place and key in ("left", "top", "right", "bottom", "margin-top", "margin-left"):
                    continue
                if (node["id"], key) in self.owned:
                    continue
                self.bind(node["id"], key, found["value"] * factor)
            for child in node.get("children", []):
                walk(child, tuple(size), False)
        walk(self.nodes[ident], (None, None), True)

    def block(self, ids, origin: tuple, at: tuple, factor: float) -> None:
        """The absolutely placed parts `ids`, of one coordinate frame, scaled
        `factor` times about `origin` and moved so that it lands on `at`
        (canvas dp) in a tall view."""
        for ident in ids:
            wide = self.dp(ident, "left")
            if wide is None:
                raise SystemExit(f"{ident} has no dp left for the tall layout to move")
            self.bind(ident, "left", at[0] + (wide - origin[0]) * factor)
            self.top(ident, at[1] + (self.canvas_top(ident) - origin[1]) * factor)
            self.grow(ident, factor, keep_place=True)

    def dialogs(self, factor: float) -> None:
        """Every modal's dialog `factor` times larger in a tall view, or as
        large as the column takes: its stage scales about the view's middle,
        so that the dialog grows as one with the lengths its text size
        already binds. A dialog placed from the canvas's top (one whose height
        follows its text) moves down with the stage in any view taller than
        the canvas, to the view's middle where a centred dialog stands."""
        for node in list(self.nodes.values()):
            if not node.get("modal"):
                continue
            stage = self.nodes.get(f"{node['id']}-stage")
            dialog = self.nodes.get(f"{node['id']}-dialog")
            if stage is None or dialog is None or (stage["id"], "transform") in self.owned:
                raise SystemExit(f"the tall layout cannot scale {node['id']}'s stage")
            scale = round(min(factor, CONTENT_W / self.dp(dialog["id"], "width")), 4)
            stage["properties"].setdefault("transform", {"type": "transform", "unit": "dp", "value": [0, 0, 1, 1, 0]})
            self.owned.add((stage["id"], "transform"))
            sized = {"op": "select", "args": [TALL, scale, 1]}
            shift = 0
            if self.dp(dialog["id"], "top") is not None:
                below = {"op": "-", "args": [{"op": "/", "args": [VIEW_HEIGHT, 2]}, 360]}
                shift = {"op": "*", "args": [sized, {"op": "max", "args": [0, below]}]}
            self.bindings.append({"id": f"tall.{stage['id']}.transform", "node": stage["id"], "property": "transform",
                                  "value": [0, shift, sized, sized, 0]})

    def block_bottom(self, ids, origin: tuple, at: tuple, factor: float) -> None:
        """As block() for parts the narrow-view fit keeps at the view's foot:
        `origin` and `at` give a left and a distance up from the view's bottom
        edge, so the block grows upward from where it stands."""
        for ident in ids:
            for prop, source, target in (("left", origin[0], at[0]), ("bottom", origin[1], at[1])):
                wide = self.dp(ident, prop)
                if wide is None:
                    raise SystemExit(f"{ident} has no dp {prop} for the tall layout to move")
                self.bind(ident, prop, target + (wide - source) * factor)
            self.grow(ident, factor, keep_place=True)
