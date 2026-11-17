
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
let uid = 0; const id = p => `${p}${++uid}`;

/* ---------- Levelshot stand-ins: vector scenes authored in an 854x480 frame (x -107..747), scaled to cover ---------- */
const lin = (defs, stops, x2 = 0, y2 = 1) => { const gid = id("lg"), g = svgEl("linearGradient", { id: gid, x1: 0, y1: 0, x2, y2 }, defs); stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g)); return `url(#${gid})`; };
const rad = (defs, stops, cx = .5, cy = .5, r = .5) => { const gid = id("rg"), g = svgEl("radialGradient", { id: gid, cx, cy, r }, defs); stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g)); return `url(#${gid})`; };
const SCENES = {
  sp(g, defs) {                      // Air Defense Trenches: a dusk trench line under anti-aircraft fire
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: lin(defs, [[0, "#2a1f1c"], [.45, "#6b4230"], [.62, "#c9824a"], [.7, "#8a5634"], [1, "#1a120e"]]) }, g);
    svgEl("ellipse", { cx: 520, cy: 300, rx: 260, ry: 90, fill: rad(defs, [[0, "#ffd08a", .55], [1, "#ffb060", 0]]) }, g);
    svgEl("path", { d: "M-107 310L-40 282L40 290L110 262L190 276L260 250L330 270L420 258L500 280L590 262L680 286L747 272V340H-107Z", fill: "#4a2f22" }, g);
    svgEl("path", { d: "M-107 330L0 318L90 326L170 306L280 320L370 312L470 330L560 316L660 332L747 322V360H-107Z", fill: "#35231a" }, g);
    [[120, 120, 60], [210, 90, 40], [590, 110, 55], [650, 150, 30]].forEach(([x, y, r]) => svgEl("circle", { cx: x, cy: y, r, fill: rad(defs, [[0, "#ffe6b0", .5], [.4, "#ff9a40", .18], [1, "#ff9a40", 0]]) }, g));
    [[430, 330, 250, 60], [470, 330, 330, 40], [510, 332, 420, 70]].forEach(([x1, y1, x2, y2]) => svgEl("path", { d: `M${x1} ${y1}L${x2} ${y2}`, stroke: "#ffd9a0", "stroke-opacity": .45, "stroke-width": 1.2 }, g));
    svgEl("path", { d: "M430 360L446 318H520L536 360Z", fill: "#241712" }, g);
    svgEl("path", { d: "M466 322L452 276L470 270L488 318Z", fill: "#2e1d16" }, g);
    svgEl("path", { d: "M470 280L520 170L532 176L486 286Z", fill: "#1f1410" }, g);
    svgEl("path", { d: "M480 283L540 190L550 196L494 290Z", fill: "#1f1410" }, g);
    svgEl("path", { d: "M-107 480V356L150 368L300 380L300 480Z", fill: "#23170f" }, g);
    svgEl("path", { d: "M747 480V350L480 366L350 380L350 480Z", fill: "#1d130d" }, g);
    svgEl("path", { d: "M300 380L350 380L380 480H270Z", fill: "#2f2016" }, g);
    for (let i = 0; i < 7; i++) svgEl("path", { d: `M${-60 + i * 52} ${360 + i * 3}v${120 - i * 3}`, stroke: "#3a281c", "stroke-width": 3 }, g);
    for (let i = 0; i < 6; i++) svgEl("path", { d: `M${700 - i * 60} ${352 + i * 4}v${128 - i * 4}`, stroke: "#302117", "stroke-width": 3 }, g);
    [[210, 372], [262, 377], [402, 374], [610, 360]].forEach(([x, y]) => svgEl("circle", { cx: x, cy: y, r: 2.2, fill: "#ffcf7a" }, g));
    [[80, 250, 120, 80], [640, 236, 150, 96]].forEach(([x, y, rx, ry]) => svgEl("ellipse", { cx: x, cy: y, rx, ry, fill: rad(defs, [[0, "#3a302a", .5], [.6, "#3a302a", .22], [1, "#3a302a", 0]]) }, g));
  },
  mp(g, defs) {                      // Heartless: a Strogg hall with the Marine flag stand glowing on its platform
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: lin(defs, [[0, "#0f1415"], [.5, "#1f2a28"], [1, "#0b0e0d"]]) }, g);
    for (let i = 0; i < 12; i++) { const x = -100 + i * 74; svgEl("rect", { x, y: 60, width: 66, height: 200, fill: "#243230", "fill-opacity": .55 }, g); svgEl("rect", { x: x + 4, y: 70, width: 58, height: 3, fill: "#9fd6c0", "fill-opacity": .35 }, g); }
    svgEl("rect", { x: -107, y: 250, width: 854, height: 18, fill: "#101615" }, g);
    [[40, 3.5], [58, 2.2], [74, 5]].forEach(([y, w]) => svgEl("path", { d: `M-107 ${y}H747`, stroke: "#394744", "stroke-width": w }, g));
    svgEl("path", { d: "M-107 480L-107 330L747 330L747 480Z", fill: lin(defs, [[0, "#1c2422"], [1, "#0d1110"]]) }, g);
    for (let i = -8; i <= 8; i++) svgEl("path", { d: `M${320 + i * 40} 330L${320 + i * 130} 480`, stroke: "#2c3a37", "stroke-opacity": .6 }, g);
    [345, 365, 395, 440].forEach(y => svgEl("path", { d: `M-107 ${y}H747`, stroke: "#2c3a37", "stroke-opacity": .5 }, g));
    [[-40, 22], [110, 18], [520, 18], [680, 24]].forEach(([x, w]) => { svgEl("rect", { x, y: 40, width: w, height: 300, fill: "#141b1a" }, g); svgEl("rect", { x: x + w - 3, y: 40, width: 3, height: 300, fill: "#4b5f5a", "fill-opacity": .4 }, g); });
    svgEl("ellipse", { cx: 300, cy: 318, rx: 150, ry: 42, fill: rad(defs, [[0, "#7fdc4a", .45], [1, "#7fdc4a", 0]]) }, g);
    svgEl("path", { d: "M220 318L250 300H350L380 318L350 336H250Z", fill: "#27332f", stroke: "#6AA42B", "stroke-opacity": .6 }, g);
    svgEl("path", { d: "M298 300V230", stroke: "#cfd8d2", "stroke-width": 2.2 }, g);
    svgEl("path", { d: "M299 232C312 226 322 238 338 230V256C322 264 312 252 299 258Z", fill: "#6AA42B" }, g);
    svgEl("path", { d: "M560 330L620 262H747V330Z", fill: "#18201e" }, g);
    svgEl("ellipse", { cx: 612, cy: 356, rx: 38, ry: 9, fill: "none", stroke: "#FF7B04", "stroke-opacity": .55, "stroke-width": 2 }, g);
    svgEl("path", { d: "M-20 300l8-30h14l8 30zM596 300l8-30h14l8 30z", fill: "#0e1312" }, g);
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: rad(defs, [[0, "#6d8f84", .12], [1, "#000", 0]], .45, .55, .6) }, g);
  },
  strogg(g, defs) {                  // Strogg Medical Facilities: an operating bay under a harsh lamp, organic conduits, red haze
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: lin(defs, [[0, "#120807"], [.5, "#2d130e"], [1, "#0a0504"]]) }, g);
    for (let i = 0; i < 14; i++) { const x = -100 + i * 64;
      svgEl("path", { d: `M${x} 40L${x + 26} 40L${x + 34} 300L${x - 8} 300Z`, fill: "#26110c", "fill-opacity": .8 }, g);
      svgEl("rect", { x: x + 8, y: 50, width: 10, height: 3, fill: "#ff7a2a", "fill-opacity": .45 }, g); }
    [["M-107 120C60 60 140 200 320 150S560 70 747 140", 16], ["M-107 210C80 170 200 260 360 220S600 180 747 230", 10]].forEach(([d, w]) => {
      svgEl("path", { d, fill: "none", stroke: "#3d1812", "stroke-width": w }, g);
      svgEl("path", { d, fill: "none", stroke: "#a0432a", "stroke-opacity": .35, "stroke-width": w * .25, transform: `translate(0 ${-w * .2})` }, g); });
    svgEl("path", { d: "M300 0L340 0L420 330L220 330Z", fill: lin(defs, [[0, "#ffd08a", .35], [1, "#ff8a3a", 0]]) }, g);
    svgEl("ellipse", { cx: 320, cy: 332, rx: 150, ry: 26, fill: rad(defs, [[0, "#ff9a40", .35], [1, "#ff9a40", 0]]) }, g);
    svgEl("path", { d: "M230 300H410L400 316H240Z", fill: "#1a0b08", stroke: "#6a2a1c", "stroke-width": 1 }, g);
    svgEl("path", { d: "M300 316V360M340 316V360", stroke: "#1a0b08", "stroke-width": 6 }, g);
    svgEl("path", { d: "M-107 360H747V480H-107Z", fill: lin(defs, [[0, "#1c0c09"], [1, "#0a0403"]]) }, g);
    for (let i = -8; i <= 8; i++) svgEl("path", { d: `M${320 + i * 40} 360L${320 + i * 120} 480`, stroke: "#3a1a12", "stroke-opacity": .6 }, g);
    [[120, 0, 140, 220], [520, 0, 500, 190], [600, 0, 630, 260]].forEach(([x1, y1, x2, y2]) =>
      svgEl("path", { d: `M${x1} ${y1}Q${(x1 + x2) / 2 + 20} ${(y1 + y2) / 2} ${x2} ${y2}`, fill: "none", stroke: "#1a0a07", "stroke-width": 3 }, g));
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: rad(defs, [[0, "#ff5a2a", .1], [1, "#000", 0]], .5, .6, .7) }, g);
  },
  intro(g, defs) {                   // the stock intro art without its emblem: a green glow and geared rings; openQ4 stands in
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: "#060a03" }, g);
    svgEl("circle", { cx: 320, cy: 230, r: 320, fill: rad(defs, [[0, "#7fae1a", .75], [.35, "#44640e", .5], [1, "#0b1405", 0]]) }, g);
    [[210, 26, .18], [170, 14, .22], [120, 6, .3]].forEach(([r, w, a]) => svgEl("circle", { cx: 320, cy: 230, r, fill: "none", stroke: "#a4d23a", "stroke-opacity": a, "stroke-width": w }, g));
    const arc = (r0, r1, a0, a1) => `M${320 + r0 * Math.cos(a0)} ${230 + r0 * Math.sin(a0)}A${r0} ${r0} 0 0 1 ${320 + r0 * Math.cos(a1)} ${230 + r0 * Math.sin(a1)}L${320 + r1 * Math.cos(a1)} ${230 + r1 * Math.sin(a1)}A${r1} ${r1} 0 0 0 ${320 + r1 * Math.cos(a0)} ${230 + r1 * Math.sin(a0)}Z`;
    for (let k = 0; k < 8; k++) { const a = k * Math.PI / 4 + Math.PI / 8; svgEl("path", { d: arc(232, 258, a - .12, a + .12), fill: "#86b52a", "fill-opacity": .16 }, g); }
    const t = svgEl("text", { x: 320, y: 244, "text-anchor": "middle", "font-family": F_M, "font-size": 40, fill: "#0a1004", stroke: "#b9f04a", "stroke-opacity": .8, "stroke-width": 1.4 }, g);
    t.textContent = "openQ4";
  },
};
/* Draws a scene covering the view; returns a setter for the slow drift (scale 1 + d). */
A.levelshot = (parent, kind, lay) => {
  const defs = svgEl("defs", {}, parent), s = Math.max(lay.W / 854, lay.H / 480), cx = lay.x0 + lay.W / 2, cy = lay.y0 + lay.H / 2;
  const g = svgEl("g", {}, parent);
  const set = d => g.setAttribute("transform", `translate(${cx} ${cy}) scale(${(s * (1 + d)).toFixed(4)}) translate(-320 -240)`);
  set(0); SCENES[kind](g, defs);
  return { g, set };
};

const svg = document.getElementById("ldSvg");
if (!svg) return;
const NOTE = "#FFFF8D", VALUE = "#FFBE23", PROG = "#E06C00", MARKER = "#B0CD6B";
const DATA = {
  sp: { name: "AIR DEFENSE TRENCHES", line: "Mission 2 · Corporal", shot: "sp",
    objectives: ["Fight through the trench network", "Destroy the anti-aircraft battery", "Regroup with Rhino squad at the bunker"],
    stages: [["Map", 500, .06], ["Collision map", 700, .10], ["Area awareness", 600, .08], ["Assets", 4200, .71, 980], ["Scripts", 500, .05]],
    tips: ["Hold the scores button to see your objectives and the mission briefing.", "Point at a medic or a tech and press Fire to ask for help.", "Quick save often: the quick save key stores your progress at once."] },
  intro: { name: "AIR DEFENSE BUNKER", line: "Mission 1 · Corporal", shot: "intro", objectives: [],
    stages: [["Map", 600, .07], ["Collision map", 700, .11], ["Area awareness", 600, .08], ["Assets", 4200, .69, 1136], ["Scripts", 400, .05]],
    tips: ["Hold the scores button to see your objectives and the mission briefing.", "Point at a medic or a tech and press Fire to ask for help."] },
  mp: { name: "HEARTLESS", line: "Capture the Flag · q4ctf1", shot: "mp",
    server: ["openQ4 Community · CTF", "203.0.113.24:28004", "Capture the Flag", "Capture Limit: 5", "Time Limit: 15"],
    items: [["machinegun", "Machinegun"], ["shotgun", "Shotgun"], ["hyperblaster", "Hyperblaster"], ["grenadelauncher", "Grenade Launcher"], ["nailgun", "Nailgun"], ["rocketlauncher", "Rocket Launcher"],
      ["railgun", "Railgun"], ["lightninggun", "Lightning Gun"], ["armor", "Large Armor"], ["health", "Large Health"], ["quaddamage", "Quad Damage"], ["item_haste", "Haste"]],
    stages: [["Map", 500, .08], ["Collision map", 700, .12], ["Items", 2800, .40], ["Assets", 2000, .40, 640]],
    tips: [] },
};
const WEAPON_COLOR = { machinegun: "#FFFF00", shotgun: "#FF8C00", hyperblaster: "#0073FF", grenadelauncher: "#338F12", nailgun: "#99CCCC", rocketlauncher: "#FF3300", railgun: "#00FF00", lightninggun: "#FFFFBA", armor: "#FF0000", health: "#FF8000", quaddamage: "#3F8CFF", item_haste: "#FFCC33" };
let kind = "sp", dev = "mouse", t0 = performance.now(), ui = {};

/* Stock furniture, in stock units (guis/loading/*.gui, frames.md 1.20). */
const shadowText = (parent, x, y, t, o) => { const g = svgEl("g", {}, parent); svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000", "fill-opacity": .9 }, g).textContent = t; const f = svgEl("text", { ...o, x, y }, g); f.textContent = t; return f; };
/* The load bands' inner edges: top thick (28.8 u) on the leading side with a riser at 268.8-290 u, then 9.3 u;
   bottom 11.5 u on the leading side, a riser at 165-207.5 u, then 54 u (the MP band sits 118 u higher). */
const topEdge = (L, R, T) => [[L, T + 28.8], [268.8, T + 28.8], [290, T + 9.3], [R, T + 9.3]];
const btmEdge = (L, R, B, mp) => { const o = B - 480 - (mp ? 118 : 0); return [[L, 468.5 + o], [165, 468.5 + o], [207.5, 426 + o], [R, 426 + o]]; };
const poly = pts => pts.map(([x, y], i) => `${i ? "L" : "M"}${x.toFixed(2)} ${y.toFixed(2)}`).join("");
function bands(g, L, R, T, B, mp) {
  const te = topEdge(L, R, T), be = btmEdge(L, R, B, mp);
  /* load_*_edgeadd: an additive #181D0A spill into the picture, full at the inner edge and gone about 100 u in,
     drawn as ten stacked strips that follow the silhouette. */
  const spill = svgEl("g", { style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
  for (let d = 10; d <= 100; d += 10) {
    svgEl("path", { d: poly(te) + poly([...te].reverse().map(([x, y]) => [x, y + d])).replace("M", "L") + "Z", fill: "#181D0A", "fill-opacity": .1 }, spill);
    svgEl("path", { d: poly(be) + poly([...be].reverse().map(([x, y]) => [x, y - d])).replace("M", "L") + "Z", fill: "#181D0A", "fill-opacity": .1 }, spill);
  }
  svgEl("path", { d: `M${L} ${T}H${R}` + poly([...te].reverse()).replace("M", "L") + "Z", fill: "#000", "fill-opacity": .8 }, g);
  svgEl("path", { d: `M${L} ${B}H${R}` + poly([...be].reverse()).replace("M", "L") + "Z", fill: "#000", "fill-opacity": .8 }, g);
  return { te, be };
}
/* load_corner: a 0.8 u L bracket with 22.8 u legs and a warm halo, added in #3A3A3A. */
function bracket(g, x, y, sx, sy) {
  const d = `M${x + sx * 22.8} ${y}H${x}V${y + sy * 22.8}`;
  const add = svgEl("g", { style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
  svgEl("path", { d, fill: "none", stroke: "#5A3418", "stroke-width": 7, "stroke-opacity": .28, filter: `url(#${ui.soft})` }, add);
  svgEl("path", { d, fill: "none", stroke: "#3A3A3A", "stroke-width": .9 }, add);
}
/* load_dots: ten by two 4 u squares on a 9 u pitch in #1C2613. */
const dots = (g, x, y) => { for (let i = 0; i < 10; i++) for (let j = 0; j < 2; j++) svgEl("rect", { x: x + 9.8 + i * 9, y: y + 5.5 + j * 9.4, width: 4.1, height: 3.9, fill: "#1C2613" }, g); };
/* common/gradient3: black, 0 at the leading end to 0.73 at the trailing end (drawn mirrored). */
const strip = (g, x, y, w, h, a) => { const gid = id("st"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
  [[0, 0], [.98, .73 * a], [1, .73 * a]].forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": v }, lg));
  svgEl("rect", { x, y, width: w, height: h, fill: `url(#${gid})` }, g); };
/* load_bar: a cylindrical bar with a lower-leading 45-degree cut, #E06C00 track 0.30 and fill 0.50. */
function bar(g, x, y, w, h) {
  const gid = id("cy"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs);
  [[0, .55], [.45, 1], [.55, 1], [1, .5]].forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": PROG, "stop-opacity": v }, lg));
  const shape = (w2) => `M${x} ${y}H${x + w2}V${y + h}H${x + Math.min(h, w2)}L${x} ${y + h - Math.min(h, w2)}Z`;
  svgEl("path", { d: shape(w), fill: `url(#${gid})`, "fill-opacity": .3 }, g);
  const fill = svgEl("path", { d: shape(0), fill: `url(#${gid})`, "fill-opacity": .5 }, g);
  const hi = svgEl("rect", { x, y: y + h * .46, width: 0, height: Math.max(.6, h * .08), fill: "#FFC27A", "fill-opacity": .5 }, g);
  return p => { const w2 = Math.max(0, Math.min(1, p)) * w; fill.setAttribute("d", shape(w2)); hi.setAttribute("width", Math.max(0, w2 - 2)); };
}
/* Device glyphs for the continue prompt. */
function glyph(g, x, y, device) {
  if (device === "pad") { svgEl("circle", { cx: x + 6, cy: y - 4, r: 6, fill: "#fff", "fill-opacity": .85 }, g); svgEl("text", { x: x + 6, y: y - 1, "text-anchor": "middle", "font-family": F_L, "font-weight": 700, "font-size": 8, fill: "#10140A" }, g).textContent = "A"; return 16; }
  if (device === "touch") { svgEl("circle", { cx: x + 6, cy: y - 4, r: 2.4, fill: "#fff" }, g); svgEl("circle", { cx: x + 6, cy: y - 4, r: 5.6, fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1 }, g); return 16; }
  svgEl("path", { d: `M${x + 1} ${y - 7.5}a4.5 5.5 0 0 1 9 0v6a4.5 5.5 0 0 1-9 0z`, fill: "none", stroke: "#fff", "stroke-width": 1 }, g);
  svgEl("path", { d: `M${x + 1.4} ${y - 7}a4.1 5 0 0 1 4.1 -4.9v4.9z`, fill: "#fff" }, g);
  return 15;
}

function build() {
  svg.innerHTML = ""; ui = { defs: svgEl("defs", {}, svg) };
  ui.soft = id("sf"); svgEl("feGaussianBlur", { stdDeviation: 2.4 }, svgEl("filter", { id: ui.soft, x: "-30%", y: "-30%", width: "160%", height: "160%" }, ui.defs));
  const lay = hud.fit(svg), classic = hud.classic, D = DATA[kind], mp = kind === "mp";
  const VL = lay.x0, VR = lay.x0 + lay.W, VT = lay.y0, VB = lay.y0 + lay.H;
  // Classic draws the stock 4:3 composition pillarboxed (or letterboxed); Remastered fills the view.
  const L = classic ? 0 : VL, R = classic ? 640 : VR, T = classic ? 0 : VT, B = classic ? 480 : VB;
  svgEl("rect", { x: VL, y: VT, width: lay.W, height: lay.H, fill: "#000" }, svg);
  const shotLay = classic ? { x0: 0, y0: 0, W: 640, H: 480 } : lay;
  const clipId = id("pic"); svgEl("rect", { x: L, y: T, width: R - L, height: B - T }, svgEl("clipPath", { id: clipId }, ui.defs));
  const pic = svgEl("g", { "clip-path": `url(#${clipId})` }, svg);
  ui.shot = A.levelshot(pic, D.shot, shotLay);
  bands(svg, L, R, T, B, mp && classic);
  // grid (bg_grid at 0.04, over everything but the text); not on the intro
  if (kind !== "intro") { const pid = id("gp"), pat = svgEl("pattern", { id: pid, width: 30, height: 30, patternUnits: "userSpaceOnUse" }, ui.defs);
    svgEl("path", { d: "M15 10.35v9.3M10.35 15h9.3", stroke: "#fff", "stroke-opacity": .04, "stroke-width": 1 }, pat);
    svgEl("rect", { x: L, y: T, width: R - L, height: B - T, fill: `url(#${pid})` }, svg); }
  const cL = classic ? 0 : lay.L, cR = classic ? 640 : lay.R;                       // content anchors keep to the HUD width
  dots(svg, cL + 3 * (classic ? 1 : 1), T + 2);
  if (kind !== "intro" || !classic) {
    const lowY = classic && mp ? 293.4 : B - 68.6;
    [[cL + 14.6, T + 42.6, 1, 1], [cR - 14.6, T + 42.6, -1, 1], [cL + 14.6, lowY, 1, -1], [cR - 14.6, lowY, -1, -1]].forEach(p => bracket(svg, ...p));
    strip(svg, cR - 349, T + 14, 349, 22, 1);
    const nm = shadowText(svg, cR - 14, T + 28.5, D.name, { "font-family": F_M, "font-size": 12.1, "letter-spacing": -.6, "text-anchor": "end", fill: "#fff" });
    nm.previousSibling.remove(); nm.setAttribute("fill-opacity", .8);
    if (!classic || mp) {
      strip(svg, cR - 349, T + 36, 349, 18, .5);
      ui.msg = svgEl("text", { x: cR - 14, y: T + 48, "text-anchor": "end", "font-family": F_L, "font-weight": 500, "font-size": 10, fill: "#fff", "fill-opacity": .52 }, svg);
      ui.msg.textContent = classic ? "" : D.line;
    }
  }
  if (classic) buildClassic(lay, D, mp); else buildRemastered(lay, D, mp, cL, cR, T, B);
  hud.regionsLayer(svg, lay);
  t0 = performance.now(); ui.done = false; update(t0);
}

function buildClassic(lay, D, mp) {
  const y = mp ? 313 : 431;
  ui.bar = bar(svg, 235, y + 6, 405, 14);
  ui.status = shadowText(svg, 628, y + 20, "LOADING", { "font-family": F_M, "font-size": 13.4, "text-anchor": "end", fill: "#fff" });
  if (mp) {
    const S = D.server;
    [[S[0], 353, .8], [S[1], 368, .8], [S[2], 386, .52], [S[3], 401, .52]].forEach(([t, yy, a]) =>
      svgEl("text", { x: 627, y: yy, "text-anchor": "end", "font-family": F_L, "font-weight": 500, "font-size": 12, fill: "#fff", "fill-opacity": a }, svg).textContent = t);
    ui.info = svgEl("text", { x: 320, y: 421, "text-anchor": "middle", "font-family": F_L, "font-weight": 500, "font-size": 12, fill: "#fff", "fill-opacity": .52 }, svg);
    ui.icons = svgEl("g", {}, svg); ui.iconSlots = [];
  }
}

function buildRemastered(lay, D, mp, cL, cR, T, B) {
  // mission objectives, in the objectives display's open plate (section 14.11), on the leading side
  if (D.objectives?.length) {
    const g = svgEl("g", { transform: `translate(${cL + 16} ${T + 62})` }, svg), w = 236, h = 34 + D.objectives.length * 17;
    const gid = id("op"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
    [[0, .49], [.36, .49], [.52, .25], [.7, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, lg));
    svgEl("path", { d: `M0 0H${w}V${h}H10L0 ${h - 10}Z`, fill: `url(#${gid})` }, g);
    svgEl("path", { d: `M.5 0V${h - 10}L10.5 ${h - .5}H${w * .55}`, fill: "none", stroke: MARKER, "stroke-opacity": .4, "stroke-width": 1 }, g);
    svgEl("text", { x: 10, y: 16, "font-family": F_M, "font-size": 8.4, "letter-spacing": -.3, fill: "#FF8000" }, g).textContent = ">>MISSION OBJECTIVES";
    D.objectives.forEach((o, i) => {
      svgEl("path", { d: `M10 ${27 + i * 17}h6l-6 6z`, fill: MARKER, "fill-opacity": .4 }, g);
      svgEl("text", { x: 22, y: 33 + i * 17, "font-family": F_L, "font-weight": 500, "font-size": 9.6, fill: "#fff" }, g).textContent = o;
    });
  }
  // the server card (section 6 card construction) on the leading side, above the thin band
  if (mp) {
    const S = D.server, w = 216, h = 118, g = svgEl("g", { transform: `translate(${cL + 16} ${B - 30 - h})` }, svg);
    svgEl("path", { d: `M3 0H${w - 12}L${w} 12V${h - 3}L${w - 3} ${h}H12L0 ${h - 12}V3Z`, fill: "#000", "fill-opacity": .9, stroke: "#A0A040", "stroke-opacity": .8, "stroke-width": 1 }, g);
    const hg = id("hw"), lg = svgEl("linearGradient", { id: hg, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
    [[0, .3], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#8B964B", "stop-opacity": a }, lg));
    svgEl("path", { d: `M3.5 .5H${w - 12}L${w - .5} 12V26.7H.5V3Z`, fill: `url(#${hg})` }, g);
    svgEl("path", { d: "M10 9.6l5.3 3.7-5.3 3.7z", fill: "#909A49" }, g);
    svgEl("text", { x: 20, y: 17.4, "font-family": F_M, "font-size": 7.6, "letter-spacing": -.2, fill: "#fff", "fill-opacity": .9 }, g).textContent = S[0].toUpperCase();
    svgEl("path", { d: `M0 26.7H${w}`, stroke: "#A0A040", "stroke-opacity": .5, "stroke-width": .7 }, g);
    svgEl("text", { x: 10, y: 40, "font-family": F_P, "font-size": 8, fill: "#B8C29E" }, g).textContent = S[1];
    [["Mode", S[2]], ["Limits", "5 captures · 15 min"], ["Players", "11 / 16 · 6 Marine, 5 Strogg"]].forEach(([k, v], i) => {
      svgEl("text", { x: 10, y: 55 + i * 13, "font-family": F_L, "font-weight": 500, "font-size": 8.6, fill: "#fff", "fill-opacity": .45 }, g).textContent = k;
      svgEl("text", { x: 58, y: 55 + i * 13, "font-family": F_L, "font-weight": 500, "font-size": 8.6, fill: "#fff", "fill-opacity": .88 }, g).textContent = v; });
    svgEl("text", { x: 10, y: 103, "font-family": F_L, "font-style": "italic", "font-size": 8.4, fill: NOTE, "fill-opacity": .8 }, g).textContent = "“Play fair, call your flag runs, have fun.”";
  }
  // progress readout, anchored to the trailing end of the thick bottom band
  const bx = cR - 250, bw = 240;
  ui.status = svgEl("text", { x: cR - 10, y: B - 36, "text-anchor": "end", "font-family": F_M, "font-size": 13.4, fill: "#fff" }, svg);
  ui.glyph = svgEl("g", {}, svg);
  ui.bar = bar(svg, bx, B - 30, bw, 10);
  ui.stage = svgEl("text", { x: bx, y: B - 9, "font-family": F_L, "font-weight": 500, "font-size": 8.6, "letter-spacing": .4, fill: "#fff", "fill-opacity": .6 }, svg);
  ui.pct = svgEl("text", { x: cR - 10, y: B - 9, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 10, fill: VALUE }, svg);
  // the leading part of the thick band: tips (single player) or the arsenal (multiplayer)
  const lx = 214, lw = bx - lx - 16;
  if (mp) {
    svgEl("text", { x: lx + 2, y: B - 40, "font-family": F_L, "font-weight": 700, "font-size": 7.2, "letter-spacing": .8, fill: "#fff", "fill-opacity": .45 }, svg).textContent = "ARSENAL";
    ui.icons = svgEl("g", {}, svg); ui.iconSlots = []; ui.arsenal = { x: lx, y: B - 33, pitch: Math.min(19, lw / D.items.length) };
  } else if (D.tips.length) {
    const tag = svgEl("g", {}, svg);
    svgEl("path", { d: `M${lx} ${B - 47}H${lx + 20}V${B - 37}H${lx + 3}L${lx} ${B - 40}Z`, fill: VALUE, "fill-opacity": .85 }, tag);
    svgEl("text", { x: lx + 10.5, y: B - 39.2, "text-anchor": "middle", "font-family": F_L, "font-weight": 700, "font-size": 7.2, fill: "#10140A" }, tag).textContent = "TIP";
    ui.tip = svgEl("text", { x: lx, y: B - 24, "font-family": F_L, "font-weight": 500, "font-size": 9.6, fill: "#fff", "fill-opacity": .85 }, svg);
    ui.tipW = lw; ui.tipIndex = -1;
  }
}

/* Wrap a tip into at most two lines inside `w` stock units. */
let meter;
const measure = (t, size, fam = F_L, weight = 500) => { if (!meter) { meter = svgEl("svg", { width: 0, height: 0, style: "position:absolute;visibility:hidden" }); document.body.appendChild(meter); }
  const e = svgEl("text", { "font-family": fam, "font-size": size, "font-weight": weight }, meter); e.textContent = t; const w = e.getComputedTextLength(); e.remove(); return w; };
function setTip(text, w) {
  ui.tip.innerHTML = ""; const words = text.split(" "), lines = [""];
  words.forEach(x => { const t = lines[lines.length - 1] ? lines[lines.length - 1] + " " + x : x; if (measure(t, 9.6) > w && lines[lines.length - 1]) lines.push(x); else lines[lines.length - 1] = t; });
  lines.slice(0, 2).forEach((l, i) => { const s = svgEl("tspan", { x: ui.tip.getAttribute("x"), dy: i ? 11.5 : 0 }, ui.tip); s.textContent = l; });
}

function update(now) {
  const D = DATA[kind], classic = hud.classic, mp = kind === "mp", t = Math.max(0, now - t0);   // a frame can start just before t0
  const total = D.stages.reduce((a, s) => a + s[1], 0);
  let acc = 0, frac = 0, stage = D.stages[D.stages.length - 1][0], si = D.stages.length - 1, within = 1, queue = 0;
  for (let i = 0; i < D.stages.length; i++) { const [n, d, f, q] = D.stages[i]; if (t < acc + d) { within = (t - acc) / d; frac += f * within; stage = n; si = i; queue = q || 0; break; } acc += d; frac += f; }
  const done = t >= total;
  ui.bar(done ? 1 : frac);
  ui.shot.set(reduce ? 0 : .03 * Math.min(1, t / total));
  // items spawn one by one during the Items stage (stock: an icon per unique inventory item, its name in the message)
  let item = null;
  if (mp && ui.icons) {
    const ii = D.stages.findIndex(s => s[0] === "Items"), start = D.stages.slice(0, ii).reduce((a, s) => a + s[1], 0), dur = D.stages[ii][1];
    const shown = Math.max(0, Math.min(D.items.length, Math.floor((t - start) / dur * D.items.length) + (t >= start ? 1 : 0)));
    if (t >= start && t < start + dur) item = D.items[Math.min(D.items.length - 1, shown - 1)];
    while (ui.iconSlots.length < shown) {
      const k = ui.iconSlots.length, [key] = D.items[k];
      // stock: iconOffset = (640 - (24n + 24)) / 2, so the row sits 12 u left of center; the glyph fills most of its 24 u cell
      const x = classic ? (640 - (24 * D.items.length + 24)) / 2 + 24 * k + 2 : ui.arsenal.x + k * ui.arsenal.pitch;
      const y = classic ? 432 : ui.arsenal.y, h = classic ? 20 : 15;
      const g = svgEl("g", { opacity: reduce ? 1 : 0 }, ui.icons); ui.iconSlots.push(g);
      A.icon(g, key, { x, y, h, color: classic ? "#FFFFFF" : WEAPON_COLOR[key] || "#DDEBC3", opacity: classic ? 1 : .95 });
      if (!reduce) { const s0 = performance.now(); const step = n => { const p = Math.min(1, (n - s0) / 150); g.setAttribute("opacity", p); if (p < 1) requestAnimationFrame(step); }; requestAnimationFrame(step); }
    }
  }
  if (classic) {
    if (mp) { const msg = item ? item[1] : done ? "" : stage === "Map" ? "mp/q4ctf1" : stage === "Collision map" ? "collision map" : ""; if (ui.msg) ui.msg.textContent = msg; if (ui.info) ui.info.textContent = msg; }
    if (done && !mp) {                // FinishedLoading: the text changes and starts to pulse (700 ms cycle), the full bar shows
      const c = (t - total) % 700, v = c < 500 ? 1 - .5 * c / 500 : .5 + .5 * (c - 500) / 200, g = Math.round(255 * v);
      ui.status.textContent = "- CLICK TO CONTINUE -"; ui.status.previousSibling.textContent = "- CLICK TO CONTINUE -";
      ui.status.setAttribute("fill", reduce ? "#fff" : `rgb(${g},${g},${g})`);
    } else { ui.status.textContent = "LOADING"; ui.status.previousSibling.textContent = "LOADING"; ui.status.setAttribute("fill", "#fff"); }
  } else {
    const n = D.stages.length;
    ui.pct.textContent = done ? "" : `${Math.floor(frac * 100)}%`;
    if (done) {
      ui.stage.textContent = mp ? "CONNECTED · WAITING FOR THE FIRST SNAPSHOT" : `READY · LOADED IN ${(total / 1000).toFixed(1)} S`;
      const label = mp ? "JOINING" : dev === "pad" ? "CONTINUE" : dev === "touch" ? "TAP TO CONTINUE" : "CLICK TO CONTINUE";
      ui.status.textContent = label;
      const c = (t - total) % 700, v = mp || reduce ? 1 : c < 500 ? 1 - .5 * c / 500 : .5 + .5 * (c - 500) / 200;
      ui.status.setAttribute("fill-opacity", v);
      if (!mp && !ui.glyphDrawn) { ui.glyph.innerHTML = ""; const w = measure(label, 13.4, F_M, 400); glyph(ui.glyph, (hud.classic ? 628 : Number(ui.status.getAttribute("x"))) - w - 20, Number(ui.status.getAttribute("y")), dev); ui.glyphDrawn = true; }
      if (!mp) ui.glyph.setAttribute("opacity", v);
    } else {
      ui.glyph.innerHTML = ""; ui.glyphDrawn = false; ui.status.textContent = "LOADING"; ui.status.setAttribute("fill-opacity", 1);
      ui.stage.textContent = (item ? `SPAWNING · ${item[1]}` : queue ? `${stage} · ${Math.floor(within * queue)} of ${queue}` : `${stage} · ${si + 1} of ${n}`).toUpperCase();
    }
    if (ui.tip) {                     // tips change every 6 s with a 250 ms cross-fade
      const k = Math.floor(t / 6000) % D.tips.length, ph = t % 6000;
      if (k !== ui.tipIndex) { ui.tipIndex = k; setTip(D.tips[k], ui.tipW); }
      ui.tip.setAttribute("opacity", reduce ? 1 : Math.min(1, ph / 250, (6000 - ph) / 250));
    }
  }
}

build();
hud.on(build);
A.tick(svg, now => update(now));
const segs = (hostId, attr, set) => document.getElementById(hostId)?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  b.parentNode.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); set(b.dataset[attr]); build(); });
segs("ldKind", "kind", v => { kind = v; }); segs("ldDev", "dev", v => { dev = v; });
document.getElementById("ldReplay")?.addEventListener("click", build);
const ctl = document.getElementById("ldLayoutCtl"); if (ctl) hud.controls(ctl, { scale: false });
})();
