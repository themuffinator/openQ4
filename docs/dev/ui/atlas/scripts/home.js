
(() => {
const A = window.__atlas, { svgEl } = A;
const svg = document.getElementById("homeSvg");
if (!svg) return;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_S = "Oxanium, Chakra Petch, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const ST = { read: "#FCFFC8", sel: "#FFCC00", line: "#F59512", fill: "#FF9000", bar: "#FF8000" };
let uid = 0; const id = p => `hm${p}${++uid}`;
let state = "menu";
const status = document.getElementById("homeStatus");

/* The Quake emblem (q4logo_alpha, 512x512, traced) in its stock 260x260 u window at 380,125. */
const EMBLEM = "M91.28 3.55 84.55 7.11 77.31 11.81 70.46 17.27 66.65 20.82 61.45 26.41 57.13 31.87 52.81 38.34 49.26 44.69 46.34 51.29 44.18 57.26 42.66 62.59 41.13 69.95 40.12 79.60 40.12 89.50 41.13 99.02 42.15 104.23 44.18 111.72 47.23 119.84 49.51 124.54 47.23 130.51 47.10 131.78 53.45 141.43 60.68 149.93 68.17 149.55 72.24 153.23 76.81 156.79 77.57 163.64 78.20 164.40 85.06 168.47 95.09 173.04 97.88 174.05 98.64 174.05 104.10 170.50 104.74 170.37 110.96 172.02 120.48 173.42 120.86 173.80 120.86 212.77 121.11 213.28 121.37 217.98 122.38 225.98 124.92 239.31 129.11 256.57 129.75 257.33 130.25 256.57 134.95 236.89 136.98 225.98 138.00 217.98 138.25 213.28 138.51 212.77 138.51 173.80 138.89 173.42 146.50 172.40 154.88 170.37 160.72 174.05 161.48 174.05 171.26 169.99 177.23 166.82 181.67 163.90 182.56 156.79 191.19 149.55 198.68 149.93 205.54 141.93 212.27 131.78 212.27 130.89 209.85 124.54 212.14 119.84 215.19 111.72 217.22 104.23 218.23 99.02 219.25 89.50 219.25 79.60 218.23 69.95 215.69 58.91 212.14 49.13 208.08 40.88 204.90 35.67 200.71 29.83 193.86 21.96 188.91 17.27 184.59 13.71 174.81 7.11 167.71 3.55 167.83 3.94 168.34 3.94 168.72 4.44 173.93 7.49 180.78 12.70 186.24 17.77 191.45 23.61 196.52 30.72 200.08 36.82 203.12 43.54 205.66 50.91 207.57 59.03 208.58 67.54 208.71 75.54 208.20 82.14 207.06 89.12 205.54 95.09 203.51 101.05 199.57 109.69 194.49 117.94 188.27 125.68 181.80 132.03 174.81 137.49 168.21 141.68 158.44 146.25 148.92 149.30 138.89 151.07 138.51 150.69 138.51 128.35 137.87 124.54 136.47 118.96 134.44 112.86 130.38 103.09 129.62 102.32 128.98 103.09 124.92 112.86 122.89 118.96 121.49 124.54 120.86 128.35 120.86 150.69 120.35 151.07 114.51 150.19 106.39 148.15 99.40 145.62 91.15 141.68 82.77 136.22 76.43 131.02 71.22 125.81 67.54 121.49 64.87 117.94 60.94 111.72 58.78 107.78 55.86 101.05 52.69 90.90 50.91 79.60 50.65 70.08 51.67 59.79 53.83 50.53 57.26 41.13 61.95 32.12 67.41 24.25 74.27 16.63 82.39 9.65 87.60 6.09 91.79 3.68Z";
A.EMBLEM = EMBLEM;

/* Measured band silhouettes (Appendix B.2) in their home positions, and the Strogg family's version: the same frame
   with 30-degree shoulders, downward teeth and a stepped riser in place of the 45-degree notches. */
const MARINE_TOP = [[-400, 89.7], [-297.9, 89.7], [-283.7, 103.8], [30.7, 103.8], [40.9, 94.7], [76.6, 94.7], [85.8, 103.8], [127.6, 103.8], [137.8, 94.7],
  [172.5, 94.7], [181.7, 103.8], [223.5, 103.8], [233.7, 94.7], [268.4, 94.7], [277.6, 103.8], [315.4, 103.8], [352.1, 68.5], [900, 68.5]];
const MARINE_BTM = [[-399, 421.5], [-232.7, 421.5], [-222.5, 430.6], [-28.6, 430.6], [2.1, 400.4], [344.9, 400.4], [389.8, 443.7], [582.7, 443.7], [591.9, 434.6], [900, 434.6]];
const STROGG_TOP = [[-400, 92], [-300, 92], [-283, 102], [24, 102], [41, 112], [86, 112], [103, 102], [150, 102], [167, 112], [212, 112], [229, 102], [308, 102],
  [327, 91], [334, 91], [352, 66], [520, 66], [537, 76], [578, 76], [595, 66], [900, 66]];
const STROGG_BTM = [[-400, 424], [-240, 424], [-223, 434], [-26, 434], [-8, 424], [0, 400], [334, 400], [351, 410], [372, 410], [394, 444], [580, 444], [597, 434], [900, 434]];
const poly = pts => "M" + pts.map(p => p.join(" ")).join("L");

/* Navigation: dark plates bleeding off the leading edge on a 30 u pitch, marker at 32 u, label at 44 u (section 9). */
const NAV = {
  menu: { y0: 202, items: [["CONTINUE", "Air Defense Bunker · Corporal · 1:42 played · saved 3 min ago"], ["SINGLE PLAYER", "Start the campaign or play the Arena."],
    ["LOAD GAME", "Three saves; the newest is Air Defense Bunker."], ["MULTIPLAYER", "Find a server, host a match or play with bots."], ["SETTINGS", "Controls, display, audio and game options."]] },
  pause: { y0: 172, items: [["RESUME"], ["SAVE GAME"], ["LOAD GAME"], ["RESTART LEVEL"], ["OBJECTIVES"], ["SETTINGS"], ["QUIT TO MENU"]] },
};
NAV.strogg = NAV.pause;
const LEVEL = {
  pause: { head: "CURRENT MISSION", name: "AIR DEFENSE TRENCHES", line: "Mission 2 · Corporal", scene: "sp",
    objectives: [["Fight through the trench network", 1], ["Destroy the anti-aircraft battery", 0], ["Regroup with Rhino squad at the bunker", 0]],
    stats: "0:42:10 in mission · 3:12:05 total · autosave 4 min ago" },
  strogg: { head: "CURRENT MISSION", name: "STROGG MEDICAL FACILITIES", line: "Stroggos · Corporal", scene: "strogg",
    objectives: [["Escape the medical facility", 0], ["Find a way back to Rhino squad", 0]],
    stats: "0:18:40 in mission · 9:44:12 total · autosave 2 min ago" },
};

/* One frame loop for the effects: [start, duration, fn(p)]. */
let jobs = [], looping = false;
const run = (delay, dur, fn) => { jobs.push([performance.now() + delay, dur, fn]); if (!looping) { looping = true; requestAnimationFrame(tick); } };
function tick(now) {
  jobs = jobs.filter(([t0, dur, fn]) => { if (now < t0) return true; fn(dur ? Math.min(1, (now - t0) / dur) : 1); return now < t0 + dur; });
  if (jobs.length) requestAnimationFrame(tick); else looping = false;
}
const mix = (a, b, p) => { const h = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16)); const x = h(a), y = h(b); return `rgb(${x.map((v, i) => Math.round(v + (y[i] - v) * p)).join(",")})`; };

/* Strogg text: the same words in runes and in R_Strogg on one baseline, the runes 1.2x the cap height; the credits'
   translation (section 8) cross-fades them, pops both 6% and settles, and a heading gets the scan bar. */
const skew = (x, y) => `translate(${x} ${y}) skewX(11.4) translate(${-x} ${-y})`;
function slabel(parent, text, x, y, size, { color = ST.read, weight = 600, anchor = "start", spacing = .6, cls } = {}) {
  const g = svgEl("g", {}, parent);
  const rune = A.runes ? A.runes(g, text.toUpperCase(), x, y, size * .7 * 1.2, { anchor, fill: ST.sel, opacity: 0 }) : svgEl("g", {}, g);
  const latin = svgEl("text", { x, y, "text-anchor": anchor, "font-family": F_S, "font-weight": weight, "font-size": size, "letter-spacing": spacing, fill: color, transform: skew(x, y) }, g);
  if (cls) latin.setAttribute("class", cls);
  latin.textContent = text;
  return { g, rune, latin, x, y, size, color };
}
function translate(L, delay, dur, { white = false, bar = null } = {}) {
  if (reduce || !A.runes) { L.rune.setAttribute("opacity", 0); L.latin.setAttribute("opacity", 1); return; }
  L.rune.setAttribute("opacity", .8); L.latin.setAttribute("opacity", 0);
  const cx = L.x, cy = L.y - L.size * .35, pop = .06;
  run(delay, dur, p => {
    L.rune.setAttribute("opacity", .8 * (1 - p)); L.rune.setAttribute("transform", `translate(${cx} ${cy}) scale(${1 + pop * (1 - p)}) translate(${-cx} ${-cy})`);
    L.latin.setAttribute("opacity", p); L.latin.setAttribute("transform", `translate(${cx} ${cy}) scale(${1 + pop * .9 * (1 - p)}) translate(${-cx} ${-cy}) ${skew(L.x, L.y)}`);
    if (white) L.latin.setAttribute("fill", "#FFFFFF");
  });
  if (white) run(delay + dur + 60, 150, p => L.latin.setAttribute("fill", mix("#FFFFFF", L.color, p)));
  if (bar && A.creditsBar) scanBar(bar, delay, dur * .3);
}
function scanBar({ parent, len, h, y, from, to }, delay, slide) {
  const g = A.creditsBar(parent, len, h, ST.bar); g.setAttribute("opacity", 0);
  const dim = slide * 3.33;
  run(delay, dim, p => { const q = A.accelDecel ? A.accelDecel(p * dim, slide * .4, slide * .6) : Math.min(1, p * 3.33);
    g.setAttribute("transform", `translate(${from + (to - from) * q} ${y})`); g.setAttribute("opacity", 1 - p); });
  run(delay + dim + 20, 0, () => g.remove());
}

/* ---------------------------------------------------------------------------------------------------------- */
function build(opening = true) {
  svg.innerHTML = "";
  const strogg = state === "strogg", pause = state !== "menu";
  svg.classList.toggle("strogg", strogg);
  svg.setAttribute("aria-label", pause ? (strogg ? "Strogg single-player pause menu" : "Single-player pause menu") : "Main menu in its home state");
  const defs = svgEl("defs", {}, svg);
  const grad = (stops, v = true, extra = {}) => { const gid = id("g"), g = svgEl("linearGradient", { id: gid, x1: 0, y1: 0, x2: v ? 0 : 1, y2: v ? 1 : 0, ...extra }, defs);
    stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g)); return `url(#${gid})`; };
  const blur = sd => { const fid = id("f"); svgEl("feGaussianBlur", { stdDeviation: sd }, svgEl("filter", { id: fid, x: "-20%", y: "-40%", width: "140%", height: "180%" }, defs)); return `url(#${fid})`; };
  const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter";

  // backdrop: the lit field on the title screen; in a game, the paused view softened behind the menu
  if (!pause) {
    const sid = id("s"), rg = svgEl("radialGradient", { id: sid, cx: "68%", cy: "42%", r: "75%" }, defs);
    [[0, "#232a18"], [.55, "#0e110b"], [1, "#040503"]].forEach(([o, c]) => svgEl("stop", { offset: o, "stop-color": c }, rg));
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: `url(#${sid})` }, svg);
  } else {
    const fid = id("soft"), f = svgEl("filter", { id: fid, x: "-2%", y: "-2%", width: "104%", height: "104%", "color-interpolation-filters": "sRGB" }, defs);
    svgEl("feGaussianBlur", { stdDeviation: 5 }, f); svgEl("feColorMatrix", { type: "saturate", values: .7 }, f);
    const scene = svgEl("g", { filter: `url(#${fid})` }, svg);
    A.levelshot?.(scene, LEVEL[state].scene, { x0: -107, y0: 0, W: 854, H: 480 });
    svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: "#000", "fill-opacity": .45 }, svg);
    svgEl("rect", { x: -107, y: 0, width: 520, height: 480, fill: grad([[0, "#000", .7], [.55, "#000", .35], [1, "#000", 0]], false) }, svg);
  }
  svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: grad(strogg ? [[.1, "#6b2a0e", 0], [.5, "#6b2a0e", .55], [.9, "#6b2a0e", 0]] : [[.1, "#616F26", 0], [.5, "#616F26", pause ? .45 : .7], [.9, "#616F26", 0]]), style: ADD }, svg);
  svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: grad([[0, "#000", .97], [.3, "#000", 0], [.7, "#000", 0], [1, "#000", .97]]) }, svg);
  const pid = id("p"), pat = svgEl("pattern", { id: pid, width: 30, height: 30, patternUnits: "userSpaceOnUse" }, defs);
  if (strogg) svgEl("path", { d: "M15 9.5v11M9.5 15h11", stroke: "#FFB060", "stroke-width": .6 }, pat); else svgEl("path", { d: "M15 8.5v13M8.5 15h13", stroke: "#fff", "stroke-width": .6 }, pat);
  svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: `url(#${pid})`, opacity: .04 }, svg);

  if (!pause) drawEmblem(defs, blur, ADD);
  if (pause) drawLevelBlock(defs, grad, blur, ADD, opening);

  // framing bands with their rim light
  const top = strogg ? STROGG_TOP : MARINE_TOP, btm = strogg ? STROGG_BTM : MARINE_BTM, rim = strogg ? ST.line : "#626934";
  [top, btm].forEach(pts => svgEl("path", { d: poly(pts), fill: "none", stroke: rim, "stroke-width": strogg ? 5 : 7, opacity: strogg ? .5 : .7, filter: blur(4) }, svg));
  svgEl("path", { d: poly(top) + "L900 0L-400 0Z", fill: "#000" }, svg);
  svgEl("path", { d: poly(btm) + "L900 480L-400 480Z", fill: "#000" }, svg);
  if (strogg) {                                              // circuit traces etched in the bands, as on the Strogg wheel
    const c = svgEl("g", { stroke: ST.line, "stroke-width": .8, fill: "none", opacity: .32 }, svg);
    ["M-300 40H40L60 58H250", "M380 24H520L538 40H700", "M-200 460H120L140 448H300", "M420 470H560L578 458H760"].forEach(d => svgEl("path", { d }, c));
    [[40, 40], [250, 58], [520, 24], [700, 40], [120, 460], [300, 448], [560, 470]].forEach(([x, y]) => svgEl("circle", { cx: x, cy: y, r: 1.6, fill: ST.line }, c));
  }

  // wordmark or screen title
  if (!pause) {
    const hid = id("h"), hf = svgEl("filter", { id: hid }, defs);
    svgEl("feGaussianBlur", { stdDeviation: 1.1, result: "b" }, hf); const mg = svgEl("feMerge", {}, hf); svgEl("feMergeNode", { in: "b" }, mg); svgEl("feMergeNode", { in: "SourceGraphic" }, mg);
    svgEl("text", { x: 8, y: 176, "font-family": F_M, "font-size": 46, fill: "#A2C550", opacity: .85, filter: `url(#${hid})`, style: ADD, "letter-spacing": 1 }, svg).textContent = "openQ4";
  } else if (strogg) {
    const L = slabel(svg, "GAME PAUSED", 44, 152, 13, { color: ST.read, spacing: 1.2 });
    if (opening) translate(L, 60, 500, { white: true, bar: { parent: svg, len: 150, h: 16, y: 147, from: 30, to: 44 + 125 } }); else translate(L, 0, 0);
  } else svgEl("text", { x: 44, y: 152, "font-family": F_M, "font-size": 12, fill: "#fff", "fill-opacity": .5 }, svg).textContent = "GAME PAUSED";

  drawNav(defs, grad, opening);
  if (!pause) drawDetail(0);

  // plinth with the secondary links; in a game only EXIT stands there
  svgEl("path", { d: strogg ? "M36 405H318L330 419V426H44L36 419Z" : "M36 405H318L334 421V426H44L36 418Z", fill: strogg ? "#4a2410" : "#3E4A21", "fill-opacity": .3 }, svg);
  (pause ? [["EXIT", 58]] : [["MODS", 58], ["UPDATES", 114], ["CREDITS", 188], ["EXIT", 262]]).forEach(([t, x]) => {
    const g = svgEl("g", { class: "sl", tabindex: 0, role: "button" }, svg);
    svgEl("path", { d: strogg ? `M${x - 8} ${410.5}h4.5l-1.2 4.5h-4.5z` : `M${x - 8} 410.5h4.5v4.5z`, class: "mkr" }, g);
    svgEl("text", { x, y: 416.5, ...(strogg ? { transform: skew(x, 416.5) } : {}) }, g).textContent = t;
  });
  // prompt bar in the bottom band's raised trailing section
  const pr = svgEl("g", { "font-family": F_M, "font-size": 7.4, fill: "#fff", "fill-opacity": .8 }, svg);
  [["A", "SELECT", 617], ["B", pause ? "RESUME" : "QUIT", 681]].forEach(([k, t, x]) => {
    svgEl("circle", { cx: x, cy: 455, r: 6, fill: strogg ? ST.sel : "#fff", "fill-opacity": .85 }, pr);
    svgEl("text", { x: x - 2.6, y: 458, "font-family": F_L, "font-weight": 700, "font-size": 8, fill: "#0b0d09", "fill-opacity": 1 }, pr).textContent = k;
    svgEl("text", { x: x + 10, y: 458 }, pr).textContent = t;
  });
}

/* The emblem watermark: the stock darkening blend (the backdrop multiplied by the inverse of the emblem's color, so it
   reads as a shadow cast in the light band), then a faint rim light along its outline and a glint that runs around it
   every 9 s. Reduced motion keeps the rim and drops the glint. */
function drawEmblem(defs, blur, ADD) {
  const g = svgEl("g", { transform: "translate(380 125)" }, svg);
  svgEl("path", { d: EMBLEM, fill: "rgb(168,166,57)", style: "mix-blend-mode:multiply" }, g);
  svgEl("path", { d: EMBLEM, fill: "none", stroke: "#CCCC51", "stroke-width": .9, "stroke-opacity": .2, style: ADD }, g);
  if (reduce) return;
  const probe = svgEl("path", { d: EMBLEM }, g), L = probe.getTotalLength(); probe.remove();
  const seg = L * .12, dash = `${seg} ${L - seg}`;
  const glow = svgEl("path", { d: EMBLEM, fill: "none", stroke: "#DDEB8A", "stroke-width": 3, "stroke-opacity": .45, "stroke-dasharray": dash, "stroke-linecap": "round", filter: blur(1.6), style: ADD }, g);
  const core = svgEl("path", { d: EMBLEM, fill: "none", stroke: "#F4FAD0", "stroke-width": 1, "stroke-opacity": .75, "stroke-dasharray": dash, "stroke-linecap": "round", style: ADD }, g);
  [glow, core].forEach(p => svgEl("animate", { attributeName: "stroke-dashoffset", from: 0, to: -L, dur: "9s", repeatCount: "indefinite" }, p));
}

/* The current level block of the pause menus, in the space the title screen gives the emblem. */
function drawLevelBlock(defs, grad, blur, ADD, opening) {
  const D = LEVEL[state], strogg = state === "strogg", x = 374, y = 118, w = 262, h = 270;
  const g = svgEl("g", { transform: `translate(${x} ${y})` }, svg);
  const shape = strogg ? `M10 0H${w - 10}L${w} 10V${h - 10}L${w - 10} ${h}H10L0 ${h - 10}V10Z` : `M2 0H${w - 8}L${w} 8V${h - 2}L${w - 2} ${h}H8L0 ${h - 8}V2Z`;
  svgEl("path", { d: shape, fill: strogg ? "#0a0503" : "#000", "fill-opacity": .92 }, g);
  if (strogg) {                                             // masked grain and a warm edge light, as on the Strogg wheel
    const nid = id("n"), pat = svgEl("pattern", { id: nid, width: 120, height: 120, patternUnits: "userSpaceOnUse" }, defs);
    svgEl("image", { width: 120, height: 120, href: "data:image/svg+xml," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='120' height='120'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='1.1' numOctaves='2' stitchTiles='stitch'/><feColorMatrix values='0 0 0 0 1 0 0 0 0 .75 0 0 0 0 .4 0 0 0 1.4 -.55'/></filter><rect width='120' height='120' filter='url(#n)'/></svg>") }, pat);
    const cid = id("c"); svgEl("path", { d: shape }, svgEl("clipPath", { id: cid }, defs));
    svgEl("rect", { x: 0, y: 0, width: w, height: h, fill: `url(#${nid})`, opacity: .12, "clip-path": `url(#${cid})` }, g);
  }
  svgEl("path", { d: shape, fill: "none", stroke: strogg ? ST.line : "#A0A040", "stroke-width": 1, "stroke-opacity": strogg ? .75 : 1 }, g);
  // header
  svgEl("path", { d: strogg ? `M10.5 .5H${w - 10.5}L${w - .5} 10.5V26.7H.5V10.5Z` : `M2.5 .5H${w - 8.2}L${w - .5} 7.8V26.7H.5V2.5Z`, fill: grad([[0, strogg ? "#FF9000" : "#fff", strogg ? .16 : .08], [.5, strogg ? "#FF9000" : "#fff", strogg ? .1 : .08], [1, "#fff", 0]], false) }, g);
  svgEl("path", { d: `M0 26.7H${w}`, stroke: strogg ? ST.line : "#A0A040", "stroke-opacity": .5, "stroke-width": .67 }, g);
  if (strogg) svgEl("path", { d: "M12 9.8h6l-1.6 6.4h-6z", fill: ST.line }, g); else svgEl("path", { d: "M10 9.8l5.3 3.5-5.3 3.5z", fill: "#909A49" }, g);
  const texts = [];
  if (strogg) texts.push([slabel(g, D.head, 24, 17.2, 8.2, { color: ST.read, spacing: .8 }), 0, 300, true]);
  else svgEl("text", { x: 20, y: 17.2, "font-family": F_M, "font-size": 7.4, fill: "#fff", "fill-opacity": .9 }, g).textContent = D.head;
  // levelshot in a 1 u picture frame (#414624; Strogg: chamfered, orange)
  const px = 10, py = 34, pw = w - 20, ph = 118, cid = id("pic");
  svgEl("path", { d: strogg ? `M${px + 6} ${py}H${px + pw}V${py + ph - 6}L${px + pw - 6} ${py + ph}H${px}V${py + 6}Z` : `M${px} ${py}h${pw}v${ph}h${-pw}Z` }, svgEl("clipPath", { id: cid }, defs));
  const pic = svgEl("g", { "clip-path": `url(#${cid})` }, g);
  const sc = svgEl("g", { transform: `translate(${px} ${py}) scale(${pw / 854 * 1.12}) translate(${107 - 854 * .05} -30)` }, pic);
  A.levelshot?.(sc, D.scene, { x0: -107, y0: 0, W: 854, H: 480 });
  svgEl("path", { d: strogg ? `M${px + 6} ${py}H${px + pw}V${py + ph - 6}L${px + pw - 6} ${py + ph}H${px}V${py + 6}Z` : `M${px} ${py}h${pw}v${ph}h${-pw}Z`, fill: "none", stroke: strogg ? ST.line : "#414624", "stroke-opacity": strogg ? .6 : 1 }, g);
  // name, line, objectives, stats
  if (strogg) {
    texts.push([slabel(g, D.name, 10, 168, 10.5, { color: ST.read, spacing: .5 }), 80, 400, true]);
    texts.push([slabel(g, D.line, 10, 181, 8.4, { color: ST.sel, weight: 500, spacing: .3 }), 140, 200, false]);
    texts.push([slabel(g, ">>OBJECTIVES", 10, 199, 7.2, { color: ST.sel, spacing: .8 }), 180, 200, false]);
  } else {
    svgEl("text", { x: 10, y: 168, "font-family": F_M, "font-size": 9.4, "letter-spacing": -.2, fill: "#fff", "fill-opacity": .9 }, g).textContent = D.name;
    const l = svgEl("text", { x: 10, y: 181, "font-family": F_L, "font-weight": 500, "font-size": 8.4, fill: "#fff", "fill-opacity": .6 }, g); l.textContent = D.line;
    svgEl("text", { x: 10, y: 199, "font-family": F_M, "font-size": 6.6, "letter-spacing": -.2, fill: "#FF8000" }, g).textContent = ">>MISSION OBJECTIVES";
  }
  D.objectives.forEach(([t, done], i) => {
    const oy = 212 + i * 12.5;
    if (done) svgEl("path", { d: `M10 ${oy - 4}l2.4 2.6 4.6-5.4`, fill: "none", stroke: strogg ? ST.sel : "#B5C784", "stroke-width": 1.4 }, g);
    else if (strogg) svgEl("path", { d: `M10.8 ${oy - 7}h5l-1.3 5h-5z`, fill: ST.line, "fill-opacity": .6 }, g);
    else svgEl("path", { d: `M10 ${oy - 7}h6l-6 6z`, fill: "#B0CD6B", "fill-opacity": .4 }, g);
    if (strogg) texts.push([slabel(g, t, 22, oy, 8, { color: done ? "rgba(252,255,200,.55)" : ST.read, weight: 500, spacing: .2 }), 220 + i * 60, 200, false]);
    else svgEl("text", { x: 22, y: oy, "font-family": F_L, "font-weight": 500, "font-size": 8.4, fill: "#fff", "fill-opacity": done ? .5 : .9 }, g).textContent = t;
  });
  svgEl("path", { d: `M10 ${h - 22}H${w - 10}`, stroke: "#fff", "stroke-opacity": .1 }, g);
  svgEl("text", { x: 10, y: h - 9, "font-family": F_L, "font-weight": 500, "font-size": 7.4, fill: strogg ? "#C9A27A" : "#8C947A" }, g).textContent = D.stats;
  texts.forEach(([L, delay, dur, white]) => translate(L, opening ? delay : 0, opening ? dur : 0, { white }));
}

/* The navigation plates. Marine: dark plates with the #CCCC51 rail. Strogg: the same bleed with a 30-degree trailing
   shoulder, orange rails, a slanted marker and R_Strogg labels translated from runes on opening; focus runs the credits'
   scan bar under the label. */
let navPlates = [];
function drawNav(defs, grad, opening) {
  const N = NAV[state], strogg = state === "strogg";
  const dark = grad([[0, "#000"], [.44, "#000"], [.62, "#000", .5], [.88, "#000", 0]], false);
  const rail = grad(strogg ? [[0, ST.line, .9], [.55, ST.line, .9], [1, ST.line, .45]] : [[0, "#CCCC51"], [.44, "#CCCC51"], [.88, "#CCCC51", 0]], false);
  navPlates = [];
  N.items.forEach(([label], i) => {
    const y = N.y0 + i * 30, g = svgEl("g", { class: "np" + (i === 0 ? " f" : ""), tabindex: 0, role: "button", "aria-label": label }, svg);
    if (strogg) {
      svgEl("path", { d: `M-120 ${y + 2.8}H236L252 ${y + 12}V${y + 27.2}H-120Z`, fill: dark, class: "pf" }, g);
      svgEl("path", { d: `M-120 ${y + 27.2}H252V${y + 12}L236 ${y + 2.8}H180`, fill: "none", stroke: rail, "stroke-width": 1, class: "pf" }, g);
      svgEl("path", { d: `M-120 ${y + 25.1}H232`, stroke: ST.sel, "stroke-width": .8, class: "fx", opacity: 0 }, g);
      svgEl("path", { d: `M33.4 ${y + 11}h6l-1.6 6h-6z`, class: "mkr" }, g);
    } else {
      svgEl("rect", { x: -120, y: y + 2.8, width: 376, height: 24.4, fill: dark, class: "pf" }, g);
      svgEl("path", { d: `M-120 ${y + 27.2}H256`, stroke: rail, "stroke-width": 1, class: "pf" }, g);
      svgEl("path", { d: `M-120 ${y + 25.1}H232`, stroke: "#E38900", "stroke-width": .8, class: "fx", opacity: 0 }, g);
      svgEl("ellipse", { cx: 30, cy: y + 15, rx: 16, ry: 14, fill: "#616F26", class: "fx", opacity: 0, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
      svgEl("path", { d: `M32 ${y + 11}h6v6z`, class: "mkr" }, g);
    }
    let L = null;
    if (strogg) { L = slabel(g, label, 44, y + 19.4, 12, { cls: "nl" }); L.latin.removeAttribute("fill"); translate(L, opening ? 120 + i * 45 : 0, opening ? 200 : 0); }
    else svgEl("text", { x: 44, y: y + 19.1, class: "nl" }, g).textContent = label;
    const focus = () => { svg.querySelectorAll(".np.f").forEach(n => n.classList.remove("f")); g.classList.add("f");
      if (state === "menu") drawDetail(i);
      if (strogg && !reduce) scanBar({ parent: g, len: 150, h: 15, y: y + 15, from: 30, to: 44 + label.length * 8 + 12 }, 0, 300); };
    g.addEventListener("pointerenter", focus); g.addEventListener("focus", focus);
    navPlates.push(g);
  });
  if (strogg && opening && !reduce) scanBar({ parent: navPlates[0], len: 150, h: 15, y: N.y0 + 15, from: 30, to: 44 + 6 * 8 + 12 }, 420, 300);
}

/* The title screen's detail line: the stock message-of-the-day slot under the navigation explains the focused item,
   and for CONTINUE shows the save's levelshot, mission, difficulty and play time. */
function drawDetail(i) {
  svg.querySelector("g.hmdetail")?.remove();
  const g = svgEl("g", { class: "hmdetail" }, svg), [label, text] = NAV.menu.items[i];
  let tx = 44;
  if (i === 0) {
    const cid = id("th"); svgEl("rect", { x: 44, y: 360, width: 56, height: 31.5 }, svgEl("clipPath", { id: cid }, svg.querySelector("defs")));
    const pic = svgEl("g", { "clip-path": `url(#${cid})` }, g);
    A.levelshot?.(svgEl("g", { transform: "translate(44 360) scale(.0656) translate(107 0)" }, pic), "sp", { x0: -107, y0: 0, W: 854, H: 480 });
    svgEl("rect", { x: 44, y: 360, width: 56, height: 31.5, fill: "none", stroke: "#414624" }, g);
    tx = 108;
  }
  svgEl("text", { x: tx, y: 371, class: "cl" }, g).textContent = i === 0 ? "Air Defense Bunker" : label.charAt(0) + label.slice(1).toLowerCase();
  svgEl("text", { x: tx, y: 384, class: "cm" }, g).textContent = i === 0 ? "Corporal · 1:42 played · saved 3 min ago" : text;
}

build(false);
const seg = document.getElementById("homeState");
seg?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  seg.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); state = b.dataset.s; build(true);
  if (status) status.textContent = state === "menu" ? "Main menu: the emblem watermark keeps the trailing side; the detail line explains the focused item."
    : state === "pause" ? "Single-player pause: the current level block replaces the emblem, and Resume leads."
    : "Strogg pause: the menu's labels arrive in runes and translate, the scan bar runs under the focused plate."; });
document.getElementById("homeReplay")?.addEventListener("click", () => build(true));
})();
