
(() => {
const { svgEl } = window.__atlas;
const F_C = "Chakra Petch, Arial Narrow, sans-serif";

/* Preset schematics at 16:9 (section 13.9, 14.4): the center screen is x 20-140; the tinted strips are the
   leading and trailing screens that horizontal expansion adds. Boxes are stock u scaled by 0.1875. */
const pr = document.getElementById("presets");
if (pr) {
  const u = v => 20 + v * .1875, uy = v => v * .1875;
  const G = "#B3D06E", N = "#FFFF8D", O = "#FF8000";
  const box = (x, y, w, h, c = G, a = .45) => [x, y, w, h, c, a];
  const status = cx => [box(u(cx), uy(429), 23.4, 7.5), box(u(cx + 177), uy(429), 23.4, 7.5), box(u(cx + 313), uy(429), 23.4, 7.5)];
  /* The status bar enlarged about its bottom-centre anchor (Competitive: 1.15 within the same width limit). */
  const big = (cx, k) => status(cx).map(([x, y, w, h, c, a]) => [80 + (x - 80) * k, 90 - (90 - y) * k, w * k, h * k, c, a]);
  /* ui_hudLayout: 0 Classic (the stock HUD itself), 1 Remastered, 2 Minimal, 3 Competitive (multiplayer; elsewhere it
     is Remastered). Touch is not a layout: on Android the HUD's top edge drops below the touch controls and, in
     portrait, the bottom edge rises above the thumb deck (RetainedHud.cpp). */
  const P = [
    ["Classic", "Layout 0: the stock HUD, its 4:3 rectangles centered; nothing expands", [...status(13), box(u(21), uy(401), 43, 4.5, "#fff", .3), box(u(468), uy(6), 31, 5, O, .5)]],
    ["Remastered", "Layout 1: status bar centered, strip and pickups above it; edge groups move out with their edge", [...status(101), box(u(262), uy(402), 22, 4, "#fff", .3), box(u(158), uy(350), 61.5, 4.9, "#fff", .18),
      box(160 - 32.3, uy(6), 31, 5, O, .5), box(1, uy(32), 38, 11, G, .3), box(160 - 34, uy(55), 33, 6, G, .3), box(160 - 14, uy(330), 12, 22, "#fff", .25)]],
    ["Minimal", "Layout 2: the gauges where Remastered puts them and the crosshair; prompts, the save notice, chat and votes stay", [...status(101)]],
    ["Competitive", "Layout 3, multiplayer: the match bar at the top center, team status on the leading edge, the status bar 15% larger", [...big(101, 1.15), box(u(250), uy(4), 26, 12, N, .45), box(u(146), uy(4), 18, 12, "#6AA42B", .6), box(u(355), uy(4), 18, 12, "#FF7B04", .6), box(1, uy(150), 30, 26, G, .3)]],
    ["Touch", "Not a layout: on Android the top edge drops below the touch controls, and in portrait the bottom edge rises above the thumb deck", [...status(101), box(160 - 32.3, uy(40), 31, 5, O, .5)]],
  ];
  P.forEach(([name, desc, boxes]) => {
    const fig = document.createElement("figure");
    const s = svgEl("svg", { viewBox: "0 0 160 90", role: "img", "aria-label": `${name} HUD preset` });
    if (name !== "Classic") { svgEl("rect", { x: 0, y: 0, width: 20, height: 90, fill: G, "fill-opacity": .07 }, s); svgEl("rect", { x: 140, y: 0, width: 20, height: 90, fill: G, "fill-opacity": .07 }, s); }
    svgEl("path", { d: "M20 0V90M140 0V90", stroke: "rgba(255,255,255,.16)", "stroke-dasharray": "2 2", "stroke-width": .6 }, s);
    boxes.forEach(([x, y, w, h, c, a]) => svgEl("rect", { x, y, width: w, height: h, rx: .6, fill: c, "fill-opacity": a }, s));
    svgEl("path", { d: "M80 42V48M77 45H83", stroke: "#fff", "stroke-width": .8 }, s);
    if (name === "Touch") { svgEl("circle", { cx: 22, cy: 66, r: 12, fill: "none", stroke: G, "stroke-opacity": .6 }, s);
      [[140, 66, 9], [124, 74, 6], [148, 50, 6], [12, 6, 4], [148, 6, 4]].forEach(([x, y, r]) => svgEl("rect", { x: x - r, y: y - r, width: 2 * r, height: 2 * r, fill: "none", stroke: G, "stroke-opacity": .6 }, s)); }
    if (name === "Competitive") svgEl("text", { x: 80, y: 7.6, "text-anchor": "middle", "font-family": F_C, "font-weight": 600, "font-size": 5, fill: "#10140a" }, s).textContent = "7:12";
    const cap = document.createElement("figcaption"); cap.innerHTML = `<b>${name.toUpperCase()}</b>${desc}`;
    fig.append(s, cap); pr.appendChild(fig);
  });
}
})();
