
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
let uid = 0; const id = p => `ld${p}${++uid}`;

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
const NOTE = "#FFFF8D", VALUE = "#FFBE23", PROG = "#E06C00", HEAD = "#FF8000", RAIL = "#A0A040", MARK = "#909A49", OBJ = "#B0CD6B";
const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter", CAPS = "font-variant:small-caps";
/* The session's own phases (Session.cpp SetRetainedLoadingPhase): MAP 1/4, WORLD 2/4, ASSETS with the asset queue's
   count, FINISHING 4/4, then LOAD TIME. The durations are illustrative: [phase, ms, share of the bar, queue size]. */
const TIPS = ["Some Strogg drop their guns. Pick them up for ammo.", "Press fire to cut a shotgun reload short.",
  "Armor absorbs two-thirds of the damage you take.", "Aim for the head: most Strogg take double damage."];
const DATA = {
  sp: { name: "Air Defense Trenches", detail: "Corporal", shot: "sp", objectives: "Fight through the trench network to the anti-aircraft battery.",
    phases: [["MAP", 600, .06], ["WORLD", 1100, .14], ["ASSETS", 4800, .74, 1630], ["FINISHING", 500, .06]], tips: TIPS },
  intro: { name: "Air Defense Bunker", detail: "Corporal", shot: "intro", objectives: "",
    phases: [["MAP", 600, .07], ["WORLD", 1000, .14], ["ASSETS", 4400, .73, 1136], ["FINISHING", 400, .06]], tips: TIPS.slice(1) },
  mp: { name: "Heartless", detail: "Capture the Flag", shot: "mp", map: "mp/q4ctf1",
    server: { name: "openQ4 Community · CTF", address: "203.0.113.24:28004", mode: "Capture the Flag", rules: "Capture limit 5 - Time limit 15",
      teamA: ["Kane", "Rhodes", "Strauss", "Cortez"], teamB: ["Makron", "Gladiator", "Iron Maiden"], extra: "Spectating: 1 • Connecting: 2",
      message: "Play fair, call your flag runs and have fun." },
    items: [["machinegun", "Machinegun"], ["shotgun", "Shotgun"], ["hyperblaster", "Hyperblaster"], ["grenadelauncher", "Grenade Launcher"], ["nailgun", "Nailgun"], ["rocketlauncher", "Rocket Launcher"],
      ["railgun", "Railgun"], ["lightninggun", "Lightning Gun"], ["armor", "Large Armor"], ["health", "Large Health"], ["quaddamage", "Quad Damage"], ["item_haste", "Haste"]],
    phases: [["MAP", 500, .08], ["WORLD", 2600, .36], ["ASSETS", 2200, .5, 640], ["FINISHING", 400, .06]], tips: [] },
};
const WEAPON_COLOR = { machinegun: "#FFFF00", shotgun: "#FF8C00", hyperblaster: "#0073FF", grenadelauncher: "#338F12", nailgun: "#99CCCC", rocketlauncher: "#FF3300", railgun: "#00FF00", lightninggun: "#FFFFBA", armor: "#FF0000", health: "#FF8000", quaddamage: "#3F8CFF", item_haste: "#FFCC33" };
let kind = "sp", dev = "mouse", t0 = performance.now(), ui = {};

/* Timeline helpers: cubic-bezier easings and keyed tracks, [ms, value, easing of the interval that starts there]. */
const bez = (x1, y1, x2, y2) => p => { if (p <= 0) return 0; if (p >= 1) return 1; let lo = 0, hi = 1, s = p;
  for (let i = 0; i < 24; i++) { s = (lo + hi) / 2; const x = 3 * (1 - s) * (1 - s) * s * x1 + 3 * (1 - s) * s * s * x2 + s * s * s; if (x < p) lo = s; else hi = s; }
  return 3 * (1 - s) * (1 - s) * s * y1 + 3 * (1 - s) * s * s * y2 + s * s * s; };
const EASE_OUT = bez(.16, 1, .3, 1), ACCEL = bez(.455, .03, .515, .955);
const track = (keys, t) => { if (t <= keys[0][0]) return keys[0][1];
  for (let i = 0; i < keys.length - 1; i++) { const [a, v0, e] = keys[i], [b, v1] = keys[i + 1]; if (t < b) { const p = (t - a) / (b - a); return v0 + (v1 - v0) * (e ? e(p) : p); } }
  return keys[keys.length - 1][1]; };
/* frameIn (section 14.17, Appendix C 1.46): the bands scroll in 120 u and 140 u, decelerating hard for 300 ms past
   their seat by 2.5 u, then lock back over 100 ms, the bottom 40 ms after the top; the furniture each carries trails
   it by 70 ms and seats 320 ms after it starts; each inner edge lights as its band locks; the cards fade in 260-460 ms. */
const PRESENT = 160;                       // the first frame: the levelshot alone, the frame parked
const bandKeys = (lead, park, over) => [[0, park], [lead, park, EASE_OUT], [lead + 300, over, ACCEL], [lead + 400, 0]];
const furnKeys = (lead, park) => [[0, park], [lead, park, EASE_OUT], [lead + 320, 0]];
const flareKeys = lead => [[0, 0], [lead + 230, 0], [lead + 290, 1], [lead + 590, 0]];
const FRAME = { tb: bandKeys(1, -120, 2.5), bb: bandKeys(40, 140, -2.5), ft: furnKeys(70, -120), fb: furnKeys(110, 140),
  flT: flareKeys(1), flB: flareKeys(40), cards: [[0, 0], [260, 0], [460, 1]] };

/* Stock furniture for Classic, in stock units (guis/loading/*.gui, frames.md 1.20). */
const shadowText = (parent, x, y, t, o) => { const g = svgEl("g", {}, parent); svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000", "fill-opacity": .9 }, g).textContent = t; const f = svgEl("text", { ...o, x, y }, g); f.textContent = t; return f; };
const poly = pts => pts.map(([x, y], i) => `${i ? "L" : "M"}${x.toFixed(2)} ${y.toFixed(2)}`).join("");
/* The stock load bands' inner edges: top thick (28.8 u) on the leading side with a riser at 268.8-290 u, then 9.3 u;
   bottom 11.5 u on the leading side, a riser at 165-207.5 u, then 54 u (the MP band sits 118 u higher). */
function classicBands(g, mp) {
  const te = [[0, 28.8], [268.8, 28.8], [290, 9.3], [640, 9.3]], be = [[0, 468.5], [165, 468.5], [207.5, 426], [640, 426]].map(([x, y]) => [x, y - (mp ? 118 : 0)]);
  const spill = svgEl("g", { style: ADD }, g);
  for (let d = 10; d <= 100; d += 10) {
    svgEl("path", { d: poly(te) + poly([...te].reverse().map(([x, y]) => [x, y + d])).replace("M", "L") + "Z", fill: "#181D0A", "fill-opacity": .1 }, spill);
    svgEl("path", { d: poly(be) + poly([...be].reverse().map(([x, y]) => [x, y - d])).replace("M", "L") + "Z", fill: "#181D0A", "fill-opacity": .1 }, spill);
  }
  svgEl("path", { d: "M0 0H640" + poly([...te].reverse()).replace("M", "L") + "Z", fill: "#000", "fill-opacity": .8 }, g);
  svgEl("path", { d: "M0 480H640" + poly([...be].reverse()).replace("M", "L") + "Z", fill: "#000", "fill-opacity": .8 }, g);
}
function classicBracket(g, x, y, sx, sy) {
  const d = `M${x + sx * 22.8} ${y}H${x}V${y + sy * 22.8}`, add = svgEl("g", { style: ADD }, g);
  svgEl("path", { d, fill: "none", stroke: "#5A3418", "stroke-width": 7, "stroke-opacity": .28, filter: `url(#${ui.soft})` }, add);
  svgEl("path", { d, fill: "none", stroke: "#3A3A3A", "stroke-width": .9 }, add);
}
const classicDots = (g, x, y) => { for (let i = 0; i < 10; i++) for (let j = 0; j < 2; j++) svgEl("rect", { x: x + 9.8 + i * 9, y: y + 5.5 + j * 9.4, width: 4.1, height: 3.9, fill: "#1C2613" }, g); };
const strip = (g, x, y, w, h, stops) => { const gid = id("st"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
  stops.forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": v }, lg));
  svgEl("rect", { x, y, width: w, height: h, fill: `url(#${gid})` }, g); };
function classicBar(g, x, y, w, h) {
  const gid = id("cy"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs);
  [[0, .55], [.45, 1], [.55, 1], [1, .5]].forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": PROG, "stop-opacity": v }, lg));
  const shape = (w2) => `M${x} ${y}H${x + w2}V${y + h}H${x + Math.min(h, w2)}L${x} ${y + h - Math.min(h, w2)}Z`;
  svgEl("path", { d: shape(w), fill: `url(#${gid})`, "fill-opacity": .3 }, g);
  const fill = svgEl("path", { d: shape(0), fill: `url(#${gid})`, "fill-opacity": .5 }, g);
  return p => fill.setAttribute("d", shape(Math.max(0, Math.min(1, p)) * w));
}

/* Text measurement (Archivo stands in for Lowpixel, Michroma for Marine, Plex Mono for Profont). */
let meter;
const measure = (t, size, fam = F_L, weight = 500, style = "") => { if (!meter) { meter = svgEl("svg", { width: 0, height: 0, style: "position:absolute;visibility:hidden" }); document.body.appendChild(meter); }
  const e = svgEl("text", { "font-family": fam, "font-size": size, "font-weight": weight, style }, meter); e.textContent = t; const w = e.getComputedTextLength(); e.remove(); return w; };
const wrap = (text, w, size, max) => { const lines = [""];
  text.split(" ").forEach(x => { const l = lines[lines.length - 1], t = l ? l + " " + x : x; if (l && measure(t, size) > w) lines.push(x); else lines[lines.length - 1] = t; });
  return lines.slice(0, max); };
const lines = (parent, x, y, pitch, list, attrs) => { const t = svgEl("text", { x, y, ...attrs }, parent);
  list.forEach((l, i) => { svgEl("tspan", { x, dy: i ? pitch : 0 }, t).textContent = l; }); return t; };
/* Marine stand-in: Michroma in small caps, sized and tracked to the captures' widths (Marine is narrower). */
const MARINE = (size, extra = {}) => ({ "font-family": F_M, "font-size": size, style: CAPS, "letter-spacing": -.1 * size, ...extra });
const LOW = (size, extra = {}) => ({ "font-family": F_L, "font-weight": 500, "font-size": size, ...extra });

function build() {
  svg.innerHTML = ""; ui = { defs: svgEl("defs", {}, svg), done: false, icons: [] };
  ui.soft = id("sf"); svgEl("feGaussianBlur", { stdDeviation: 2.4 }, svgEl("filter", { id: ui.soft, x: "-30%", y: "-30%", width: "160%", height: "160%" }, ui.defs));
  const lay = hud.fit(svg), D = DATA[kind];
  svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: "#000" }, svg);
  if (hud.classic) buildClassic(lay, D, kind === "mp"); else buildRemastered(lay, D);
  hud.regionsLayer(svg, lay);
  t0 = performance.now(); update(t0);
}

/* Classic keeps the stock screens on the 4:3 canvas, pillarboxed or letterboxed. */
function buildClassic(lay, D, mp) {
  const clipId = id("pic"); svgEl("rect", { x: 0, y: 0, width: 640, height: 480 }, svgEl("clipPath", { id: clipId }, ui.defs));
  ui.shot = A.levelshot(svgEl("g", { "clip-path": `url(#${clipId})` }, svg), D.shot, { x0: 0, y0: 0, W: 640, H: 480 });
  classicBands(svg, mp);
  if (kind !== "intro") { const pid = id("gp"), pat = svgEl("pattern", { id: pid, width: 30, height: 30, patternUnits: "userSpaceOnUse" }, ui.defs);
    svgEl("path", { d: "M15 10.35v9.3M10.35 15h9.3", stroke: "#fff", "stroke-opacity": .04, "stroke-width": 1 }, pat);
    svgEl("rect", { x: 0, y: 0, width: 640, height: 480, fill: `url(#${pid})` }, svg); }
  classicDots(svg, 3, 2);
  if (kind !== "intro") {
    const lowY = mp ? 293.4 : 411.4;
    [[14.6, 42.6, 1, 1], [625.4, 42.6, -1, 1], [14.6, lowY, 1, -1], [625.4, lowY, -1, -1]].forEach(p => classicBracket(svg, ...p));
    strip(svg, 291, 14, 349, 22, [[0, 0], [.98, .73], [1, .73]]);
    svgEl("text", { x: 626, y: 28.5, "font-family": F_M, "font-size": 12.1, "letter-spacing": -.6, "text-anchor": "end", fill: "#fff", "fill-opacity": .8 }, svg).textContent = D.name.toUpperCase();
    if (mp) { strip(svg, 291, 36, 349, 18, [[0, 0], [.98, .37], [1, .37]]);
      ui.msg = svgEl("text", { x: 626, y: 48, "text-anchor": "end", ...LOW(10), fill: "#fff", "fill-opacity": .52 }, svg); }
  }
  const y = mp ? 313 : 431;
  ui.bar = classicBar(svg, 235, y + 6, 405, 14);
  ui.status = shadowText(svg, 628, y + 20, "LOADING", { "font-family": F_M, "font-size": 13.4, "text-anchor": "end", fill: "#fff" });
  if (mp) {
    const S = D.server;
    [[S.name, 353, .8], [S.address, 368, .8], [S.mode, 386, .52], ["Capture Limit: 5", 401, .52]].forEach(([t, yy, a]) =>
      svgEl("text", { x: 627, y: yy, "text-anchor": "end", ...LOW(12), fill: "#fff", "fill-opacity": a }, svg).textContent = t);
    ui.info = svgEl("text", { x: 320, y: 421, "text-anchor": "middle", ...LOW(12), fill: "#fff", "fill-opacity": .52 }, svg);
    ui.iconHost = svgEl("g", {}, svg);
  }
}

/* Remastered (build_retained_screens.py loading_document): every part keeps its stock distance from the view edge it
   belongs to. The bands' risers stay 268.8-290 u and 165-207.5 u from the leading edge and their straight spans run
   to the edges; the bottom anchors follow the view's height. */
function buildRemastered(lay, D) {
  const mp = kind === "mp", intro = kind === "intro";
  const VL = lay.x0, VR = lay.x0 + lay.W, VT = lay.y0, VB = lay.y0 + lay.H;
  const X = u => VL + u, XR = u => VR - 640 + u, Y = u => VT + u, YB = u => VB - 480 + u;
  ui.shot = A.levelshot(svgEl("g", {}, svg), D.shot, lay);
  // the + grid: 13 u crosses on a 30 u pitch at 0.04, columns -225..855 u, sixteen rows from the top; not on the intro
  if (!intro) { const pid = id("gp"), pat = svgEl("pattern", { id: pid, x: 0, y: VT, width: 30, height: 30, patternUnits: "userSpaceOnUse" }, ui.defs);
    svgEl("path", { d: "M15 8.5v13M8.5 15h13", stroke: "#fff", "stroke-opacity": .04, "stroke-width": .667 }, pat);
    const x0 = Math.max(VL, -240), x1 = Math.min(VR, 870);
    svgEl("rect", { x: x0, y: VT, width: x1 - x0, height: Math.min(480, lay.H), fill: `url(#${pid})` }, svg); }
  // the bands, each in its own scrolling group with its spill and its inner-edge flare
  const te = [[VL - 60, Y(28.8)], [X(268.8), Y(28.8)], [X(290), Y(9.3)], [VR + 60, Y(9.3)]];
  const be = [[VL - 60, YB(468.5)], [X(165), YB(468.5)], [X(207.5), YB(426)], [VR + 60, YB(426)]];
  const band = (edge, outer) => { const g = svgEl("g", {}, svg);
    // *_edgeadd: additive #181D0A x1.4 ramping from 0.98 at the inner edge to nothing 80 u away, on both sides;
    // one gradient column per edge segment, its ramp measured straight down or up from that segment
    for (let i = 0; i + 1 < edge.length; i++) {
      const [[x0, y0], [x1, y1]] = [edge[i], edge[i + 1]], slope = (y1 - y0) / (x1 - x0), reach = 80 / (slope * slope + 1);
      for (const s of [1, -1]) {
        const gid = id("sp"), lg = svgEl("linearGradient", { id: gid, gradientUnits: "userSpaceOnUse", x1: x0, y1: y0, x2: x0 - s * reach * slope, y2: y0 + s * reach }, ui.defs);
        svgEl("stop", { offset: 0, "stop-color": "#22290E", "stop-opacity": .98 }, lg);
        svgEl("stop", { offset: 1, "stop-color": "#22290E", "stop-opacity": 0 }, lg);
        svgEl("path", { d: poly([[x0, y0], [x1, y1], [x1, y1 + s * 80], [x0, y0 + s * 80]]) + "Z", fill: `url(#${gid})`, style: ADD }, g);
      }
    }
    svgEl("path", { d: poly(edge) + `L${VR + 60} ${outer}L${VL - 60} ${outer}Z`, fill: "#000", "fill-opacity": .8 }, g);
    const flare = [svgEl("path", { d: poly(edge), fill: "none", stroke: PROG, "stroke-opacity": .4, "stroke-width": 5, "stroke-linejoin": "round", style: ADD, opacity: 0 }, g),
      svgEl("path", { d: poly(edge), fill: "none", stroke: "#FFC070", "stroke-opacity": .8, "stroke-width": .8, style: ADD, opacity: 0 }, g)];
    return { g, flare }; };
  const top = band(te, VT - 270), bottom = band(be, VB + 270);
  ui.tb = top.g; ui.bb = bottom.g; ui.flT = top.flare; ui.flB = bottom.flare;
  // load_corner: 24 u legs, a warm 7 u halo, a #3A3A3A core and a 0.8 u white line, all additive; the top-trailing one is left out
  const bracket = (g, x, y, sx, sy) => { const d = `M${x} ${y + sy * 24}V${y}H${x + sx * 24}`;
    [["#502D14", .55, 7], ["#3A3A3A", 1, 1.2], ["#FFFFFF", .55, .8]].forEach(([c, a, w]) => svgEl("path", { d, fill: "none", stroke: c, "stroke-opacity": a, "stroke-width": w, "stroke-linejoin": "miter", style: ADD }, g)); };
  // the top band's furniture: the leading bracket, the dot matrix and the identity
  const ft = ui.ft = svgEl("g", {}, svg);
  if (!intro) bracket(ft, X(22), Y(42), 1, 1);
  for (let c = 0; c < 10; c++) for (let r = 0; r < 2; r++) svgEl("rect", { x: X(12 + 9 * c), y: Y(4 + 9 * r), width: 4, height: 4, fill: "#1C2613" }, ft);
  if (!intro) {
    strip(ft, XR(300), Y(34), 340, 44, [[0, 0], [.5, .55], [1, .8]]);
    svgEl("text", { x: XR(632), y: Y(50.8), "text-anchor": "end", ...MARINE(13.6, { "letter-spacing": .2 }), fill: "#fff", "fill-opacity": .8 }, ft).textContent = D.name;
    svgEl("text", { x: XR(632), y: Y(71.4), "text-anchor": "end", ...LOW(11.33), fill: "#fff", "fill-opacity": .62 }, ft).textContent = D.detail;
  }
  ui.cards = [];
  // the objectives display's open plate (section 14.11) at 18,120 u, 250x180 u: >>MISSION OBJECTIVES over the map's text
  if (!mp && !intro && D.objectives) {
    const g = svgEl("g", { transform: `translate(${X(18)} ${Y(120)})` }, svg); ui.cards.push(g);
    const grad = stops => { const gid = id("og"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 250, y1: 0, y2: 0, gradientUnits: "userSpaceOnUse" }, ui.defs);
      stops.forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, lg)); return `url(#${gid})`; };
    const plate = "M0 0H250V180H20L0 160Z";
    svgEl("path", { d: plate, fill: grad([[0, OBJ, .2], [.3, OBJ, .2], [.56, OBJ, 0], [1, OBJ, 0]]) }, g);
    svgEl("path", { d: "M.5 0V160L20 179.5H250", fill: "none", stroke: grad([[0, OBJ, .4], [.56, OBJ, .4], [1, OBJ, 0]]), "stroke-width": 1 }, g);
    svgEl("path", { d: plate, fill: grad([[0, "#000", .5], [.66, "#000", .5], [1, "#000", 0]]) }, g);
    svgEl("text", { x: 10, y: 18, ...MARINE(8.4, { "letter-spacing": .9 }), fill: HEAD }, g).textContent = ">>MISSION OBJECTIVES";
    lines(g, 10, 38, 12, wrap(D.objectives, 225, 9.33, 11), { ...LOW(9.33), fill: "#D1DEB5" });
  }
  // multiplayer: the section 6 server card, 250 u wide on the leading side, standing on 384 u and growing upward
  if (mp) ui.cards.push(serverCard(svgEl("g", {}, svg), D.server, X(18), YB(384)));
  // the bottom band's furniture: the lower brackets, the tips or arsenal, and the progress
  const fb = ui.fb = svgEl("g", {}, svg);
  if (!intro) { bracket(fb, X(22), YB(412), 1, -1); bracket(fb, XR(618), YB(412), -1, -1); }
  if (mp) { ui.arsenal = { x: X(216), y: YB(436) }; ui.iconHost = svgEl("g", {}, fb); }
  else if (D.tips.length) {
    ui.tips = svgEl("g", {}, fb);
    svgEl("text", { x: X(216), y: YB(435.3), ...LOW(9.33), fill: HEAD }, ui.tips).textContent = "TIP";
    ui.tipSlots = [0, 1].map(() => svgEl("g", { opacity: 0 }, ui.tips)); ui.tipIndex = -1; ui.tipX = X(216); ui.tipY = YB(446.2);
  }
  // LOADING above a 240 u bar ending 8 u from the trailing edge; the phase, its count and the percentage under it
  ui.glyph = svgEl("g", {}, fb);
  ui.promptShadow = svgEl("text", { x: XR(633), y: YB(444.3), "text-anchor": "end", ...MARINE(13.6, { "letter-spacing": 1.9 }), fill: "#000", "fill-opacity": .8 }, fb);
  ui.prompt = svgEl("text", { x: XR(632), y: YB(443.3), "text-anchor": "end", ...MARINE(13.6, { "letter-spacing": 1.9 }), fill: "#fff" }, fb);
  const bx = XR(392), by = YB(452);
  const vgrad = (a0, a1) => { const gid = id("pg"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs);
    [[0, a0], [.5, a1], [1, a0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": PROG, "stop-opacity": a }, lg)); return `url(#${gid})`; };
  const bar = svgEl("g", { transform: `translate(${bx} ${by})` }, fb);
  svgEl("path", { d: "M6 0H240V6H0V6Z", fill: vgrad(.2, .3) }, bar);
  const clipId = id("pc"), clipRect = svgEl("rect", { x: 0, y: -1, width: 0, height: 8 }, svgEl("clipPath", { id: clipId }, ui.defs));
  const fill = svgEl("g", { "clip-path": `url(#${clipId})` }, bar);
  svgEl("path", { d: "M6 0H240V6H0V6Z", fill: vgrad(.35, .6) }, fill);
  svgEl("path", { d: "M3 3H240", stroke: "#FFC070", "stroke-opacity": .5, "stroke-width": .667 }, fill);
  ui.bar = p => clipRect.setAttribute("width", (Math.max(0, Math.min(1, p)) * 240).toFixed(2));
  ui.phase = svgEl("text", { x: bx, y: YB(468.7), ...LOW(9.33), fill: "#fff" }, fb);
  ui.phaseName = svgEl("tspan", { "fill-opacity": .62 }, ui.phase); ui.phaseCount = svgEl("tspan", { dx: 5.33, "fill-opacity": .42 }, ui.phase);
  ui.pct = svgEl("text", { x: XR(632), y: YB(468.7), "text-anchor": "end", ...LOW(9.33), fill: VALUE }, fb);
}

/* The server card (section 6 construction): 0.94 black body inside one #A0A040 rail, 8 and 2 u cuts, a 26.7 u header
   holding the marker and the name; the address in Profont, the mode, the limits, the roster by team with spectators
   and players still connecting, and the server's message in the notices color. Its parts flow upward from 384 u. */
function serverCard(g, S, x, bottom) {
  const w = 250, head = 26.667, roster = Math.max(S.teamA.length, S.teamB.length);
  const msg = wrap(S.message, 230, 10.67, 3);
  const h = head + 5.333 + 13.333 * 3 + (14.667 + roster * 10.667 + 10.667) + (4 + msg.length * 13.333) + 8;
  g.setAttribute("transform", `translate(${x} ${bottom - h})`);
  const outline = `M2 0H242L250 8V${h - 2}L248 ${h}H8L0 ${h - 8}V2Z`;
  svgEl("path", { d: outline, fill: "#000", "fill-opacity": .94 }, g);
  const gid = id("cw"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
  [[0, .08], [.5, .08], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#fff", "stop-opacity": a }, lg));
  svgEl("path", { d: `M2 .5H242L${242 + head * .3} ${head}H.5V2Z`, fill: `url(#${gid})` }, g);
  svgEl("path", { d: `M0 ${head}H250`, stroke: RAIL, "stroke-opacity": .5, "stroke-width": .667 }, g);
  svgEl("path", { d: outline, fill: "none", stroke: RAIL, "stroke-width": 1 }, g);
  svgEl("path", { d: "M9.33 10.67h5.33v5.33z", fill: MARK }, g);
  svgEl("text", { x: 20, y: 16.6, ...MARINE(9.33, { "letter-spacing": .4 }), fill: "#fff", "fill-opacity": .9 }, g).textContent = S.name;
  let y = head + 5.333;
  const line = (t, attrs, a) => { svgEl("text", { x: 10, y: y + 10, ...attrs, fill: "#fff", "fill-opacity": a }, g).textContent = t; y += 13.333; };
  line(S.address, { "font-family": F_P, "font-size": 8.67 }, .62);
  line(S.mode, LOW(11.33), .8);
  line(S.rules, LOW(11.33), .62);
  [[S.teamA, "MARINES", "#6AA42B", 10], [S.teamB, "STROGG", "#FF7B04", 129]].forEach(([names, label, c, cx]) => {
    svgEl("text", { x: cx, y: y + 10.6, ...LOW(8.67), fill: c }, g).textContent = label;
    lines(g, cx, y + 14.667 + 8, 10.667, names, { ...LOW(9.33), fill: "#fff", "fill-opacity": .8 }); });
  svgEl("text", { x: 10, y: y + 14.667 + roster * 10.667 + 7.8, ...LOW(8.67), fill: "#999999" }, g).textContent = S.extra;
  y += 14.667 + roster * 10.667 + 10.667 + 4;
  lines(g, 10, y + 9.6, 13.333, msg, { ...LOW(10.67), fill: NOTE });
  return g;
}

function update(now) {
  const D = DATA[kind], mp = kind === "mp", classic = hud.classic, t = Math.max(0, now - t0);   // a frame can start just before t0
  const P = D.phases, total = P.reduce((a, s) => a + s[1], 0);
  let acc = 0, frac = 0, pi = P.length - 1, within = 1;
  for (let i = 0; i < P.length; i++) { if (t < acc + P[i][1]) { within = (t - acc) / P[i][1]; frac += P[i][2] * within; pi = i; break; } acc += P[i][1]; frac += P[i][2]; }
  const done = t >= total;
  if (done) frac = 1;
  ui.bar(frac);
  ui.shot.set(reduce ? 0 : .03 * frac);
  // multiplayer: one icon per kind of item as it spawns, during WORLD, each fading in over 150 ms
  let item = null;
  if (mp && ui.iconHost) {
    const wi = P.findIndex(s => s[0] === "WORLD"), start = P.slice(0, wi).reduce((a, s) => a + s[1], 0), dur = P[wi][1];
    const shown = t < start ? 0 : Math.min(D.items.length, Math.floor((t - start) / dur * D.items.length) + 1);
    if (t >= start && t < start + dur) item = D.items[shown - 1];
    while (ui.icons.length < shown) {
      const k = ui.icons.length, [key] = D.items[k];
      const x = classic ? (640 - (24 * D.items.length + 24)) / 2 + 24 * k + 2 : ui.arsenal.x + (k % 10) * 17;
      const y = classic ? 432 : ui.arsenal.y + Math.floor(k / 10) * 18, h = classic ? 20 : 16;
      const g = svgEl("g", { opacity: 0 }, ui.iconHost); ui.icons.push(g);
      A.icon(g, key, { x, y, h, color: classic ? "#FFFFFF" : WEAPON_COLOR[key] || "#DDEBC3" });
      const s0 = performance.now(), fade = reduce ? 80 : 150;
      const step = n => { const p = Math.min(1, (n - s0) / fade); g.setAttribute("opacity", p); if (p < 1) requestAnimationFrame(step); }; requestAnimationFrame(step);
    }
  }
  const pulse = c => c < 500 ? 1 - .5 * c / 500 : .5 + .5 * (c - 500) / 200;
  if (classic) {
    if (mp) { const msg = item ? item[1] : done ? "" : P[pi][0] === "MAP" ? D.map : ""; if (ui.msg) ui.msg.textContent = msg; if (ui.info) ui.info.textContent = msg; }
    const label = done && !mp ? "- CLICK TO CONTINUE -" : "LOADING";
    ui.status.textContent = label; ui.status.previousSibling.textContent = label;
    const v = done && !mp && !reduce ? Math.round(255 * pulse((t - total) % 700)) : 255;
    ui.status.setAttribute("fill", `rgb(${v},${v},${v})`);
    return;
  }
  /* the entrance */
  const te = t - PRESENT;
  const at = (keys, park) => reduce ? keys[keys.length - 1][1] : te < 0 ? park : track(keys, te);
  ui.tb.setAttribute("transform", `translate(0 ${at(FRAME.tb, -120).toFixed(2)})`);
  ui.bb.setAttribute("transform", `translate(0 ${at(FRAME.bb, 140).toFixed(2)})`);
  ui.ft.setAttribute("transform", `translate(0 ${at(FRAME.ft, -120).toFixed(2)})`);
  ui.fb.setAttribute("transform", `translate(0 ${at(FRAME.fb, 140).toFixed(2)})`);
  const fT = reduce || te < 0 ? 0 : track(FRAME.flT, te), fB = reduce || te < 0 ? 0 : track(FRAME.flB, te);
  ui.flT.forEach(p => p.setAttribute("opacity", fT.toFixed(3))); ui.flB.forEach(p => p.setAttribute("opacity", fB.toFixed(3)));
  const cards = reduce ? Math.min(1, t / 80) : te < 0 ? 0 : track(FRAME.cards, te);
  ui.cards.forEach(g => g.setAttribute("opacity", cards.toFixed(3)));
  /* the progress readout and the continue prompt */
  const pad = dev === "pad", ready = done && !mp;
  const label = mp ? (done ? "JOINING" : "LOADING") : ready ? (pad ? "CONTINUE" : "- PRESS ANY KEY TO CONTINUE -") : "LOADING";
  if (ui.prompt.textContent !== label) {
    ui.prompt.textContent = ui.promptShadow.textContent = label; ui.glyph.innerHTML = "";
    if (ready && pad) {                     // the active controller family's south button, 12 u, 5.3 u before the text
      const x = Number(ui.prompt.getAttribute("x")) - ui.prompt.getComputedTextLength() - 5.33 - 6, y = Number(ui.prompt.getAttribute("y")) - 4.6;
      svgEl("circle", { cx: x, cy: y, r: 6, fill: "#fff" }, ui.glyph);
      svgEl("text", { x, y: y + 3.1, "text-anchor": "middle", "font-family": F_L, "font-weight": 700, "font-size": 8.4, fill: "#10140A" }, ui.glyph).textContent = "A";
    }
  }
  ui.prompt.setAttribute("fill-opacity", ready && !reduce ? pulse((t - total) % 700).toFixed(3) : 1);
  const [name, , , queue] = P[pi];
  ui.phaseName.textContent = done ? "LOAD TIME" : name;
  ui.phaseCount.textContent = done ? `${(total / 1000).toFixed(1)} s` : queue ? `${Math.floor(within * queue)}/${queue}` : `${{ MAP: 1, WORLD: 2, FINISHING: 4 }[name]}/4`;
  ui.pct.textContent = `${Math.round(frac * 100)}%`;
  /* tips: two slots cross-fading over 250 ms every 6 s at 0.70, gone once the level is ready */
  if (ui.tips) {
    ui.tips.setAttribute("display", done ? "none" : "inline");
    const k = Math.floor(t / 6000), ph = t % 6000;
    if (k !== ui.tipIndex) { ui.tipIndex = k; const slot = ui.tipSlots[k % 2]; slot.innerHTML = "";
      lines(slot, ui.tipX, ui.tipY, 10.5, wrap(D.tips[k % D.tips.length], 168, 9.33, 2), { ...LOW(9.33), fill: "#fff", "fill-opacity": .7 }); }
    const a = reduce || k === 0 ? 1 : Math.min(1, ph / 250);
    ui.tipSlots[k % 2].setAttribute("opacity", a); ui.tipSlots[(k + 1) % 2].setAttribute("opacity", k === 0 ? 0 : 1 - a);
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
