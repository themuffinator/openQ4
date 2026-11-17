
(() => {
const A = window.__atlas, { svgEl } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_S = "Oxanium, Chakra Petch, sans-serif";
const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter";
const reduceMQ = matchMedia("(prefers-reduced-motion: reduce)");
let reduce = reduceMQ.matches;
/* Section 4 tokens, as tools/ui/build_retained_screens.py names them. */
const C = { marker: "#909A49", orange: "#E38900", value: "#FFBE23", rail: "#CCCC51", railCard: "#A0A040", glow: "#616F26", rim: "#626934",
  plinth: "#3E4A21", picture: "#414624", objHead: "#FF8000", objMark: "#B0CD6B", stats: "#8C947A", check: "#B5C784", save: "#B8C29E" };
const ST = { read: "#FCFFC8", sel: "#FFCC00", line: "#F59512", fill: "#FF9000", bar: "#FF8000", light: "#6B2A0E", grid: "#FFB060",
  plinth: "#4A2410", card: "#0A0503", stats: "#C9A27A" };
let uid = 0; const id = p => `hm${p}${++uid}`;

/* The Quake emblem (q4logo_alpha, 512x512, traced) in its stock 260x260 u window at 380,125. */
const EMBLEM = "M91.28 3.55 84.55 7.11 77.31 11.81 70.46 17.27 66.65 20.82 61.45 26.41 57.13 31.87 52.81 38.34 49.26 44.69 46.34 51.29 44.18 57.26 42.66 62.59 41.13 69.95 40.12 79.60 40.12 89.50 41.13 99.02 42.15 104.23 44.18 111.72 47.23 119.84 49.51 124.54 47.23 130.51 47.10 131.78 53.45 141.43 60.68 149.93 68.17 149.55 72.24 153.23 76.81 156.79 77.57 163.64 78.20 164.40 85.06 168.47 95.09 173.04 97.88 174.05 98.64 174.05 104.10 170.50 104.74 170.37 110.96 172.02 120.48 173.42 120.86 173.80 120.86 212.77 121.11 213.28 121.37 217.98 122.38 225.98 124.92 239.31 129.11 256.57 129.75 257.33 130.25 256.57 134.95 236.89 136.98 225.98 138.00 217.98 138.25 213.28 138.51 212.77 138.51 173.80 138.89 173.42 146.50 172.40 154.88 170.37 160.72 174.05 161.48 174.05 171.26 169.99 177.23 166.82 181.67 163.90 182.56 156.79 191.19 149.55 198.68 149.93 205.54 141.93 212.27 131.78 212.27 130.89 209.85 124.54 212.14 119.84 215.19 111.72 217.22 104.23 218.23 99.02 219.25 89.50 219.25 79.60 218.23 69.95 215.69 58.91 212.14 49.13 208.08 40.88 204.90 35.67 200.71 29.83 193.86 21.96 188.91 17.27 184.59 13.71 174.81 7.11 167.71 3.55 167.83 3.94 168.34 3.94 168.72 4.44 173.93 7.49 180.78 12.70 186.24 17.77 191.45 23.61 196.52 30.72 200.08 36.82 203.12 43.54 205.66 50.91 207.57 59.03 208.58 67.54 208.71 75.54 208.20 82.14 207.06 89.12 205.54 95.09 203.51 101.05 199.57 109.69 194.49 117.94 188.27 125.68 181.80 132.03 174.81 137.49 168.21 141.68 158.44 146.25 148.92 149.30 138.89 151.07 138.51 150.69 138.51 128.35 137.87 124.54 136.47 118.96 134.44 112.86 130.38 103.09 129.62 102.32 128.98 103.09 124.92 112.86 122.89 118.96 121.49 124.54 120.86 128.35 120.86 150.69 120.35 151.07 114.51 150.19 106.39 148.15 99.40 145.62 91.15 141.68 82.77 136.22 76.43 131.02 71.22 125.81 67.54 121.49 64.87 117.94 60.94 111.72 58.78 107.78 55.86 101.05 52.69 90.90 50.91 79.60 50.65 70.08 51.67 59.79 53.83 50.53 57.26 41.13 61.95 32.12 67.41 24.25 74.27 16.63 82.39 9.65 87.60 6.09 91.79 3.68Z";
A.EMBLEM = EMBLEM;
/* QUAKE 4 in the q4text letterforms (1024x256 texels), the trace scripts/init.js draws on the initializing screen. The
   title's wordmark stretches the texture over 376x92 u at 6,119 u (wordmark, fit fill). */
const Q4TEXT = window.__atlas.Q4TEXT;
const WORDMARK = "translate(6 119) scale(.36719 .35938)";

/* Measured band silhouettes (Appendix B.2) in their home positions, and the Strogg family's version: the same frame
   with 30-degree shoulders, downward teeth and a stepped riser in place of the 45-degree notches. */
const MARINE_TOP = [[-400, 89.7], [-297.9, 89.7], [-283.7, 103.8], [30.7, 103.8], [40.9, 94.7], [76.6, 94.7], [85.8, 103.8], [127.6, 103.8], [137.8, 94.7],
  [172.5, 94.7], [181.7, 103.8], [223.5, 103.8], [233.7, 94.7], [268.4, 94.7], [277.6, 103.8], [315.4, 103.8], [352.1, 68.5], [900, 68.5]];
const MARINE_BTM = [[-399, 421.5], [-232.7, 421.5], [-222.5, 430.6], [-28.6, 430.6], [2.1, 400.4], [344.9, 400.4], [389.8, 443.7], [582.7, 443.7], [591.9, 434.6], [900, 434.6]];
const STROGG_TOP = [[-400, 92], [-300, 92], [-283, 102], [24, 102], [41, 112], [86, 112], [103, 102], [150, 102], [167, 112], [212, 112], [229, 102], [308, 102],
  [327, 91], [334, 91], [352, 66], [520, 66], [537, 76], [578, 76], [595, 66], [900, 66]];
const STROGG_BTM = [[-400, 424], [-240, 424], [-223, 434], [-26, 434], [-8, 424], [0, 400], [334, 400], [351, 410], [372, 410], [394, 444], [580, 444], [597, 434], [900, 434]];
const poly = pts => "M" + pts.map(p => p.join(" ")).join("L");

/* Type: the atlas's stand-ins for the shipped faces, sized and tracked to the engine captures (1280x720, 1.5 dp a u). */
const T = {
  title: { family: F_M, size: 8.4, spacing: 1.75, fill: "#fff", alpha: .5 },          // GAME PAUSED: Marine 18 dp at 0.50
  link: { family: F_M, size: 7.4, spacing: 1.1 },                                     // plinth links: Marine 16 dp
  verb: { family: F_M, size: 6.4, spacing: 1.35, fill: "#fff", alpha: .8 },             // prompt verbs: Marine 14 dp
  key: { family: F_L, size: 8.4, weight: 700, spacing: 0, fill: "#0A0D09", alpha: 1 },   // keycaps: Lowpixel 13 dp
  message: { family: F_L, size: 10.8, weight: 600, spacing: .25, fill: "#fff", alpha: .8 },// message line: Lowpixel 17 dp
  saveTitle: { family: F_L, size: 10.8, weight: 600, spacing: .2, fill: "#fff", alpha: .8 },
  saveDetail: { family: F_L, size: 8.9, weight: 600, spacing: .2, fill: C.save, alpha: 1 },
  head: { family: F_M, size: 6.4, spacing: 1.3, fill: "#fff", alpha: .9 },             // card header: Marine 14 dp
  name: { family: F_M, size: 8.6, spacing: 1.45, fill: "#fff", alpha: .9 },            // level: Marine 20 dp
  detail: { family: F_L, size: 10.6, weight: 600, spacing: .2, fill: "#fff", alpha: .6 },  // difficulty: Lowpixel 17 dp
  objHead: { family: F_M, size: 6.4, spacing: .9, fill: C.objHead, alpha: 1 },          // >>MISSION OBJECTIVES: Marine 14 dp
  objective: { family: F_L, size: 8.2, weight: 600, spacing: .2, fill: "#fff", alpha: .9 }, // Lowpixel 13 dp
  stats: { family: F_L, size: 7.1, weight: 600, spacing: .2, fill: C.stats, alpha: 1 },     // Lowpixel 11 dp
  score: { family: F_M, size: 7.2, spacing: 1.2, fill: "#fff", alpha: .85 },           // Marine 16 dp
  scoreLabel: { family: F_L, size: 8.2, weight: 600, spacing: .2, fill: C.stats, alpha: 1 },
};
const ctx = document.createElement("canvas").getContext("2d");
const measure = (s, t) => { ctx.font = `${t.weight || 400} ${t.size}px ${t.family}`; return ctx.measureText(s).width + (t.spacing || 0) * s.length; };
const text = (parent, s, x, y, t, extra = {}) => {
  const n = svgEl("text", { x, y, "font-family": t.family, "font-size": t.size, "font-weight": t.weight || 400, "letter-spacing": t.spacing || 0,
    fill: t.fill || "#fff", "fill-opacity": t.alpha ?? 1, ...extra }, parent);
  n.textContent = s; return n;
};
const grad = (defs, stops, { v = true, user = null } = {}) => {
  const gid = id("g"), g = svgEl("linearGradient", { id: gid, x1: 0, y1: 0, x2: v ? 0 : 1, y2: v ? 1 : 0,
    ...(user ? { gradientUnits: "userSpaceOnUse", x1: user[0], y1: user[1], x2: user[2], y2: user[3] } : {}) }, defs);
  stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${gid})`;
};
const full = (parent, fill, extra = {}) => svgEl("rect", { x: -400, y: 0, width: 1700, height: 480, fill, ...extra }, parent);

/* The lit field (lit_field): the vignette, the additive light band and the reticle grid's 13 u crosses on a 30 u pitch. */
function vignette(parent, defs) { full(parent, grad(defs, [[0, "#000", .97], [.3, "#000", 0], [.7, "#000", 0], [1, "#000", .97]])); }
function lightBand(parent, defs, glow, peak) { full(parent, grad(defs, [[.1, glow, 0], [.5, glow, peak], [.9, glow, 0]]), { style: ADD }); }
function grid(parent, defs, tint = "#fff", arm = 6.5) {
  const pid = id("p"), pat = svgEl("pattern", { id: pid, width: 30, height: 30, patternUnits: "userSpaceOnUse" }, defs);
  svgEl("path", { d: `M15 ${15 - arm}v${2 * arm}M${15 - arm} 15h${2 * arm}`, stroke: tint, "stroke-width": .67 }, pat);
  full(parent, `url(#${pid})`, { opacity: .04 });
}
/* The framing bands with their rim light (framing_bands, rim): ten nested strokes of equal alpha along the inner edge,
   peak 0.37 falling to nothing over 19 u (Strogg: #F59512, 0.34 over 11 u); the opaque band hides their inner halves. */
function bands(parent, family = "marine", { top = family === "strogg" ? STROGG_TOP : MARINE_TOP, btm = family === "strogg" ? STROGG_BTM : MARINE_BTM } = {}) {
  const strogg = family === "strogg", tint = strogg ? ST.line : C.rim, peak = strogg ? .34 : .37, reach = strogg ? 11 : 19, steps = 10;
  const a = 1 - (1 - peak) ** (1 / steps), g = svgEl("g", {}, parent);
  [top, btm].forEach(pts => { for (let i = steps; i > 0; i--) svgEl("path", { d: poly(pts), fill: "none", stroke: tint, "stroke-opacity": a.toFixed(4),
    "stroke-width": (2 * reach * i / steps).toFixed(2), "stroke-linejoin": "round" }, g); });
  svgEl("path", { d: poly(top) + "L900 -200L-400 -200Z", fill: "#000" }, g);
  svgEl("path", { d: poly(btm) + "L900 700L-400 700Z", fill: "#000" }, g);
  if (strogg) {                                              // circuit traces etched in the bands, with their junction dots
    const c = svgEl("g", { stroke: ST.line, "stroke-width": .8, fill: "none", opacity: .32 }, g);
    ["M-300 40H40L60 58H250", "M380 24H520L538 40H700", "M-200 460H120L140 448H300", "M420 470H560L578 458H760"].forEach(d => svgEl("path", { d }, c));
    [[40, 40], [250, 58], [520, 24], [700, 40], [120, 460], [300, 448], [560, 470]].forEach(([x, y]) => svgEl("circle", { cx: x, cy: y, r: 1.6, fill: ST.line }, c));
  }
  return g;
}

/* The prompt bar in the bottom band's raised trailing section (prompt_bar): per prompt the keyboard's keycap, the key
   in Lowpixel on a 22 dp plate with a 45-degree lower-leading cut at 0.85, 6 dp, its verb, 22 dp; it ends 12 u inside
   the view's trailing edge, 447 u down. `right` is that edge in u. */
function promptBar(parent, items, { right = 735, top = 447, strogg = false } = {}) {
  const h = 14.67, pad = 5.33, gap = 4, after = 14.67, g = svgEl("g", { class: "hmprompts" }, parent);
  const verb = strogg ? { family: F_S, size: 8.6, weight: 600, spacing: .9, fill: ST.read, alpha: .8 } : T.verb;
  const widths = items.map(([k, v]) => { const kw = measure(k, T.key) + 2 * pad; return [kw, kw + gap + measure(v, verb)]; });
  let x = right - widths.reduce((s, [, w]) => s + w + after, 0);
  items.forEach(([k, v], i) => {
    const [kw, w] = widths[i];
    svgEl("path", { d: `M${x} ${top}h${kw}v${h}H${x + 4}l-4 -4Z`, fill: strogg ? ST.sel : "#fff", opacity: .85 }, g);
    text(g, k, x + pad, top + h / 2 + 3, T.key, { opacity: .85 });
    const vt = text(g, v, x + kw + gap, top + h / 2 + (strogg ? 3 : 2.3), verb);
    if (strogg) vt.setAttribute("transform", skew(x + kw + gap, top + h / 2 + 3));
    x += w + after;
  });
  return g;
}

/* The link plinth (plinth): 24-361 u by 403-422 u, a 7.5 u lower-leading cut and a 12.8 u trailing cut, #3E4A21 at
   0.30; its links run in a row from 32 u, a 4.5 u marker in a 7 u slot before each and 14 u after. */
function plinth(parent, links, { strogg = false, focus = null } = {}) {
  const x = 24, y = 403.3, w = 337.2, h = 18.8;
  svgEl("path", { d: strogg ? `M${x} ${y}h${w - 10.7}l10.7 12.5V${y + h}H${x + 7.2}L${x} ${y + h - 6.3}Z` : `M${x} ${y}h${w - 12.8}l12.8 ${h}H${x + 7.47}L${x} ${y + h - 7.47}Z`,
    fill: strogg ? ST.plinth : C.plinth, "fill-opacity": .3 }, parent);
  const lt = strogg ? { family: F_S, size: 10.7, weight: 600, spacing: 1 } : T.link;
  let lx = 32;
  return links.map(t => {
    const g = svgEl("g", { class: "sl" + (t === focus ? " f" : ""), tabindex: 0, role: "button", "aria-label": t }, parent);
    svgEl("path", { d: strogg ? `M${lx + 1.2} 410.25h4.5l-1.2 4.5h-4.5z` : `M${lx} 410.25h4.5v4.5z`, class: "mkr" }, g);
    const n = svgEl("text", { x: lx + 7, y: 415.2, ...(strogg ? { transform: skew(lx + 7, 415.2) } : { "letter-spacing": T.link.spacing }) }, g);
    n.textContent = t; lx += 7 + measure(t, lt) + 14;
    return g;
  });
}

/* A dark navigation plate (navigation_plate): b1_dark bleeding off the leading edge on the 30 u pitch, its band 2.8-27.2 u
   into the row, opaque to 44 % of 413 u, half at 62 %, gone by 88 %, with the #CCCC51 rail along its foot; the marker at
   35 u, the label at 44 u. Focus adds the orange inset rail to 232 u and a soft additive glow beside the marker. */
function navPlate(parent, defs, y, label, { focused = false } = {}) {
  const g = svgEl("g", { class: "np" + (focused ? " f" : ""), tabindex: 0, role: "button", "aria-label": label }, parent);
  const fade = grad(defs, [[0, "#000"], [.44, "#000"], [.62, "#000", .5], [.88, "#000", 0]], { user: [0, 0, 413, 0] });
  const rail = grad(defs, [[0, C.rail], [.44, C.rail], [.62, C.rail, .5], [.88, C.rail, 0]], { user: [0, 0, 413, 0] });
  svgEl("rect", { x: -600, y: y + 2.8, width: 1013, height: 24.4, fill: fade, class: "pf" }, g);
  svgEl("path", { d: `M-600 ${y + 26.7}H413`, stroke: rail, "stroke-width": 1, class: "pf" }, g);
  svgEl("path", { d: `M-600 ${y + 25.07}H232`, stroke: C.orange, "stroke-width": .8, class: "fx", opacity: 0 }, g);
  svgEl("rect", { x: 13.3, y: y + 1.47, width: 33.4, height: 27.06, fill: grad(defs, [[0, C.glow, 0], [.5, C.glow, .5], [1, C.glow, 0]], { v: false }),
    class: "fx", opacity: 0, style: ADD }, g);
  svgEl("path", { d: `M35 ${y + 11.8}h6v6z`, class: "mkr" }, g);
  svgEl("text", { x: 44, y: y + 19.6, class: "nl" }, g).textContent = label;
  return g;
}

/* The Strogg plate (strogg_navigation_plate): the same bleed ending in a 30-degree shoulder from 341 u to 357 u, 12 u
   down; the #F59512 rail runs along its foot, up its trailing edge and back over the shoulder to 285 u. */
function stroggPlate(parent, defs, y, label, { focused = false } = {}) {
  const g = svgEl("g", { class: "np" + (focused ? " f" : ""), tabindex: 0, role: "button", "aria-label": label }, parent);
  const fade = grad(defs, [[0, "#000"], [.44, "#000"], [.62, "#000", .5], [.88, "#000", 0]], { user: [0, 0, 357, 0] });
  const rail = grad(defs, [[0, ST.line, .9], [.55, ST.line, .9], [1, ST.line, .45]], { user: [0, 0, 357, 0] });
  svgEl("path", { d: `M-600 ${y + 2.8}H341L357 ${y + 12}V${y + 27.2}H-600Z`, fill: fade, class: "pf" }, g);
  svgEl("path", { d: `M-600 ${y + 26.7}H356.5V${y + 12}L341 ${y + 3.3}H285`, fill: "none", stroke: rail, "stroke-width": 1, class: "pf" }, g);
  svgEl("path", { d: `M-600 ${y + 25.07}H232`, stroke: ST.sel, "stroke-width": .8, class: "fx", opacity: 0 }, g);
  svgEl("path", { d: `M36.4 ${y + 11.8}h6l-1.6 6h-6z`, class: "mkr" }, g);
  return g;
}

/* The title's wordmark: q4text drawn additively (white, fit fill), lettering with the texture's soft halo. */
function wordmark(parent, defs) {
  const fid = id("w"), f = svgEl("filter", { id: fid, x: "-10%", y: "-30%", width: "120%", height: "160%" }, defs);
  svgEl("feGaussianBlur", { stdDeviation: 2.2 }, f);
  const g = svgEl("g", { class: "hmword", style: ADD }, parent);
  const halo = svgEl("g", { transform: WORDMARK, filter: `url(#${fid})`, fill: "#E8B070", opacity: .55 }, g);
  const core = svgEl("g", { transform: WORDMARK, fill: "#F2EDDF" }, g);
  Object.values(Q4TEXT).forEach(d => { svgEl("path", { d, "fill-rule": "evenodd" }, halo); svgEl("path", { d, "fill-rule": "evenodd" }, core); });
  return g;
}

/* The emblem (emblem): the stock darkening blend, rgb(168,166,57) multiplied over the lit field, and a rim at 0.20.
   Over it the emblem is charged: every 6.4 s a charge climbs the outline from the spike's tip in 1700 ms (a bright front,
   a 64 u tail), the whole rim flares from 1150 ms over 900 ms, and a reticle indexes about the ring's center (129.7,89.5):
   three 84-degree arcs at 104 u stepping 30 degrees (hold 2700 ms, move 500 ms) and a gauge of ticks at 97 u stepping
   -10 degrees (hold 1380 ms, move 220 ms). Reduced motion keeps the rim and a still reticle. */
const RETICLE = [[104, 30, 4, 2700, 500], [97, -10, 9, 1380, 220]];
function emblem(parent, defs) {
  const g = svgEl("g", { transform: "translate(380 125)", class: "hmemblem" }, parent);
  svgEl("path", { d: EMBLEM, fill: "rgb(168,166,57)", style: "mix-blend-mode:multiply" }, g);
  svgEl("path", { d: EMBLEM, fill: "none", stroke: C.rail, "stroke-width": .9, "stroke-opacity": .2, "stroke-linejoin": "round", style: ADD }, g);
  const cx = 129.7, cy = 89.5, pt = (r, a) => { const t = a * Math.PI / 180; return [(cx + r * Math.sin(t)).toFixed(2), (cy - r * Math.cos(t)).toFixed(2)]; };
  const rings = RETICLE.map(([r], k) => {
    let d = "";
    if (k === 0) for (let p = 0; p < 3; p++) {
      const a = p * 120 + 18, [x0, y0] = pt(r, a), [x1, y1] = pt(r, a + 84);
      d += `M${x0} ${y0}A${r} ${r} 0 0 1 ${x1} ${y1}`;
      [a, a + 84].forEach(at => { const [ax, ay] = pt(r, at), [bx, by] = pt(r - 4, at); d += `M${ax} ${ay}L${bx} ${by}`; });
    } else for (let p = 0; p < 36; p++) { const [ax, ay] = pt(r, p * 10), [bx, by] = pt(r + (p % 9 ? 1.8 : 4.5), p * 10); d += `M${ax} ${ay}L${bx} ${by}`; }
    return svgEl("path", { d, fill: "none", stroke: C.rail, "stroke-opacity": .2, "stroke-width": .8, style: ADD }, g);
  });
  const surge = svgEl("path", { d: EMBLEM, fill: "none", stroke: C.rail, "stroke-opacity": .5, "stroke-width": 1.6, "stroke-linejoin": "round", opacity: 0, style: ADD }, g);
  const mid = id("m"), mask = svgEl("mask", { id: mid, maskUnits: "userSpaceOnUse", x: -20, y: -80, width: 300, height: 420 }, defs);
  const band = svgEl("rect", { x: -10, y: 300, width: 280, height: 64, fill: grad(defs, [[0, "#fff", 0], [.06, "#fff", 1], [.3, "#fff", .45], [1, "#fff", 0]]) }, mask);
  const charge = svgEl("path", { d: EMBLEM, fill: "none", stroke: C.rail, "stroke-opacity": .85, "stroke-width": 1.07, "stroke-linejoin": "round", mask: `url(#${mid})`, style: ADD }, g);
  return { g, rings, surge, band, charge, cx, cy };
}
const easeOut = x => 1 - (1 - x) ** 4;
const accel = p => p < .5 ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);
function animateEmblem(E, t) {
  if (!E) return;
  const still = reduce;
  E.charge.style.display = E.surge.style.display = still ? "none" : "";
  RETICLE.forEach(([, step, n, hold, move], k) => {
    let a = 0;
    if (!still) { const cyc = n * (hold + move), tt = t % cyc, s = Math.floor(tt / (hold + move)), w = tt - s * (hold + move);
      a = step * s + (w < hold ? 0 : step * easeOut((w - hold) / move)); }
    E.rings[k].setAttribute("transform", `rotate(${a.toFixed(2)} ${E.cx} ${E.cy})`);
  });
  if (still) return;
  const tc = t % 6400;
  E.band.setAttribute("y", (tc < 1700 ? 260 + (-64 - 260) * accel(tc / 1700) : -64).toFixed(2));
  const s = tc < 1150 ? 0 : tc < 1290 ? (tc - 1150) / 140 : tc < 2050 ? 1 - (tc - 1290) / 760 : 0;
  E.surge.setAttribute("opacity", s.toFixed(3));
}

/* ---------------------------------------------------------------------------------------------------------------
   The title signal (section 13.8, Appendix C 1.43; retained-screens.md 13 and 14 November 2026). The pictures, their
   0.30 dimming, the vignette and the light band are one view; a fault redraws it torn: a few wide bands and many thin
   rows shifted sideways (up to 4 % and 8 % of the view), square blocks 24 across held as a 3x3 mosaic of themselves, and
   the red and blue channels parted, further in a torn row; torn rows lift 15 %, held blocks 8 %. A new arrangement every
   55 ms (backdrop-glitch-phase), the port of glsl/retained_glitch.glslfp in vector form. The 8x8 Bayer dither of 1.43
   was taken out of title.rui on 14 November, so none is drawn. */
const SIGNAL_LEAD = 150, SIGNAL_STEP = 55, SLOTS = 5, PERIOD = 5000 * SLOTS;
const SIGNAL_CHANGE = [[-120, 0], [-40, .5], [60, .85], [200, .55], [330, .22], [520, 0]];
const SIGNAL_REVEAL = [[0, 0], [60, .4], [150, .4], [220, .8], [420, 1]];
const SIGNAL_SLIPS = [[3300, .22, 110], [12600, .3, 90], [12780, .2, 70], [17400, .18, 90], [23100, .26, 120]];
const BURSTS = { open: [[0, 0], [1, .85], [90, .6], [180, .3], [250, 0]], depart: [[0, 0], [60, .4], [200, .6], [400, .85], [510, .4], [550, 0]],
  returnHome: [[0, 0], [1, .6], [120, .7], [300, .3], [480, 0]] };
const SCENES = ["mp", "sp", "strogg", "mp", "sp"];   // stand-ins for the five menu levelshots (level_mcc ... level_tram1)
const lerpKeys = (keys, t) => { if (t <= keys[0][0] || t >= keys[keys.length - 1][0]) return 0;
  for (let i = 1; i < keys.length; i++) if (t <= keys[i][0]) { const [a, va] = keys[i - 1], [b, vb] = keys[i]; return va + (vb - va) * (t - a) / (b - a); } return 0; };
const SIGNAL_BURSTS = [...Array(SLOTS).keys()].map(i => SIGNAL_CHANGE.map(([at, v]) => [5000 * i + SIGNAL_LEAD + at, v]))
  .concat(SIGNAL_SLIPS.map(([at, v, l]) => [[at, 0], [at + 20, v], [at + l - 30, v * .8], [at + l, 0]]));
function signalAt(t) {
  const tt = ((t % PERIOD) + PERIOD) % PERIOD;
  for (let i = 0; i < SIGNAL_BURSTS.length; i++) {
    const k = SIGNAL_BURSTS[i];
    for (const off of [0, PERIOD]) { const x = tt + off; if (x > k[0][0] && x < k[k.length - 1][0]) return [lerpKeys(k, x), i * 97 + Math.floor((x - k[0][0]) / SIGNAL_STEP)]; }
  }
  return [0, 0];
}
const hash1 = n => { const s = Math.sin(n) * 43758.5453; return s - Math.floor(s); };
const hash2 = (x, y) => hash1(x * 12.9898 + y * 78.233);

function signalView(parent, defs, scenes = SCENES) {
  const vid = id("view"), wrap = svgEl("g", {}, parent), view = svgEl("g", { id: vid }, wrap);
  const pics = svgEl("g", {}, view), shots = scenes.map(k => { const s = svgEl("g", { opacity: 0 }, pics); A.levelshot?.(s, k, { x0: -107, y0: 0, W: 854, H: 480 }); return s; });
  shots[0].setAttribute("opacity", 1);
  full(view, "#000", { "fill-opacity": .3, class: "hmdim" });        // field-dim: black 0.30 under their light
  vignette(view, defs); lightBand(view, defs, C.glow, .7);
  // the parted channels: R and B offset against G (the filter's dx is set per arrangement)
  const split = (name, slope = 1) => {
    const fid = id(name), f = svgEl("filter", { id: fid, x: -120, y: 0, width: 1000, height: 480, filterUnits: "userSpaceOnUse", "color-interpolation-filters": "sRGB" }, defs);
    const lift = svgEl("feComponentTransfer", { in: "SourceGraphic", result: "l" }, f);
    ["feFuncR", "feFuncG", "feFuncB"].forEach(n => svgEl(n, { type: "linear", slope }, lift));
    svgEl("feColorMatrix", { in: "l", type: "matrix", values: "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0", result: "r" }, f);
    svgEl("feColorMatrix", { in: "l", type: "matrix", values: "0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 1 0", result: "g" }, f);
    svgEl("feColorMatrix", { in: "l", type: "matrix", values: "0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 1 0", result: "b" }, f);
    const or = svgEl("feOffset", { in: "r", dx: 1, result: "ro" }, f), ob = svgEl("feOffset", { in: "b", dx: -1, result: "bo" }, f);
    svgEl("feComposite", { in: "ro", in2: "g", operator: "arithmetic", k2: 1, k3: 1, result: "rg" }, f);
    svgEl("feComposite", { in: "rg", in2: "bo", operator: "arithmetic", k2: 1, k3: 1 }, f);
    return { url: `url(#${fid})`, set: dx => { or.setAttribute("dx", dx.toFixed(2)); ob.setAttribute("dx", (-dx).toFixed(2)); }, lift };
  };
  const F = { base: split("fb"), wide: split("fw"), thin: split("ft", 1) };
  // a held block: one sample a mosaic cell, spread over the cell (the 3x3 mosaic of a block 854/24 u square)
  const cell = 854 / 24 / 3, mid = id("mos"), mf = svgEl("filter", { id: mid, x: -107, y: 0, width: 854, height: 480, filterUnits: "userSpaceOnUse", primitiveUnits: "userSpaceOnUse" }, defs);
  svgEl("feFlood", { x: -107 + cell / 2 - 1, y: cell / 2 - 1, width: 2, height: 2, "flood-color": "#000", result: "dot" }, mf);
  svgEl("feComposite", { in: "dot", x: -107, y: 0, width: cell, height: cell, result: "tile" }, mf);
  svgEl("feTile", { in: "tile", result: "grid" }, mf);
  svgEl("feComposite", { in: "SourceGraphic", in2: "grid", operator: "in", result: "s" }, mf);
  svgEl("feMorphology", { in: "s", operator: "dilate", radius: cell / 2 }, mf);
  const fault = svgEl("g", { class: "hmfault", "pointer-events": "none" }, parent);
  return { wrap, view, vid, pics, shots, fault, F, mosaic: `url(#${mid})`, slot: 0 };
}
/* One arrangement of the fault at strength s and phase. */
function drawFault(S, defs, s, phase) {
  S.fault.innerHTML = "";
  if (s <= 0) { S.wrap.style.display = ""; return; }
  const W = 854, H = 480, seed = (phase % 251) + 1, use = (parent, dx = 0) => svgEl("use", { href: `#${S.vid}`, x: dx }, parent);
  const cdefs = svgEl("defs", {}, S.fault);
  const clip = rects => { const cid = id("c"), cp = svgEl("clipPath", { id: cid }, cdefs); rects.forEach(([x, y, w, h]) => svgEl("rect", { x, y, width: w, height: h }, cp)); return `url(#${cid})`; };
  S.wrap.style.display = "none";
  const base = svgEl("g", { filter: S.F.base.url }, S.fault); use(base); S.F.base.set(.0012 * s * W);
  const wideN = 5 + Math.floor(hash1(seed * 1.7) * 9), thinN = 38 + Math.floor(hash1(seed * 3.1) * 40);
  const torn = [];
  for (let r = 0; r < wideN; r++) if (hash2(r, seed) >= 1 - .45 * s) torn.push(["wide", r * H / wideN, H / wideN, (hash2(r, seed + 5) - .5) * .08 * s * W]);
  for (let r = 0; r < thinN; r++) if (hash2(r, seed + 17) >= 1 - .30 * s) torn.push(["thin", r * H / thinN, H / thinN, (hash2(r, seed + 9) - .5) * .16 * s * W]);
  S.F.wide.set((.0012 + .004) * s * W); S.F.thin.set((.0012 + .004) * s * W);
  S.F.thin.lift.querySelectorAll("*").forEach(n => n.setAttribute("slope", 1 + .15 * s));
  torn.forEach(([kind, y, h, dx]) => { const g = svgEl("g", { "clip-path": clip([[-400, y, 1700, h]]) }, S.fault); use(svgEl("g", { filter: S.F[kind].url }, g), dx); });
  const rows = Math.max(1, Math.floor(24 * H / W + .5)), bw = W / 24, bh = H / rows, held = [];
  for (let by = 0; by < rows; by++) for (let bx = 0; bx < 24; bx++) if (hash2(bx + seed, by + seed * .37) >= 1 - .12 * s) held.push([-107 + bx * bw, by * bh, bw, bh]);
  if (held.length) { const g = svgEl("g", { "clip-path": clip(held), style: "filter:brightness(1.08)" }, S.fault); use(svgEl("g", { filter: S.mosaic }, g)); }
}

/* Strogg text (section 8; section 14.6): the same words in runes and in R_Strogg on one baseline, the runes 1.2x the
   cap height; the credits' translation cross-fades them, pops both 6 % and settles, and a heading gets the scan bar. */
let jobs = [], looping = false;
const run = (delay, dur, fn) => { jobs.push([performance.now() + delay, dur, fn]); if (!looping) { looping = true; requestAnimationFrame(tickJobs); } };
function tickJobs(now) {
  jobs = jobs.filter(([t0, dur, fn]) => { if (now < t0) return true; fn(dur ? Math.min(1, (now - t0) / dur) : 1); return now < t0 + dur; });
  if (jobs.length) requestAnimationFrame(tickJobs); else looping = false;
}
const mix = (a, b, p) => { const h = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16)); const x = h(a), y = h(b); return `rgb(${x.map((v, i) => Math.round(v + (y[i] - v) * p)).join(",")})`; };
const skew = (x, y) => `translate(${x} ${y}) skewX(11.4) translate(${-x} ${-y})`;
function slabel(parent, s, x, y, size, { color = ST.read, weight = 600, spacing = .6, cls, alpha = 1 } = {}) {
  const g = svgEl("g", {}, parent);
  const rune = A.runes ? A.runes(g, s.toUpperCase(), x, y, size * .7 * 1.2, { fill: ST.sel, opacity: 0 }) : svgEl("g", {}, g);
  const latin = svgEl("text", { x, y, "font-family": F_S, "font-weight": weight, "font-size": size, "letter-spacing": spacing, fill: color, "fill-opacity": alpha, transform: skew(x, y) }, g);
  if (cls) latin.setAttribute("class", cls);
  latin.textContent = s;
  return { g, rune, latin, x, y, size, color };
}
function translate(L, delay, dur, { white = false, bar = null } = {}) {
  if (reduce || !A.runes || !dur) { L.rune.setAttribute("opacity", 0); L.latin.setAttribute("opacity", 1); return; }
  L.rune.setAttribute("opacity", .8); L.latin.setAttribute("opacity", 0);
  const cx = L.x, cy = L.y - L.size * .35, pop = .06;
  run(delay, dur, p => {
    L.rune.setAttribute("opacity", .8 * (1 - p)); L.rune.setAttribute("transform", `translate(${cx} ${cy}) scale(${1 + pop * (1 - p)}) translate(${-cx} ${-cy})`);
    L.latin.setAttribute("opacity", p); L.latin.setAttribute("transform", `translate(${cx} ${cy}) scale(${1 + pop * .9 * (1 - p)}) translate(${-cx} ${-cy}) ${skew(L.x, L.y)}`);
    if (white) L.latin.setAttribute("fill", "#FFFFFF");
  });
  if (white) run(delay + dur + 60, 150, p => L.latin.setAttribute("fill", mix("#FFFFFF", L.color, p)));
  if (bar && A.creditsBar) scanBar(bar, delay, bar.slide || dur * .3);
}
function scanBar({ parent, len = 150, h, y, from, to }, delay, slide) {
  const g = A.creditsBar(parent, len, h, ST.bar); g.setAttribute("opacity", 0);
  const dim = slide * 3.33;
  run(delay, dim, p => { const q = A.accelDecel ? A.accelDecel(p * dim, slide * .4, slide * .6) : Math.min(1, p * 3.33);
    g.setAttribute("transform", `translate(${from + (to - from) * q} ${y})`); g.setAttribute("opacity", 1 - p); });
  run(delay + dim + 20, 0, () => g.remove());
}

A.homeKit = { EMBLEM, Q4TEXT, WORDMARK, MARINE_TOP, MARINE_BTM, T, measure, text, vignette, lightBand, grid, bands, promptBar, plinth, navPlate,
  wordmark, emblem, animateEmblem, signalView, drawFault, BURSTS, lerpKeys, SIGNAL_STEP, ADD, C };

/* ======================================================================================================== the sheet */
const svg = document.getElementById("homeSvg");
if (!svg) return;
let state = "menu", save = true, focusIdx = null;
const status = document.getElementById("homeStatus");

/* The four screens. Rows are the plates' 30 u windows: the title from 212.2 u (without a save CONTINUE collapses and the
   rows rise a pitch), the pause from 182.2 u, the Arena pause from 212.2 u. */
const MESSAGE = { CONTINUE: null, "SINGLE PLAYER": "Start the campaign or play the Arena.", "LOAD GAME": "Continue from any saved game.",
  MULTIPLAYER: "Find a server, host a match or play with bots.", SETTINGS: "Controls, display, audio and game options." };
const SAVE = { title: "Trenches checkpoint", detail: "Air Defense Trenches · Corporal · 3:12:05" };
const SCREENS = {
  menu: { top: 212.2, items: () => (save ? ["CONTINUE"] : []).concat(["SINGLE PLAYER", "LOAD GAME", "MULTIPLAYER", "SETTINGS"]),
    links: ["MODS", "UPDATES", "CREDITS", "EXIT"], prompts: [["ENTER", "SELECT"], ["ESCAPE", "EXIT"]] },
  pause: { top: 182.2, title: [134, "GAME PAUSED"], items: () => ["RESUME", "SAVE GAME", "LOAD GAME", "RESTART LEVEL", "OBJECTIVES", "SETTINGS", "QUIT TO MENU"],
    links: ["EXIT"], prompts: [["ENTER", "SELECT"], ["ESCAPE", "RESUME"]], scene: "sp" },
  arena: { top: 212.2, title: [164, "MATCH PAUSED"], items: () => ["RESUME", "RESTART MATCH", "SETTINGS", "LEAVE MATCH"],
    links: [], prompts: [["ENTER", "SELECT"], ["ESCAPE", "RESUME"]], scene: "mp" },
};
SCREENS.strogg = { ...SCREENS.pause, scene: "strogg" };
const LEVEL = {
  pause: { head: "CURRENT MISSION", name: "AIR DEFENSE TRENCHES", detail: "CORPORAL", scene: "sp",
    objectives: [["Destroy the anti-aircraft battery", 0], ["Regroup with Rhino squad at the bunker", 0], ["Fight through the trench network", 1]],
    stats: "0:42:10 in mission · 3:12:05 total", saved: "saved 4 min ago" },
  strogg: { head: "CURRENT MISSION", name: "RECOMPOSITION CENTER", detail: "CORPORAL", scene: "strogg",
    objectives: [["Regroup", 0]], stats: "0:18:40 in mission · 9:44:12 total", saved: "saved just now" },
};
const MATCH = { head: "TIER 2  /  CHALLENGER", name: "THE PROVING GROUNDS", mode: "Deathmatch", map: "Lockdown", clock: "4:12", score: "2nd place with 7" };

let E = null, S = null, sdefs = null, burst = null, openAt = 0, frozen = null;
/* For stills: hold the ambient clock at t ms into the montage (null runs it). */
A.homeKit.hold = t => { frozen = t; burst = null; ambient(performance.now(), true); };
const start = performance.now();
function build(opening = true) {
  svg.innerHTML = "";
  const D = SCREENS[state], strogg = state === "strogg", pause = state !== "menu";
  svg.classList.toggle("strogg", strogg);
  svg.setAttribute("aria-label", { menu: "Main menu in its home state", pause: "Single-player pause menu", strogg: "Strogg single-player pause menu", arena: "Arena pause menu" }[state]);
  const defs = sdefs = svgEl("defs", {}, svg);
  E = S = null;
  if (!pause) {
    // the title: the signal's view (pictures, dimming, vignette, light), the fault over it, then the grid
    S = signalView(svg, defs);
    grid(svg, defs);
    const stage = svgEl("g", { class: "hmstage" }, svg);
    E = emblem(stage, defs); wordmark(stage, defs);
  } else {
    // the paused view, softened (modal.softfocus: a 5 u Gaussian, saturation 0.8) and never dimmed; then the lit field
    const fid = id("soft"), f = svgEl("filter", { id: fid, x: "-2%", y: "-2%", width: "104%", height: "104%", "color-interpolation-filters": "sRGB" }, defs);
    svgEl("feGaussianBlur", { stdDeviation: 5 }, f); svgEl("feColorMatrix", { type: "saturate", values: .8 }, f);
    A.levelshot?.(svgEl("g", { filter: `url(#${fid})` }, svg), D.scene, { x0: -107, y0: 0, W: 854, H: 480 });
    vignette(svg, defs); lightBand(svg, defs, strogg ? ST.light : C.glow, strogg ? .55 : .45);
    grid(svg, defs, strogg ? ST.grid : "#fff", strogg ? 5.5 : 6.5);
  }
  bands(svg, strogg ? "strogg" : "marine");
  if (state === "pause" || state === "strogg") levelBlock(defs, opening);
  if (state === "arena") matchBlock(defs);
  // the screen title
  if (D.title) {
    if (strogg) {
      const L = slabel(svg, D.title[1], 44, D.title[0] + 16, 12.3, { color: ST.read, spacing: 1.2 });
      translate(L, 60, opening ? 500 : 0, { white: true, bar: { parent: svg, h: 16, y: D.title[0] + 12, from: 30, to: 44 + 120, slide: 150 } });
    } else text(svg, D.title[1], 44, D.title[0] + 15.6, T.title);
  }
  drawNav(defs, opening);
  if (D.links.length) {
    const ls = plinth(svg, D.links, { strogg });
    if (strogg) ls.forEach(g => { const L = { rune: A.runes ? A.runes(g, "EXIT", 39, 415.6, 8.4, { fill: ST.sel, opacity: 0 }) : svgEl("g", {}, g), latin: g.querySelector("text"), x: 39, y: 415.6, size: 10.7, color: ST.read };
      translate(L, 120 + 45 * 7, opening ? 200 : 0); });
  }
  promptBar(svg, D.prompts, { strogg });
  if (!pause) drawMessage();
  // menu.fade: from black over 250 ms
  const fade = full(svg, "#000", { class: "hmfade", "pointer-events": "none", opacity: opening ? 1 : 0 });
  if (opening) run(0, 250, p => fade.setAttribute("opacity", 1 - p));
  if (!pause && opening && !reduce) { burst = BURSTS.open; openAt = performance.now(); } else burst = null;
  ambient(performance.now(), true);
}

/* The navigation. The menu focuses its primary action as it opens: CONTINUE with a save, else SINGLE PLAYER; the pauses
   RESUME. Hover or focus moves the focus, and on the title the message line follows it. */
let navPlates = [];
function drawNav(defs, opening) {
  const D = SCREENS[state], items = D.items(), strogg = state === "strogg";
  if (focusIdx === null || focusIdx >= items.length) focusIdx = 0;
  navPlates = items.map((label, i) => {
    const y = D.top + i * 30, g = (strogg ? stroggPlate : navPlate)(svg, defs, y, label, { focused: i === focusIdx });
    if (strogg) {
      const L = slabel(g, label, 44, y + 20.2, 15, { cls: "nl" }); L.latin.removeAttribute("fill"); L.latin.removeAttribute("fill-opacity");
      translate(L, 120 + i * 45, opening ? 200 : 0);
    }
    const focus = () => { if (focusIdx === i && g.classList.contains("f")) return;
      svg.querySelectorAll(".np.f").forEach(n => n.classList.remove("f")); g.classList.add("f"); focusIdx = i;
      if (state === "menu") drawMessage();
      if (strogg && !reduce) scanBar({ parent: g, h: 15, y: y + 15, from: 30, to: 44 + measure(label, { family: F_S, size: 15, weight: 600, spacing: 1.1 }) + 12 }, 0, 300); };
    g.addEventListener("pointerenter", focus); g.addEventListener("focus", focus);
    return g;
  });
  if (strogg && opening && !reduce) scanBar({ parent: navPlates[0], h: 15, y: D.top + 15, from: 30, to: 44 + 60 }, 420, 300);
}

/* The message line at 44,364 u explains the focused item (section 13.7): for CONTINUE the newest save's levelshot
   (56x31.5 u in a #414624 frame), its name and its place, difficulty and play time; else one sentence. */
function drawMessage() {
  svg.querySelector("g.hmmsg")?.remove();
  const label = SCREENS.menu.items()[focusIdx], g = svgEl("g", { class: "hmmsg", "pointer-events": "none" }, svg);
  svg.insertBefore(g, svg.querySelector(".hmfade"));
  if (label === "CONTINUE") {
    const cid = id("th"); svgEl("rect", { x: 44, y: 365, width: 56, height: 31.5 }, svgEl("clipPath", { id: cid }, sdefs));
    A.levelshot?.(svgEl("g", { "clip-path": `url(#${cid})` }, g), "sp", { x0: 44, y0: 365, W: 56, H: 31.5 });
    svgEl("rect", { x: 44.5, y: 365.5, width: 55, height: 30.5, fill: "none", stroke: C.picture, "stroke-width": 1 }, g);
    text(g, SAVE.title, 108, 375.5, T.saveTitle); text(g, SAVE.detail, 108, 390.5, T.saveDetail);
  } else if (MESSAGE[label]) text(g, MESSAGE[label], 44, 375.4, T.message);
}

/* The current level block (level_block): a section 6 card, black 0.94 in a #A0A040 rail with 8 u and 2 u cuts and a
   26.7 u header (marker, CURRENT MISSION), at 374,118 u (262x270 u). The levelshot stands at 4:3 in the middle of a
   242x110 u picture frame, then the level, the difficulty, >>MISSION OBJECTIVES and the objectives (open ones newest
   first, completed ones behind a check at 0.5), and at the foot, under a faint rule, the time in the mission with the
   run's total and how long ago the newest save was written. The Strogg pause's card is chamfered 10 u at every corner,
   #0A0503 in a #F59512 rail at 0.75 under the stock HUD grain, its picture chamfered, its lines in R_Strogg. */
function card(parent, defs, w, h, strogg) {
  const shape = strogg ? `M10 0H${w - 10}L${w} 10V${h - 10}L${w - 10} ${h}H10L0 ${h - 10}V10Z` : `M2 0H${w - 8}L${w} 8V${h - 2}L${w - 2} ${h}H8L0 ${h - 8}V2Z`;
  svgEl("path", { d: shape, fill: strogg ? ST.card : "#000", "fill-opacity": .94 }, parent);
  if (strogg) {                                   // the grain: s_static added warm and faint inside the chamfers
    const nid = id("n"), nf = svgEl("filter", { id: nid, x: 0, y: 0, width: "100%", height: "100%" }, defs);
    svgEl("feTurbulence", { type: "fractalNoise", baseFrequency: 1.1, numOctaves: 2, seed: 7 }, nf);
    svgEl("feColorMatrix", { values: "0 0 0 0 1 0 0 0 0 .75 0 0 0 0 .4 0 0 0 1.4 -.55" }, nf);
    svgEl("rect", { x: 5, y: 5, width: w - 10, height: h - 10, filter: `url(#${nid})`, opacity: .14, style: ADD }, parent);
  }
  const head = 26.67;
  svgEl("path", { d: strogg ? `M10.5 .5H${w - 10.5}L${w - .5} 10.5V${head}H.5V10.5Z` : `M2 .5H${w - 8}L${w - 8 + 8} ${head}H.5V2Z`,
    fill: grad(defs, strogg ? [[0, ST.fill, .16], [.5, ST.fill, .1], [1, "#fff", 0]] : [[0, "#fff", .08], [.5, "#fff", .08], [1, "#fff", 0]], { v: false }) }, parent);
  svgEl("path", { d: `M0 ${head}H${w}`, stroke: strogg ? ST.line : C.railCard, "stroke-opacity": .5, "stroke-width": .67 }, parent);
  svgEl("path", { d: shape, fill: "none", stroke: strogg ? ST.line : C.railCard, "stroke-opacity": strogg ? .75 : 1, "stroke-width": 1 }, parent);
  if (strogg) svgEl("path", { d: "M12 9.8h6l-1.6 6.4h-6z", fill: ST.line }, parent); else svgEl("path", { d: "M9.33 10.67h5.33v5.33z", fill: C.marker }, parent);
}
function picture(parent, defs, scene, px, py, pw, ph, strogg) {
  const sw = Math.min(pw, ph * 4 / 3), sx = px + (pw - sw) / 2, cid = id("pic");
  svgEl("rect", { x: sx, y: py, width: sw, height: ph }, svgEl("clipPath", { id: cid }, defs));
  A.levelshot?.(svgEl("g", { "clip-path": `url(#${cid})` }, parent), scene, { x0: sx, y0: py, W: sw, H: ph });
  if (strogg) {
    svgEl("path", { d: `M${px} ${py}h6l-6 6zM${px + pw} ${py + ph - 6}v6h-6z`, fill: ST.card }, parent);
    svgEl("path", { d: `M${px + 6} ${py + .5}H${px + pw - .5}V${py + ph - 6}L${px + pw - 6} ${py + ph - .5}H${px + .5}V${py + 6}Z`, fill: "none", stroke: ST.line, "stroke-opacity": .6, "stroke-width": 1 }, parent);
  } else svgEl("rect", { x: px + .5, y: py + .5, width: pw - 1, height: ph - 1, fill: "none", stroke: C.picture, "stroke-width": 1 }, parent);
}
function levelBlock(defs, opening) {
  const D = LEVEL[state], strogg = state === "strogg", w = 262, h = 270;
  const g = svgEl("g", { transform: "translate(374 118)", class: "hmlevel" }, svg);
  card(g, defs, w, h, strogg);
  picture(g, defs, D.scene, 10, 34.67, 242, 110, strogg);
  const below = 151.33, rows = 202, start = 120 + 45 * 8, texts = [];
  const sl = (s, x, y, size, opt, delay, dur, white) => texts.push([slabel(g, s, x, y, size, opt), delay, dur, white]);
  if (strogg) {
    sl(D.head, 24, 17.4, 10, { spacing: .9 }, 0, 300, true);
    sl(D.name, 10, below + 13, 14, { spacing: .9 }, 80, 400, true);
    sl(D.detail, 10, below + 28.6, 11.3, { color: ST.sel, spacing: .9 }, 140, 200, false);
    sl(">>MISSION OBJECTIVES", 10, below + 45.8, 9.7, { color: ST.sel, spacing: .7 }, 180, 200, false);
  } else {
    text(g, D.head, 20, 16.4, T.head);
    text(g, D.name, 10, below + 12.6, T.name);
    text(g, D.detail, 10, below + 27.8, T.detail);
    text(g, ">>MISSION OBJECTIVES", 10, below + 45.4, T.objHead);
  }
  D.objectives.forEach(([s, done], i) => {
    const oy = rows + i * 12.5;
    if (done) svgEl("path", { d: `M10 ${oy + 5.33}l2.4 2.6 4.6-5.4`, fill: "none", stroke: strogg ? ST.sel : C.check, "stroke-width": 1.4, "stroke-linecap": "round", "stroke-linejoin": "round" }, g);
    else if (strogg) svgEl("path", { d: `M11.3 ${oy + 2.67}h4.7l-1.3 5h-4.7z`, fill: ST.line, "fill-opacity": .6 }, g);
    else svgEl("path", { d: `M10 ${oy + 2.67}h6l-6 6z`, fill: C.objMark, "fill-opacity": .4 }, g);
    if (strogg) sl(s, 22, oy + 9.6, 10.7, { alpha: done ? .55 : 1, spacing: .7 }, 220 + i * 60, 200, false);
    else text(g, s, 22, oy + 9, { ...T.objective, alpha: done ? .5 : .9 });
  });
  svgEl("path", { d: `M10 ${h - 29 + .5}H${w - 10}`, stroke: "#fff", "stroke-opacity": .1, "stroke-width": .67 }, g);
  text(g, D.stats, 10, h - 19.8, { ...T.stats, fill: strogg ? ST.stats : C.stats });
  text(g, D.saved, 10, h - 9.8, { ...T.stats, fill: strogg ? ST.stats : C.stats });
  texts.forEach(([L, delay, dur, white]) => translate(L, start + delay, opening ? dur : 0, { white }));
}
/* The Arena pause's match block (retained_arena_pause.match_block): the level block's card with the match's tier and
   role in the header, the map's levelshot in a 242x118 u frame, the match, its game type and map, and under a faint rule
   40 u above the foot SCORE with the paused clock in the value colour, then the score. */
function matchBlock(defs) {
  const w = 262, h = 270, g = svgEl("g", { transform: "translate(374 118)", class: "hmlevel" }, svg);
  card(g, defs, w, h, false);
  picture(g, defs, "mp", 10, 34.67, 242, 118, false);
  const below = 159.33;
  text(g, MATCH.head, 20, 16.4, T.head, { style: "white-space:pre" });
  text(g, MATCH.name, 10, below + 12.6, T.name);
  text(g, MATCH.mode, 10, below + 27.8, T.detail);
  text(g, MATCH.map, 10, below + 41.1, T.detail);
  svgEl("path", { d: `M10 ${h - 40 + .5}H${w - 10}`, stroke: "#fff", "stroke-opacity": .1, "stroke-width": .67 }, g);
  text(g, "SCORE", 10, h - 40 + 4 + 9.6, T.scoreLabel);
  text(g, MATCH.clock, w - 10, h - 40 + 4 + 9.6, { ...T.scoreLabel, fill: C.value }, { "text-anchor": "end" });
  text(g, MATCH.score.toUpperCase(), 10, h - 40 + 4 + 12 + 11.4, T.score);
}

/* The ambient clock: the montage and its signal, the menu-move bursts and the emblem. */
function ambient(now, force = false) {
  if (!S && !E) return;
  const t = reduce ? 0 : frozen ?? now - start;
  if (S) {
    const tt = ((t % PERIOD) + PERIOD) % PERIOD, slot = Math.floor(((tt - SIGNAL_LEAD + PERIOD) % PERIOD) / 5000);
    const into = ((tt - SIGNAL_LEAD - slot * 5000) % PERIOD + PERIOD) % PERIOD;
    // the newest picture over the one it replaces: it catches at 0.40, then 0.80, and holds by 420 ms
    const shown = reduce ? 0 : slot, under = (shown + SLOTS - 1) % SLOTS;
    if (S.slot !== shown) { S.slot = shown; S.pics.appendChild(S.shots[shown]); }
    S.shots.forEach((s, i) => s.setAttribute("opacity", i === shown ? (reduce || into >= 420 ? 1 : lerpKeys(SIGNAL_REVEAL, into)) : !reduce && i === under ? 1 : 0));
    let [st, phase] = reduce ? [0, 0] : signalAt(t);
    if (burst && !reduce) { const bt = now - openAt, d = burst[burst.length - 1][0];
      if (bt < d) { const b = lerpKeys(burst, bt); if (b > st) { st = b; phase = 9000 + Math.floor(bt / SIGNAL_STEP); } } else burst = null; }
    const key = st > 0 ? `${phase}:${st.toFixed(2)}` : "rest";
    if (force || key !== S.key) { S.key = key; drawFault(S, sdefs, st, phase); }
  }
  animateEmblem(E, t);
}
let lastStep = 0;
const tick = now => { if (now - lastStep < 16) return; lastStep = now; ambient(now); };
if (A.tick) A.tick(svg, tick); else { const loop = now => { tick(now); requestAnimationFrame(loop); }; requestAnimationFrame(loop); }
reduceMQ.addEventListener?.("change", e => { reduce = e.matches; build(false); });

build(false);
document.fonts?.ready.then(() => build(false));
const STATUS = {
  menu: () => save ? "Main menu with a save: CONTINUE leads and the message line shows the save; the backdrop changes pictures through the signal."
    : "Main menu without a save: CONTINUE collapses, the rows rise one pitch and SINGLE PLAYER takes the focus.",
  pause: () => "Single-player pause: GAME PAUSED, seven actions and the current level card in the emblem's place, over the softened game.",
  strogg: () => "Strogg pause: the labels arrive in runes and translate, the scan bar runs under the focused plate.",
  arena: () => "Arena pause: MATCH PAUSED, four actions on the main menu's rows, the match in the level block's place and nothing on the plinth.",
};
const seg = document.getElementById("homeState"), saveBox = document.getElementById("homeSave");
const sync = () => { if (status) status.textContent = STATUS[state](); if (saveBox) saveBox.closest("label").style.display = state === "menu" ? "" : "none"; };
seg?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  seg.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); state = b.dataset.s; focusIdx = null; build(true); sync(); });
saveBox?.addEventListener("click", () => { save = saveBox.classList.contains("on"); focusIdx = null; build(true); sync(); });   // core.js toggles the box first
document.getElementById("homeReplay")?.addEventListener("click", () => { focusIdx = null; build(true); });
sync();
})();
