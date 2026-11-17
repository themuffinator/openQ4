
(() => {
/* The Awakening's vehicle displays (section 14.12), measured from the expansion's GUIs (findings/awakening_hud.md):
   the space cannon's ship-status panel and lock-on reticule, the MCC cannon's hull readout, the speeder bike's race
   HUD and speedometer, and the GEV HUD, whose art the expansion never shipped (openQ4 design). Remastered layout on a
   16:9 view; the stock coordinates are the 640x480 canvas. */
const A = window.__atlas, { svgEl } = A;
const svg = document.getElementById("awkSvg");
if (!svg) return;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const F_M = "Michroma, Eurostile, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_L = "Archivo, sans-serif";
const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter";
const marine = s => 33.5 * s, chain = s => 46 * s;
let n = 0; const uid = p => `awk${p}${++n}`;
const later = f => document.readyState === "loading" ? document.addEventListener("DOMContentLoaded", f, { once: true }) : f();
const poly = pts => "M" + pts.map(([x, y]) => `${x} ${y}`).join("L") + "Z";
const text = (parent, x, y, t, o) => { const e = svgEl("text", { x, y, ...o }, parent); e.textContent = t; return e; };
const radial = (defs, cx, cy, r, stops) => { const g = svgEl("radialGradient", { id: uid("rg"), gradientUnits: "userSpaceOnUse", cx, cy, r }, defs);
  stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: Math.min(1, o / r), "stop-color": c, "stop-opacity": a }, g)); return `url(#${g.id})`; };
const arcPath = (cx, cy, r, a0, a1) => { const p = a => [cx + r * Math.cos(a * Math.PI / 180), cy + r * Math.sin(a * Math.PI / 180)];
  const [x0, y0] = p(a0), [x1, y1] = p(a1); return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`; };
/* Angles in the findings run counter-clockwise from east; SVG angles run clockwise. */
const ccw = a => 360 - a;

/* ---------------- Space cannon: the ship-status panel (turretplayerhud.gui :2258, 128x128 at 512,32) ---------------- */
const SHIP = [[62.7,17.0],[58.6,20.5],[56.9,28.0],[51.0,33.5],[47.5,38.5],[47.5,40.0],[34.0,49.5],[34.6,61.5],[35.8,61.5],[37.0,63.5],[48.1,65.5],[51.0,69.5],[51.0,76.5],[52.2,78.0],[55.1,79.0],[57.5,94.0],[52.2,102.0],[50.4,103.5],[45.2,105.0],[44.6,109.0],[61.0,109.0],[62.2,110.5],[66.3,110.5],[67.4,109.0],[74.5,108.5],[83.8,109.0],[83.2,105.0],[78.0,103.5],[70.9,94.5],[73.3,79.0],[76.2,78.0],[77.4,76.5],[77.4,69.0],[79.1,66.5],[85.6,64.0],[90.9,63.5],[94.4,61.0],[94.4,49.5],[80.9,40.0],[80.9,38.5],[77.4,33.5],[71.5,28.0],[70.9,23.5],[68.6,19.0],[65.7,17.0]];
const DAMAGE = [    // left wing, right wing, nose, tail, mid body: lit at 80, 60, 40, 20 and 10% of the dropship's health
  [[48.7,41.0],[47.5,41.0],[42.2,45.0],[38.7,46.5],[34.6,50.0],[34.6,60.5],[36.4,61.0],[38.1,63.0],[43.4,63.5],[45.8,65.0],[48.7,65.0]],
  [[79.7,42.0],[79.7,65.0],[82.7,65.0],[85.0,63.5],[90.3,63.0],[92.0,61.0],[93.8,60.5],[93.8,50.0],[83.2,42.5]],
  [[48.1,39.0],[48.1,40.0],[80.3,40.0],[76.8,34.0],[70.9,28.5],[70.9,26.0],[68.0,19.5],[65.1,17.5],[63.3,17.5],[60.4,19.5],[58.1,24.0],[57.5,28.5],[51.6,34.0],[49.9,37.5]],
  [[57.4,91.5],[70.9,91.5],[70.9,94.5],[78.0,103.5],[83.2,105.0],[83.2,110.5],[45.2,110.5],[45.2,105.0],[50.4,103.5],[52.2,102.0],[57.5,94.0]],
  [[50.4,67.0],[51.6,68.5],[51.6,76.0],[55.7,79.0],[72.7,79.0],[76.8,76.0],[76.8,68.5],[78.0,67.0]],
];
const MARKERS = [[56, 83], [64, 79], [55, 66], [54, 51], [65, 37]];
const WARN = { N: [65, 9], NE: [107, 25], E: [114, 63], SE: [105, 103], S: [64, 122], SW: [23, 104], W: [13, 62], NW: [21, 23] };
const WARN_ANGLE = { N: 0, NE: 45, E: 90, SE: 135, S: 180, SW: 225, W: 270, NW: 315 };
function shipPanel(parent, defs, x, y) {
  const g = svgEl("g", { transform: `translate(${x} ${y})` }, parent);
  const clip = svgEl("clipPath", { id: uid("sc") }, defs); svgEl("rect", { width: 128, height: 128 }, clip);
  const c = svgEl("g", { "clip-path": `url(#${clip.id})` }, g);
  /* hud_base_01: an olive disc r 58.75 with a brightening rim band, opaque in the stock (Remastered 0.85) */
  svgEl("circle", { cx: 64, cy: 64, r: 58.75, fill: "#1F2A13", "fill-opacity": .85 }, c);
  svgEl("circle", { cx: 64, cy: 64, r: 57.6, fill: "none", stroke: radial(defs, 64, 64, 58.75, [[56.4, "#4D682F"], [58.75, "#64883D"]]), "stroke-width": 2.3 }, c);
  /* hud_base_02 at 0.501: a black centre shade, full to r 28, half at r 40, gone by r 56 */
  svgEl("circle", { cx: 64, cy: 64, r: 56, fill: radial(defs, 64, 64, 56, [[0, "#000", .5], [28, "#000", .5], [40, "#000", .25], [56, "#000", 0]]) }, c);
  /* viewtracking (hud_base_03, additive): a 52-degree cone to 33.7 u from a small hub, turning with the gun's yaw */
  const cone = svgEl("g", { style: ADD }, c);
  const coneFill = svgEl("linearGradient", { id: uid("cg"), x1: 0, y1: 1, x2: 0, y2: 0 }, defs);
  [[0, "#585E29", .7], [1, "#585E29", .14]].forEach(([o, col, a]) => svgEl("stop", { offset: o, "stop-color": col, "stop-opacity": a }, coneFill));
  const reach = 33.7, half = 26 * Math.PI / 180;
  svgEl("path", { d: `M64 64L${64 - reach * Math.sin(half)} ${64 - reach * Math.cos(half)}A${reach} ${reach} 0 0 1 ${64 + reach * Math.sin(half)} ${64 - reach * Math.cos(half)}Z`, fill: `url(#${coneFill.id})` }, cone);
  svgEl("path", { d: `M64 64L${64 - reach * Math.sin(half)} ${64 - reach * Math.cos(half)}M64 64L${64 + reach * Math.sin(half)} ${64 - reach * Math.cos(half)}`, stroke: "#585E29", "stroke-width": 1.3, "stroke-opacity": .95 }, cone);
  svgEl("circle", { cx: 64, cy: 64, r: 2.35, fill: "none", stroke: "#585E29", "stroke-width": 1.9, "stroke-opacity": .55 }, cone);
  /* hud_ship_01: the dropship from above, nose up */
  svgEl("path", { d: poly(SHIP), fill: "#475736", stroke: "#63883C", "stroke-width": 1.1, "stroke-linejoin": "round" }, c);
  const dmg = DAMAGE.map(p => svgEl("path", { d: poly(p), fill: "#A13C2B", stroke: "#76351D", "stroke-width": .9, "stroke-linejoin": "round", opacity: 0 }, c));
  /* hud_target_01 (additive, 16 u, 0.35 turn a second): a dark-gold disc with two rim ticks in a broken gold ring */
  const marks = MARKERS.map(([mx, my]) => {
    const m = svgEl("g", { transform: `translate(${mx} ${my})`, style: ADD, opacity: 0 }, c), r = svgEl("g", {}, m);
    svgEl("circle", { r: 3.75, fill: "#695900" }, r);
    [145, 325].forEach(a => svgEl("path", { d: arcPath(0, 0, 3.3, ccw(a) - 10, ccw(a) + 10), fill: "none", stroke: "#FFD800", "stroke-width": .9 }, r));
    [[20, 95], [200, 275], [110, 125], [140, 155], [170, 185], [290, 305], [320, 335], [350, 365]].forEach(([a0, a1]) =>
      svgEl("path", { d: arcPath(0, 0, 5.875, ccw(a1), ccw(a0)), fill: "none", stroke: "#FFD800", "stroke-width": 1.25 }, r));
    return { m, r };
  });
  /* hud_warning_<dir> (additive, 24 u): a red dart pointing at the panel's centre, with faint arcs and dashes */
  const warns = Object.entries(WARN).map(([k, [wx, wy]]) => {
    const w = svgEl("g", { transform: `translate(${wx} ${wy}) rotate(${WARN_ANGLE[k]})`, style: ADD, opacity: 0 }, c);
    svgEl("path", { d: arcPath(0, 0, 6.6, 200, 340) + arcPath(0, 0, 6.6, 20, 160), fill: "none", stroke: "#760000", "stroke-width": 2.6, "stroke-opacity": .5 }, w);
    svgEl("path", { d: "M-4.1 -5.25L0 7.1L3.75 -4.9L1.5 -1.9L-1.5 -1.9L-3.75 -4.1Z", fill: "#D50000" }, w);
    return { k, w, t0: -1e9 };
  });
  return { g, cone, dmg, marks, warns };
}

/* ---------------- The lock-on reticule (cursor.gui's combat crosshair, 32 u) and its guides ---------------- */
const STATES = [
  ["free", "aa_space_default", "#FFFFFF", "Free · white"], ["acquire", "aa_space_acquire", "#FFFF00", "Acquiring · #FFFF00"],
  ["lock", "aa_space_acquire", "#40FF40", "Locked · #40FF40"], ["friendly", "aa_space_friendly", "#FF1A1A", "Friendly · #FF1A1A"],
  ["oor", "aa_space_oor", "#FFFFFF", "Out of range · white"]];
function reticule(parent, defs) {
  const g = svgEl("g", {}, parent);
  /* a Strogg flyer ahead and above the aim point, its guide sprite (additive, 300 world units, about 48 u here) */
  const target = svgEl("g", { transform: "translate(392 178)" }, g);
  svgEl("path", { d: "M-22 2L-6 -5L0 -9L6 -5L22 2L8 1L4 7L-4 7L-8 1Z", fill: "#1B1F22", stroke: "#3A4045", "stroke-width": .8 }, target);
  svgEl("circle", { cx: 0, cy: -2, r: 1.6, fill: "#FF5A1A" }, target);
  const guide = svgEl("svg", { x: -24, y: -26, width: 48, height: 48, viewBox: "0 0 64 64", overflow: "visible", style: ADD }, target);
  const xh = svgEl("svg", { x: 304, y: 224, width: 32, height: 32, viewBox: "0 0 64 64", overflow: "visible" }, g);
  let spin = 0;
  return {
    set(state) {
      const [, name, col] = STATES.find(s => s[0] === state);
      xh.innerHTML = A.uniqueIds((A.CROSSHAIRS[name]?.svg || "").replace(/#FFFFFF/gi, col));
      const gd = state === "lock" ? ["aa_space_lock_fx", "#E60000", 3] : state === "acquire" ? ["nailgun_scope_lock", "#F8C001", 1]
        : state === "oor" ? ["aa_space_oor_fx", "#C0C0C0", 1] : state === "free" ? ["nailgun_scope_lock", "#808080", 1] : null;
      guide.innerHTML = gd ? Array.from({ length: gd[2] }, () => `<g class="awkSpin">${A.uniqueIds(A.CROSSHAIRS[gd[0]].svg.replace(/#FFFFFF/gi, gd[1]))}</g>`).join("") : "";
      guide.dataset.spin = gd && gd[0] !== "aa_space_oor_fx" ? 1 : 0;
    },
    tick(now) { if (guide.dataset.spin === "1" && !reduce) guide.querySelectorAll(".awkSpin").forEach(e => e.setAttribute("transform", `rotate(${(now * .108) % 360} 32 32)`)); },
  };
}

/* ---------------- MCC cannon: the U.S.S. Hannibal readout (cannonplayerhud.gui, group 469,64) ---------------- */
/* The gun itself takes no damage: it inherits the space cannon's noDamage, health 0 and god mode for its gunner. The
   readout is therefore the MCC's hull. Remastered stacks it under the transmission display (which the side gun's HUD
   lacks), fills the track in proportion and warns at 25% as the vehicle gauges do; the stock track is static. */
function mccReadout(parent, defs, dy, remastered) {
  const g = svgEl("g", { transform: `translate(0 ${dy})` }, parent);
  /* the stock gauge plate (backbar) and track (valbar), 125x59 scaled to 136x70 at 494,80 and sheared 0,-.22 */
  const BACK = "M1 10.4Q1 9.2 2.2 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H2.2Q1 49.3 1 48.1Z";
  const FILL = "M5.4 14.8Q5.4 13.8 6.4 13.8H118.6Q119.6 13.8 119.6 14.8V34.6H105.4A16.6 16.6 0 0 0 93.6 44.7H6.4Q5.4 44.7 5.4 43.7Z";
  const gauge = svgEl("g", { transform: "matrix(1 0 -.22 1 25.3 0) translate(494 80) scale(1.088 1.186)" }, g);
  svgEl("path", { d: BACK, fill: "#000", "fill-opacity": .5 }, gauge);
  const track = svgEl("path", { d: FILL, fill: "#B3D06E", "fill-opacity": .2 }, gauge);
  const fc = svgEl("clipPath", { id: uid("mf") }, defs), fr = svgEl("rect", { x: 0, y: 0, width: 125, height: 59 }, fc);
  const fill = remastered ? svgEl("path", { d: FILL, fill: "#B3D06E", "fill-opacity": .35, "clip-path": `url(#${fc.id})` }, gauge) : null;
  text(g, 512, 89, "U.S.S.", { "font-family": F_M, "font-size": marine(.25), fill: "#FFFFFF", "fill-opacity": .74 });
  text(g, 560, 89, "Hannibal", { "font-family": F_M, "font-size": marine(.25), fill: "#FFFFFF", "fill-opacity": .74 });
  const num = text(g, 585, 128, "2000", { "font-family": F_M, "font-size": marine(.5), "text-anchor": "end", fill: "#F1F0C9", "fill-opacity": .733 });
  return { set(h) {
    num.textContent = String(Math.round(2000 * h)); fr.setAttribute("width", (5.4 + 114.2 * h).toFixed(1));
    if (!remastered) return;
    const warn = h <= .25, c = warn ? "#FF3300" : "#B3D06E";
    track.setAttribute("fill", c); track.setAttribute("fill-opacity", warn ? .6 : .2); track.classList.toggle("rim-on", warn && !reduce);
    fill.setAttribute("fill", c); fill.setAttribute("fill-opacity", warn ? .6 : .35); num.setAttribute("fill", warn ? "#FF3300" : "#F1F0C9");
  } };
}

/* ---------------- Speeder bike: race progress and timer (speederplayerhud.gui) ---------------- */
function speederHud(parent, defs, classic) {
  const g = svgEl("g", {}, parent);
  /* progress_base: a 45% black bar with a raised tab behind the timer; the group sits 1 u high */
  svgEl("path", { d: poly([[68.1, 59.6], [554.6, 60.5], [571.4, 23.5], [125.0, 22.5], [115.9, 15.0], [85.4, 15.0], [83.4, 16.4]]), fill: "#000", "fill-opacity": .454 }, g);
  const PIP = [[238.0, 26.4], [230.5, 55.0], [251.6, 55.5], [259.1, 26.9]];
  const xs = [215, 241, 267, 293, 319, 374, 400, 426, 452, 478];
  const pips = xs.map(x => { const dx = x - 215;
    svgEl("path", { d: poly(PIP.map(([px, py]) => [px + dx, py])), fill: "#4E5B1C", "fill-opacity": .768, stroke: "#4E5B1C", "stroke-opacity": .5, "stroke-width": 1.2 }, g);
    return svgEl("path", { d: poly(PIP.map(([px, py]) => [px + dx, py])), fill: "#798D2A", "fill-opacity": .76, stroke: "#B9CF62", "stroke-opacity": .55, "stroke-width": 1.2, opacity: 0 }, g); });
  /* the timer: Marine 0.65 seconds trailing at 158, a point, Marine 0.55 hundredths, gold, on baseline 54 */
  const sec = text(g, 158, 54, "60", { "font-family": F_M, "font-size": marine(.65), "text-anchor": "end", fill: "#E1CC67", "letter-spacing": 1 });
  text(g, 156, 54, ".", { "font-family": F_M, "font-size": marine(.55), fill: "#E2CC68" });
  const hun = text(g, 164, 54, "00", { "font-family": F_M, "font-size": marine(.55), fill: "#DFCC66", "letter-spacing": 1 });
  /* Classic: the expansion's boosting display at its own place, the hazard icon at 70% and "Boosting..." in red.
     Remastered carries the boost state in the HUD prompt. In Classic every anchor is the 4:3 canvas. */
  let boost = null;
  if (classic) {
    boost = svgEl("g", { opacity: 0 }, g);
    const tri = svgEl("g", { transform: "translate(560 366) scale(.52)", opacity: .698 }, boost);
    svgEl("path", { d: "M63 18Q66 13 69 18L113 102Q115 107 109 107H16Q10 107 12 102Z", fill: "#E62323", stroke: "#1C0202", "stroke-width": 4, "stroke-linejoin": "round" }, tri);
    svgEl("rect", { x: 56, y: 45, width: 14, height: 34, rx: 6, fill: "#1C0202" }, tri); svgEl("circle", { cx: 63, cy: 88, r: 6, fill: "#1C0202" }, tri);
    text(boost, 629, 435, "Boosting...", { "font-family": F_M, "font-size": marine(.25), "text-anchor": "end", fill: "#FF0000" });
  }
  return { pips, sec, hun, boost };
}

/* ---------------- GEV: the expansion names ten materials that exist nowhere; openQ4 draws them ---------------- */
function gevHud(parent, defs) {
  const g = svgEl("g", {}, parent);                     // placed by the caller: bottom-leading in Classic, centered in Remastered
  const sh = (cy) => `matrix(1 0 -.32 1 ${(.32 * cy).toFixed(2)} 0)`;
  /* plates: black 0.60, sheared 0,-.32 */
  svgEl("rect", { x: 16, y: 412, width: 97, height: 60, rx: 2, fill: "#000", "fill-opacity": .6, transform: sh(442) }, g);
  svgEl("rect", { x: 120, y: 412, width: 83, height: 60, rx: 2, fill: "#000", "fill-opacity": .6, transform: sh(442) }, g);
  /* health: a vertical bar, track #B0C891 at 0.30, well black 0.65, fill #FF9E1A draining from the top */
  svgEl("rect", { x: 21, y: 416, width: 23, height: 52, rx: 2, fill: "#B0C891", "fill-opacity": .3, transform: sh(442) }, g);
  svgEl("rect", { x: 25, y: 419, width: 15, height: 46, rx: 1.4, fill: "#000", "fill-opacity": .65, transform: sh(442) }, g);
  const hp = svgEl("rect", { x: 25, y: 419, width: 15, height: 46, rx: 1.4, fill: "#FF9E1A", transform: sh(442) }, g);
  /* the hull from above (turning with the view's yaw relative to the hull) under a fixed turret, both #B0C891 */
  const glyph = svgEl("g", { transform: "translate(77 444)" }, g);
  const hull = svgEl("g", {}, glyph);
  svgEl("path", { d: "M-10 -17Q0 -21 10 -17L13 14Q0 19 -13 14Z", fill: "none", stroke: "#B0C891", "stroke-width": 2.2, "stroke-linejoin": "round" }, hull);
  svgEl("path", { d: "M-16 -11V10M16 -11V10", stroke: "#B0C891", "stroke-width": 3, "stroke-linecap": "round" }, hull);
  svgEl("circle", { r: 6.5, fill: "#B0C891", "fill-opacity": .85 }, glyph);
  svgEl("rect", { x: -1.6, y: -22, width: 3.2, height: 16, rx: 1, fill: "#B0C891" }, glyph);
  /* three shield stages as arcs on both sides: #D1AF47 from 0.8, #C6C251 from 0.4, #A2B85C from 0 */
  const stages = [[27, "#D1AF47"], [23.5, "#C6C251"], [20, "#A2B85C"]].map(([r, col]) =>
    svgEl("path", { d: arcPath(0, 0, r, 125, 235) + arcPath(0, 0, r, -55, 55), fill: "none", stroke: col, "stroke-width": 2.6, "stroke-linecap": "round" }, glyph));
  const out = svgEl("path", { d: arcPath(0, 0, 18, 140, 220) + arcPath(0, 0, 18, -40, 40), fill: "none", stroke: "#FF591A", "stroke-width": 3, "stroke-linecap": "round", opacity: 0 }, glyph);
  /* the weapon: its glyph turned 60 degrees, an infinity mark or a rocket count, and the charge bar */
  const weap = svgEl("g", {}, g);
  svgEl("rect", { x: 124, y: 459, width: 63, height: 6, rx: 3, fill: "#B0C891", "fill-opacity": .3, transform: sh(462) }, g);
  const charge = svgEl("rect", { x: 129, y: 461, width: 53, height: 2, rx: 1, fill: "#FF9E1A", transform: sh(462) }, g);
  return { hp, hull, stages, out, weap, charge };
}

/* ---------------- On-model screens (640x480 canvases on the cockpit and the bike) ---------------- */
const screens = document.getElementById("awkScreens");
const screen = (title, draw) => { const f = document.createElement("figure"), s = svgEl("svg", { viewBox: "0 0 640 480", role: "img", "aria-label": title });
  svgEl("rect", { width: 640, height: 480, fill: "#070806" }, s); const api = draw(s, svgEl("defs", {}, s));
  const c = document.createElement("figcaption"); c.innerHTML = title; f.append(s, c); screens?.appendChild(f); f.dataset.v = api?.v || ""; return Object.assign(api || {}, { f }); };
const olivePanel = (s, x, y, w, h) => { svgEl("rect", { x, y, width: w, height: h, rx: 6, fill: "#2E3219", stroke: "#8C9A5A", "stroke-opacity": .45, "stroke-width": 2 }, s);
  svgEl("path", { d: `M${x + 10} ${y + h + 8}H${x + w - 10}M${x + 10} ${y + h + 18}H${x + w - 10}`, stroke: "#8C9A5A", "stroke-opacity": .35, "stroke-width": 2 }, s); };
const corners = (s, x0, y0, x1, y1, col, a) => [[x0, y0, 1, 1], [x1, y0, -1, 1], [x0, y1, 1, -1], [x1, y1, -1, -1]].forEach(([x, y, sx, sy]) =>
  svgEl("path", { d: `M${x + sx * 70} ${y}H${x + sx * 8}Q${x} ${y} ${x} ${y + sy * 8}V${y + sy * 32}`, fill: "none", stroke: col, "stroke-opacity": a, "stroke-width": 6, style: ADD }, s));
const sel = screen("<b>Selector</b> · spaceturret_main: the active weapon's row lit, ACTIVE or JAMMED, kills", s => {
  olivePanel(s, 40, 81, 560, 384);
  text(s, 308, 131, "Turret", { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), "text-anchor": "end", fill: "#FFFFFF", "fill-opacity": .6 });
  text(s, 319, 131, "Status", { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), fill: "#FFFFFF", "fill-opacity": .6 });
  corners(s, 92, 120, 548, 276, "#CDDAA6", .45);
  const rows = [[141, 193, "1.", "Guns"], [199, 253, "2.", "Rockets"]].map(([y0, y1, nm, lb]) => {
    const band = svgEl("rect", { x: 125, y: y0, width: 390, height: y1 - y0, fill: "#9EB06A", "fill-opacity": .2 }, s);
    const a = text(s, 189, y1 - 13, nm, { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), "text-anchor": "end", fill: "#FFFFFF" });
    const b = text(s, 209, y1 - 13, lb, { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), fill: "#FFFFFF" });
    return { band, a, b }; });
  const status = text(s, 320, 285, "ACTIVE", { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), "text-anchor": "middle", fill: "#D6E98C" });
  const kills = text(s, 320, 340, "12", { "font-family": F_C, "font-weight": 600, "font-size": chain(.75), "text-anchor": "middle", fill: "#CCE6CC" });
  [60, 535].forEach(x => svgEl("rect", { x, y: 311, width: 45, height: 36, fill: "#9ED67C", "fill-opacity": .14 }, s));
  return { v: "cannon mcc", set(guns, jammed, k) { rows.forEach((r, i) => { const on = (i === 0) === guns; r.band.setAttribute("fill-opacity", on ? .37 : .2);
      r.a.setAttribute("fill-opacity", on ? .796 : .247); r.b.setAttribute("fill-opacity", on ? .776 : .247); });
    status.textContent = jammed ? "JAMMED" : "ACTIVE"; status.setAttribute("fill", jammed ? "#FF0000" : "#D6E98C"); kills.textContent = k; } };
});
const rack = screen("<b>Rocket rack</b> · spaceturret_rocket: the lock frame and five rockets", (s, defs) => {
  svgEl("rect", { x: 12, y: 22, width: 600, height: 71, rx: 4, fill: "#3D4225", "fill-opacity": .55 }, s);
  svgEl("rect", { x: 12, y: 268, width: 600, height: 120, rx: 4, fill: "#3D4225", "fill-opacity": .5 }, s);
  [184, 254, 319, 389, 458].forEach(x => svgEl("rect", { x: x - 15, y: 280, width: 30, height: 96, fill: "#566236", "fill-opacity": .35 }, s));
  const frame = svgEl("g", {}, s);
  const fill = svgEl("rect", { x: 116, y: 130.5, width: 400, height: 101, rx: 17, fill: "#F7C211", "fill-opacity": .18, style: ADD }, s);
  const lines = svgEl("path", { d: "M168.5 121.2H463.5M168.5 241.2H463.5", stroke: "#FF0000", "stroke-width": 7.5, "stroke-opacity": .5, style: ADD }, s);
  const word = text(s, 315, 194, "LOCKED", { "font-family": F_M, "font-size": marine(1), "text-anchor": "middle", fill: "#FFFFB6" });
  const arrows = svgEl("path", { d: "M138.5 160.5L163.5 179.3L138.5 198ZM168.5 160.5L193.5 179.3L168.5 198ZM493.5 160.5L468.5 179.3L493.5 198ZM463.5 160.5L438.5 179.3L463.5 198Z", fill: "#FF0000", style: ADD }, s);
  const rockets = [158, 226, 295, 364, 432].map(x => { const r = svgEl("g", { transform: `translate(${x} 280.5)`, style: ADD }, s);
    svgEl("path", { d: "M26 0Q38 12 37 31H15Q14 12 26 0Z", fill: "#CD0000" }, r);
    svgEl("rect", { x: 13, y: 31, width: 26, height: 6, fill: "#FF3A3A" }, r); svgEl("rect", { x: 14, y: 37, width: 24, height: 33, fill: "#B00000" }, r);
    svgEl("path", { d: "M14 70L4 91H22L26 82L30 91H48L38 70Z", fill: "#8E0000" }, r); return r; });
  return { v: "cannon mcc", set(state, loaded) {
    const col = state === "lock" ? "#FF0000" : state === "acquire" ? "#F7C211" : "#ADADAD";
    frame.innerHTML = ""; corners(frame, 81, 102, 551, 260, col, state === "lock" ? .95 : .7);
    [[91, 146], [531, 146]].forEach(([x, y]) => svgEl("rect", { x, y, width: 10, height: 71, fill: "none", stroke: col, "stroke-width": 2.5, "stroke-opacity": .7, style: ADD }, frame));
    fill.setAttribute("opacity", state === "free" || state === "oor" || state === "friendly" ? 0 : 1); fill.setAttribute("fill", col);
    [lines, word, arrows].forEach(e => e.setAttribute("opacity", state === "lock" ? 1 : 0));
    rockets.forEach((r, i) => r.setAttribute("opacity", i < loaded ? 1 : .08)); }, arrows };
});
const heat = screen("<b>Gun heat</b> · spaceturret_guns: the bar grows and warms toward red", s => {
  svgEl("rect", { x: 35, y: 17, width: 600, height: 71, rx: 4, fill: "#574C23", "fill-opacity": .55 }, s);
  svgEl("rect", { x: 392, y: 94, width: 111, height: 163, rx: 18, fill: "none", stroke: "#8C8257", "stroke-opacity": .5, "stroke-width": 3 }, s);
  svgEl("rect", { x: 422, y: 103, width: 50, height: 144, rx: 10, fill: "#5A0000" }, s);
  svgEl("path", { d: "M429 150V202M466.5 150V202", stroke: "#FF2A2A", "stroke-width": 2.5 }, s);
  svgEl("rect", { x: 35, y: 264, width: 600, height: 120, rx: 4, fill: "#574C23", "fill-opacity": .45 }, s);
  svgEl("rect", { x: 200, y: 294, width: 265, height: 47, rx: 23, fill: "#D0A228", "fill-opacity": .8 }, s);
  [52, 70, 90, 110, 548, 568, 588, 605].forEach(x => svgEl("rect", { x, y: 358, width: 6, height: 13, fill: "#D0A228", "fill-opacity": .7 }, s));
  svgEl("path", { d: "M130 364.5H278M390 364.5H538", stroke: "#D0A228", "stroke-opacity": .7, "stroke-width": 6 }, s);
  const bar = svgEl("rect", { x: 50, y: 102, width: 40, height: 150, rx: 6, style: ADD }, s);
  const rivets = svgEl("g", { style: ADD }, s);
  return { v: "cannon mcc", set(h) { const w = 40 + 297 * h; bar.setAttribute("width", w.toFixed(1));
    const c = [.545 + .545 * h, .545 - .545 * h, .282 - .282 * h].map(v => Math.round(255 * Math.min(1, Math.max(0, v))));
    bar.setAttribute("fill", `rgb(${c.join(",")})`); rivets.innerHTML = "";
    for (let x = 58; x < 50 + w - 8; x += 26) svgEl("rect", { x, y: 106, width: 10, height: 10, fill: "#0A0A0A", "fill-opacity": .35 }, rivets); } };
});
const speedo = screen("<b>Speedometer</b> · speeder.gui on the bike: speed, the road scrolling through the bezel", s => {
  svgEl("rect", { x: 0, y: 0, width: 640, height: 480, fill: "#3E3D2F" }, s);
  const road = svgEl("g", {}, s);
  const clip = svgEl("clipPath", { id: uid("rd") }, s);
  svgEl("path", { d: "M285 237H354V357H285ZM272 265L260 288L203 365L178 411H250L238 375L238 329L248 300L277 262ZM367 265L379 288L436 365L461 411H389L401 375L401 329L391 300L362 262Z", fill: "#000" }, clip);
  const win = svgEl("g", { "clip-path": `url(#${clip.id})` }, road);
  svgEl("rect", { width: 640, height: 480, fill: "#0B0B07" }, win);
  const lanes = svgEl("g", { style: ADD }, win), chev = svgEl("path", { d: "M302 361L319.5 318L337 361L319.5 349Z", fill: "#A0A43B", style: ADD }, win);
  for (let k = -2; k < 8; k++) svgEl("path", { d: `M92.5 ${k * 100}H282.5M92.5 ${k * 100 + 37.5}H282.5M357.5 ${k * 100}H547.5M357.5 ${k * 100 + 37.5}H547.5`, stroke: "#BCBF43", "stroke-width": 7, "stroke-opacity": .8 }, lanes);
  [[10, 120], [519, 629]].forEach(([x0, x1]) => svgEl("rect", { x: x0, y: 30, width: x1 - x0, height: 430, rx: 12, fill: "#4A4936", stroke: "#7C7A5C", "stroke-width": 3 }, s));
  text(s, 126, 226, "smc", { "font-family": F_M, "font-size": marine(1.1), fill: "#C0C0C0" });
  const spd = text(s, 147, 177, "86", { "font-family": F_M, "font-size": marine(3.2), fill: "#DFDFDF", textLength: 242, lengthAdjust: "spacingAndGlyphs" });
  text(s, 419, 220, "kph", { "font-family": F_M, "font-size": marine(.85), fill: "#889946" });
  text(s, 513, 415, "config", { "font-family": F_M, "font-size": marine(.37), fill: "#FFFFFF" });
  text(s, 513, 437, "boost", { "font-family": F_M, "font-size": marine(.37), fill: "#FFFFFF" });
  return { v: "speeder", tick(now, kph) { const t = reduce ? 0 : now;
    lanes.setAttribute("transform", `translate(0 ${((t * .288) % 100).toFixed(1)})`);
    const p = (t % 1000) / 1000; chev.setAttribute("transform", `translate(0 ${(-120 * p).toFixed(1)})`); chev.setAttribute("opacity", reduce ? 1 : (1 - p).toFixed(2));
    const k = String(Math.round(kph)); spd.textContent = k; spd.setAttribute("textLength", k.length > 2 ? 353 : 242); } };
});

/* ---------------- The stage: the shared HUD layout (section 14.4), so each display follows the aspect, the HUD scale
   and Classic, and the transmission display always takes the top-trailing corner of the trailing screen ------------ */
const hud = A.hud;
const actl = document.getElementById("awkLayoutCtl"); if (actl) hud.controls(actl, { device: true });
const defs = svgEl("defs", {}, svg);
const scene = svgEl("g", {}, svg);
scene.id = uid("scn");
const blurF = svgEl("filter", { id: uid("bf"), x: "-5%", y: "-5%", width: "110%", height: "110%" }, defs); svgEl("feGaussianBlur", { stdDeviation: 5 }, blurF);
const frameMask = svgEl("mask", { id: uid("fm"), maskUnits: "userSpaceOnUse" }, defs);
const fmg = svgEl("radialGradient", { id: uid("fg"), gradientUnits: "userSpaceOnUse", cx: 320, cy: 240, r: 460 }, defs);
[[0, "#000"], [.42, "#000"], [.62, "#555"], [1, "#fff"]].forEach(([o, c]) => svgEl("stop", { offset: o, "stop-color": c }, fmg));
const fmRect = svgEl("rect", { fill: `url(#${fmg.id})` }, frameMask);
const blurLayer = svgEl("g", { mask: `url(#${frameMask.id})`, opacity: 0 }, svg);
svgEl("use", { href: "#" + scene.id, transform: "translate(320 240) scale(1.06) translate(-320 -240)", filter: `url(#${blurF.id})` }, blurLayer);
const layer = svgEl("g", {}, svg);
let shot = 0;
function stage(lay) {
  [frameMask, fmRect].forEach(e => { e.setAttribute("x", lay.x0); e.setAttribute("y", lay.y0); e.setAttribute("width", lay.W); e.setAttribute("height", lay.H); });
  fmg.setAttribute("r", (.54 * Math.hypot(lay.W, lay.H)).toFixed(1));
  const token = ++shot;
  later(() => { if (token !== shot) return; scene.innerHTML = ""; A.levelshot?.(scene, "sp", { x0: lay.x0, y0: lay.y0, W: lay.W, H: lay.H }); });
}
const ctl = document.getElementById("awkCtl"), cap = document.getElementById("awkCap");
const CAPS = {
  cannon: "The m03 dropship guns hide the gauges and strip and keep the radio, boss bar and notices. As on every HUD, Remastered keeps the transmission display in the top-trailing corner of the trailing screen, with the <b>ship-status panel</b> directly below it (the stock corner at 512,32 u touched it): an olive disc (opaque in Classic, 0.85 in Remastered) with the dropship in #475736. Its five sections turn #A13C2B at 80, 60, 40, 20 and 10% health and clear again as it is repaired, which the stock never did. Gold objective markers spin at 0.35 turn a second. Red darts on the rim point at attackers, pulsing at 2 Hz while they fade over 3 s. An additive cone shows where the gun looks relative to the ship's nose. The weapon state lives on the <b>reticule</b>, which stays at the crosshair size and changes color with the lock at once, and on the cockpit screens below. The turret HUD never said how to leave the gun; Remastered's HUD prompt adds Exit.",
  mcc: "The MCC's side gun keeps the space cannon's reticule and screens. It has <b>no turret health</b>, and rightly: the gun inherits the space cannon's noDamage, health 0 and god mode for its gunner, so nothing can damage it and a gun gauge would always read full. What the player defends is the MCC, so the readout is its hull: the stock gauge plate scaled to 136x70 u under U.S.S. Hannibal in Marine 0.25, the hull in Marine 0.50 (the MCC's health ÷ 10, 2000 when whole). Remastered stacks it under the transmission display, which the side gun's HUD lacks, fills the track in proportion (the stock track is static) and warns at 25%: the readout turns #FF3300 and its track pulses at 2 Hz, as the vehicle gauges do. Exit shows in the HUD prompt.",
  speeder: "The m07 race keeps the stock vehicle HUD: the <b>status bar</b>, centered on the bottom edge, where Remastered restores the hull and shield warnings the expansion removed and shows the machinegun's glyph for the machinegun (the bike lists it first, so the expansion showed the cannon's panel), and the radio in the trailing screen's corner. It adds a <b>progress bar</b> at the top center, 18 u lower in Remastered so the transmission display never covers its end at 4:3: a 45% black plate whose tab carries the gold race timer, and ten forward-leaning pips in two groups of five, lit one per checkpoint. Remastered pads the hundredths, turns the seconds #FF3333 over 100 ms in the last 10 s, and streaks the view's edges along the bike's path while boosting, the expansion's square-frame speed blur, never under reduced motion. The <b>HUD prompt</b> carries Boost while a boost is ready, the hazard symbol and Boosting while it lasts, and Exit, which the expansion lost when its boost line took the exit line's window. Classic keeps the boost line and Boosting... at the expansion's places.",
  chaingun: "The Awakening's mannable Strogg chaingun (m01's trench, m04's prison) cannot be damaged, but its gunner stands exposed, so its HUD (turret_hud.gui, Appendix C 1.39) is the gunner's: the on-foot Marine status bar with health and armor in their places, the armor gauge leaving at 0, and the <b>barrel heat</b> on the gauge where ammunition shows, track 0.20 and fill 0.35 in #FF9E1A, the heat as a percentage of the gun's limit where the clip shows, the gun's glyph where the weapon render sits and three rising heat waves in the notch. From the warning level (75%) and while the gun vents, track and fill turn #FF3300 at 0.35 and 0.60 and pulse at 2 Hz, and the figures turn #FF3300. Once the barrel is warm a 40x3 u bar sits 22 u under the crosshair, #FF9E1A at 0.85, #FF3300 from the warning level; while it vents, OVERHEATED in Marine #FF3300 pulses between 100% and 55% at 2 Hz. The transmission display, damage direction, notices and Exit in the HUD prompt complete it. Classic builds it on the stock vehicle bar: HEALTH and ARMOR with their values, HEAT (OVERHEATED while venting) on the weapon panel's top row and the heat in the charge bar.",
  gev: "The GEV's HUD names ten materials that neither game ships, so openQ4 draws them in the stock vehicle construction: plates at black 0.60 sheared 0,-.32; a vertical #FF9E1A health bar draining from the top (the stock divides an already normalized value by 1000 and reads empty); the hull from above, turning with the view relative to it under a fixed turret, in #B0C891; three shield stages as arcs, #D1AF47 from 80%, #C6C251 from 40% and #A2B85C to empty, the inner arcs pulsing #FF591A to #80591A every 500 ms once the shields fail; and the weapon glyph turned 60 degrees with an infinity mark or the rocket count over the #FF9E1A charge bar. Remastered centers the cluster on the bottom edge like every status bar (Classic keeps it bottom-leading), keeps the transmission display in the trailing screen's corner and adds Exit. No map places the GEV.",
};
let veh = "cannon", parts = {}, promptKey = "", st = { lock: "free", dmg: 0, marker: 0, guns: true, jam: false, kills: 12, heat: 0, loaded: 5, hull: 1, prog: 3, timeLeft: 42.37, boostAt: -1e9, spHull: .9, gevShield: 1, gevHp: .82, gevWeap: 0, cgHeat: .82, cgOver: false };
const button = (label, fn) => { const b = document.createElement("button"); b.className = "pl act"; b.innerHTML = `<i class="mk"></i><span class="lb">${label}</span>`; b.addEventListener("click", fn); ctl.appendChild(b); };
/* The HUD prompt (section 14.8): Exit on every ride, Boost on the speeder while a boost is ready. Classic shows only
   the stock lines (the walker-style exit line on the speeder, whose expansion HUD lost it, and its boost line). */
function drawPrompt(now) {
  if (!parts.prompt) return;
  const classic = hud.classic, items = [], since = now - st.boostAt;
  if (veh === "speeder") {
    if (since < 3000) { if (!classic) items.push({ warn: true, label: "Boosting" }); }
    else if (since > 6000) items.push({ kbm: "C", pad: "B", touch: "crouch", label: "Boost" });
  }
  if (!classic || veh === "speeder") items.push({ kbm: "SPACE", pad: "A", touch: "jump", label: "Exit" });
  const key = items.map(i => i.label).join("|") + hud.device + classic;
  if (key === promptKey) return;
  promptKey = key; parts.prompt.innerHTML = "";
  if (classic) items.forEach(it => A.hudPrompt(parts.prompt, { x: 629, y: it.label === "Exit" ? 475 : 455, items: [it], classic: true }));
  else if (items.length) A.hudPrompt(parts.prompt, { x: 629, y: 476, items });
}
function build() {
  const lay = hud.fit(svg), classic = hud.classic;
  stage(lay);
  layer.innerHTML = ""; ctl.innerHTML = ""; parts = {}; promptKey = ""; cap.innerHTML = CAPS[veh];
  hud.regionsLayer(layer, lay);
  screens?.querySelectorAll("figure").forEach(f => { f.hidden = !f.dataset.v.split(" ").includes(veh); });
  const tr = hud.place(layer, "tr", lay);
  /* the transmission display: the turret HUD's own corner (520,5) in Classic, which the MCC side gun's HUD lacks */
  if (!(classic && veh === "mcc")) A.radioChatter?.(svgEl("g", { transform: classic && veh === "cannon" ? "translate(4 -5)" : "" }, tr), {});
  parts.prompt = svgEl("g", {}, hud.place(layer, "br", lay));
  if (veh === "cannon" || veh === "mcc") {
    if (veh === "cannon") parts.panel = shipPanel(tr, defs, 512, 32); else parts.mcc = mccReadout(tr, defs, classic ? 0 : -36, !classic);
    parts.ret = reticule(layer, defs); parts.ret.set(st.lock);
    button("Cycle the lock", () => { const i = STATES.findIndex(s => s[0] === st.lock); st.lock = STATES[(i + 1) % STATES.length][0]; parts.ret.set(st.lock); paint(); });
    button("Fire rockets", () => { st.loaded = Math.max(0, st.loaded - 1); paint(); setTimeout(() => { st.loaded = Math.min(5, st.loaded + 1); paint(); }, 2500); });
    button("Fire the guns", () => { st.heat = Math.min(1, st.heat + .22); paint(); });
    if (veh === "cannon") {
      button("Hit the dropship", () => { st.dmg = Math.min(5, st.dmg + 1); const w = parts.panel.warns[Math.floor(Math.random() * 8)]; w.t0 = performance.now(); paint(); });
      button("Repair", () => { st.dmg = 0; paint(); });
      button("Next objective", () => { st.marker = (st.marker + 1) % 6; paint(); });
    } else button("Hit the MCC", () => { st.hull = st.hull <= .1 ? 1 : Math.max(.1, +(st.hull - .15).toFixed(2)); paint(); });
  } else if (veh === "speeder") {
    parts.sp = speederHud(hud.place(layer, "tc", lay, classic ? "" : " translate(0 18)"), defs, classic);
    parts.bar = A.vehicleBar?.(hud.place(layer, "bc", lay, classic ? "" : " translate(170.5 0)"), { weapon: "mgun", other: "cannon", hull: st.spHull, shield: .7, warnings: !classic });
    button("Pass a checkpoint", () => { st.prog = (st.prog + 1) % 11; paint(); });
    button("Boost", () => { st.boostAt = performance.now(); paint(); });
    button("Last 10 seconds", () => { st.timeLeft = 9.8; paint(); });
    button("Take a hit", () => { st.spHull = st.spHull > .25 ? .2 : .9; paint(); });
  } else if (veh === "chaingun") {
    chaingunHud(layer, lay, classic);
    button("Fire a burst", () => { if (st.cgOver) return; st.cgHeat = Math.min(1, st.cgHeat + .2); if (st.cgHeat >= 1) st.cgOver = true; paint(); });
    button("Hold the trigger", () => { if (st.cgOver) return; st.cgHeat = 1; st.cgOver = true; paint(); });
    button("Let it cool", () => { st.cgHeat = 0; st.cgOver = false; paint(); });
  } else {
    parts.gev = gevHud(classic ? hud.place(layer, "bl", lay) : hud.place(layer, "bc", lay, " translate(210.5 0)"), defs);
    button("Hit the shields", () => { st.gevShield = st.gevShield <= 0 ? 1 : Math.max(0, +(st.gevShield - .25).toFixed(2)); paint(); });
    button("Switch weapon", () => { st.gevWeap = (st.gevWeap + 1) % 3; paint(); });
  }
  paint(); drawPrompt(performance.now());
}
/* ---------------- The chaingun turret (section 14.12, Appendix C 1.39; remastered_hud_awakening.py heat_gauge,
   heat_reticle): the gunner's on-foot Marine status bar with the barrel heat on the ammunition gauge, and the heat under
   the crosshair. Classic builds it on the stock vehicle bar: HEALTH and ARMOR with values, HEAT on the panel's top row
   (OVERHEATED while the gun vents) and the heat in the charge bar. ---------------- */
const HEAT = "#FF9E1A", HEAT_HOT = "#FF3300";
function chaingunHud(layer, lay, classic) {
  A.crosshairAt?.(layer);
  const sb = hud.place(layer, "bc", lay);
  if (classic) {
    parts.cbar = A.vehicleBar?.(sb, { weapon: "mgun", other: "mgun", hull: .8, shield: .5, charge: 0, labels: ["HEALTH", "ARMOR"], values: [80, 50], otherText: "HEAT" });
  } else {
    const heatG = A.marineGauge(sb, 101, "ammo", { color: HEAT });
    heatG.icon.innerHTML = ""; heatG.num.setAttribute("x", 118);
    if (A.ICONS?.veh_mgun) A.icon(heatG.root, "veh_mgun", { x: 9, y: 17, h: 22, color: "#C7BD78", opacity: .4 });
    const waves = svgEl("g", {}, heatG.root);
    [101.5, 106, 110.5].forEach(x0 => svgEl("path", { d: "M" + Array.from({ length: 13 }, (_, k) => `${(x0 + 1.4 * Math.sin(Math.PI * 3 * k / 12)).toFixed(2)} ${(53 - 14 * k / 12).toFixed(2)}`).join("L"),
      fill: "none", "stroke-width": 1.9, "stroke-linecap": "round", "stroke-linejoin": "round" }, waves));
    A.marineGauge(sb, 278, "health").set(80, .8); A.marineGauge(sb, 414, "armor").set(50, .5);
    parts.cg = { heatG, waves };
  }
  // under the crosshair once the barrel is warm: a 40x3 u bar, and OVERHEATED while the gun vents
  const xh = svgEl("g", {}, hud.place(layer, "cc", lay));
  svgEl("rect", { x: 300, y: 262, width: 40, height: 3, fill: "#000", "fill-opacity": .45 }, xh);
  const bar = svgEl("rect", { x: 300, y: 262, width: 0, height: 3 }, xh);
  const over = svgEl("g", {}, xh);
  [[1, "#000"], [0, HEAT_HOT]].forEach(([d, c]) => svgEl("text", { x: 320 + d, y: 281 + d, "text-anchor": "middle", "font-family": F_M, "font-size": 7, fill: c, "fill-opacity": d ? .8 : 1 }, over).textContent = "OVERHEATED");
  if (!reduce) svgEl("animate", { attributeName: "opacity", values: "1;.55;1", dur: ".5s", repeatCount: "indefinite" }, over);
  parts.xh = { g: xh, bar, over };
}
function paintChaingun() {
  const h = st.cgHeat, hot = h >= .75 || st.cgOver;
  if (parts.cg) { const { heatG, waves } = parts.cg;
    heatG.set(Math.round(h * 100), h);
    heatG.trackFill.setAttribute("fill", hot ? HEAT_HOT : HEAT); heatG.trackFill.setAttribute("fill-opacity", hot ? .35 : .2);
    heatG.fill.setAttribute("fill", hot ? HEAT_HOT : HEAT); heatG.fill.setAttribute("fill-opacity", hot ? .6 : .35);
    heatG.bevel.setAttribute("stroke", hot ? HEAT_HOT : HEAT);
    [heatG.trackFill, heatG.fill].forEach(e => e.classList.toggle("rim-on", hot && !reduce));
    heatG.num.setAttribute("fill", hot ? HEAT_HOT : "#DDEBC3");
    waves.setAttribute("stroke", hot ? HEAT_HOT : "#A8A360"); waves.setAttribute("stroke-opacity", hot ? .8 : .6); }
  if (parts.cbar) { parts.cbar.charge?.setAttribute("width", (74 * h).toFixed(1)); if (parts.cbar.otherText) {
    parts.cbar.otherText.textContent = st.cgOver ? "OVERHEATED" : "HEAT"; parts.cbar.otherText.setAttribute("fill", st.cgOver ? HEAT_HOT : "#A8A360"); } }
  if (parts.xh) { parts.xh.g.setAttribute("opacity", h > .02 || st.cgOver ? 1 : 0); parts.xh.bar.setAttribute("width", (40 * h).toFixed(2));
    parts.xh.bar.setAttribute("fill", hot ? HEAT_HOT : HEAT); parts.xh.bar.setAttribute("fill-opacity", hot ? .9 : .85);
    parts.xh.over.setAttribute("display", st.cgOver ? "inline" : "none"); }
}
function paint() {
  if (veh === "chaingun") paintChaingun();
  if (parts.panel) { parts.panel.dmg.forEach((d, i) => d.setAttribute("opacity", i < st.dmg ? 1 : 0));
    parts.panel.marks.forEach((m, i) => m.m.setAttribute("opacity", i === st.marker - 1 ? 1 : 0)); }
  if (parts.mcc) parts.mcc.set(st.hull);
  sel.set?.(st.lock !== "acquire" && st.lock !== "lock", st.jam, st.kills);
  rack.set?.(st.lock, st.loaded);
  heat.set?.(st.heat);
  if (parts.sp) { parts.sp.pips.forEach((p, i) => p.setAttribute("opacity", i < st.prog ? 1 : 0)); }
  if (parts.bar) parts.bar.set(st.spHull, .7);
  if (parts.gev) { const g = parts.gev, s = st.gevShield;
    g.hp.setAttribute("y", (419 + (1 - st.gevHp) * 46).toFixed(1)); g.hp.setAttribute("height", (46 * st.gevHp).toFixed(1));
    const al = [Math.max(0, (s - .8) / .2), Math.max(0, Math.min(1, (s - .4) / .4)), Math.max(0, Math.min(1, s / .4))];
    g.stages.forEach((e, i) => e.setAttribute("opacity", al[i].toFixed(2))); g.out.setAttribute("opacity", s <= 0 ? 1 : 0);
    g.weap.innerHTML = ""; const key = ["veh_shell", "veh_mgun", "veh_rocket"][st.gevWeap];
    const gl = svgEl("g", { transform: "translate(171 433) rotate(60)" }, g.weap);
    if (A.ICONS[key]) A.icon(gl, key, { x: 0, y: -11, h: 22, anchor: "middle", color: "#B0C891" });
    if (st.gevWeap === 2) text(g.weap, 140, 457, "12", { "font-family": F_M, "font-size": marine(.7), "text-anchor": "middle", fill: "#B0C891", "fill-opacity": .8, transform: "matrix(1 0 -.32 1 146 0)" });
    else text(g.weap, 142, 452, "∞", { "font-family": F_L, "font-size": 24, "text-anchor": "middle", fill: "#B0C891", "fill-opacity": .8 }); }
}
function tick(now) {
  if (parts.ret) parts.ret.tick(now);
  if (parts.panel) {
    const yaw = reduce ? 0 : 38 * Math.sin(now / 7000 * 2 * Math.PI);
    parts.panel.cone.setAttribute("transform", `rotate(${yaw.toFixed(2)} 64 64)`);
    parts.panel.marks.forEach(m => m.r.setAttribute("transform", `rotate(${reduce ? 0 : (now * .126) % 360})`));
    parts.panel.warns.forEach(w => { const e = now - w.t0;
      if (e > 3000) { w.w.setAttribute("opacity", 0); return; }
      const pulse = reduce ? 1 : Math.min(1, Math.max(0, Math.sin(now / 1000 * 2 * Math.PI * 2) + .5));
      w.w.setAttribute("opacity", ((1 - e / 3000) * pulse).toFixed(3)); });
  }
  if (!reduce && st.heat > 0 && veh !== "speeder" && veh !== "gev") { st.heat = Math.max(0, st.heat - .0011); heat.set(st.heat); }
  if (parts.sp) {
    if (!reduce) { st.timeLeft -= 1 / 60; if (st.timeLeft < 0) st.timeLeft = 60; }
    const s = Math.floor(st.timeLeft), h = Math.floor((st.timeLeft - s) * 100);
    parts.sp.sec.textContent = s; parts.sp.hun.textContent = String(h).padStart(2, "0");
    parts.sp.sec.setAttribute("fill", st.timeLeft < 10 ? "#FF3333" : "#E1CC67");
    const boosting = now - st.boostAt < 3000;
    parts.sp.boost?.setAttribute("opacity", boosting ? 1 : 0);
    blurLayer.setAttribute("opacity", boosting && !reduce && !hud.classic ? Math.min(1, (now - st.boostAt) / 500, (3000 - (now - st.boostAt)) / 1500).toFixed(2) : 0);
    speedo.tick(now, boosting ? 142 : 86);
    drawPrompt(now);
  } else blurLayer.setAttribute("opacity", 0);
  if (veh === "chaingun") {                     // the barrel cools; a vent runs until it is cold
    if (!reduce) { st.cgHeat = Math.max(0, st.cgHeat - (st.cgOver ? .006 : .0025)); if (st.cgHeat <= 0) st.cgOver = false; }
    paintChaingun();
  }
  if (parts.gev) { const pulse = Math.floor(now / 500) % 2; parts.gev.out.setAttribute("stroke", pulse || reduce ? "#FF591A" : "#80591A");
    parts.gev.hull.setAttribute("transform", `rotate(${reduce ? 0 : (24 * Math.sin(now / 5000 * 2 * Math.PI)).toFixed(2)})`);
    parts.gev.charge.setAttribute("width", st.gevWeap === 0 ? (53 * ((now % 2400) / 2400)).toFixed(1) : 53); }
  if (veh === "cannon" || veh === "mcc") { const on = Math.floor(now / 125) % 2 === 0 || reduce; rack.arrows.setAttribute("fill-opacity", on ? 1 : .3); }
}
document.getElementById("awkVeh")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; veh = b.dataset.v;
  document.querySelectorAll("#awkVeh .seg").forEach(x => x.setAttribute("aria-pressed", x === b)); build(); });
build(); hud.on(build); document.fonts?.ready.then(build);
A.tick ? A.tick(svg, tick) : tick(0);
if (reduce) tick(4000);
})();
