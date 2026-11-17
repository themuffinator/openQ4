
(() => {
const A = window.__atlas, { svgEl } = A;
const svg = document.getElementById("wheel2");
if (!svg) return;
const F_M = "Michroma, Eurostile, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_L = "Archivo, sans-serif";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;

/* Section 14.6 at 100% HUD scale, in dp. Strip order; fixed positions. */
const R0 = 104, R1 = 232, GAP = 6, RAD = 2;
const W_Q4 = [
  ["blaster", "Blaster", "#DDEBC3", 1, Infinity, 0, 0, "BL"], ["machinegun", "Machinegun", "#FFFF00", 1, 164, 300, 40, "MG"], ["shotgun", "Shotgun", "#FF8C00", 1, 24, 50, 8, "SG"],
  ["hyperblaster", "Hyperblaster", "#0073FF", 1, 0, 300, 0, "HB"], ["grenadelauncher", "Grenade launcher", "#338F12", 1, 11, 50, 8, "GL"], ["nailgun", "Nailgun", "#99CCCC", 1, 86, 200, 50, "NG"],
  ["rocketlauncher", "Rocket launcher", "#FF4000", 1, 17, 40, 0, "RL"], ["railgun", "Railgun", "#00FF00", 1, 9, 50, 3, "RG"], ["lightninggun", "Lightning gun", "#FFFF99", 0, 0, 200, 0, "LG"],
  ["darkmattergun", "Dark matter gun", "#C433FF", 0, 0, 5, 0, "DM"], ["napalmgun", "Napalm launcher", "#FFBF40", 0, 0, 100, 0, "NP"],
];
/* The Awakening (q4xbase): slots 10-12 hold the Core Cannon (50 spikes, no clip), the napalm launcher (100, clip 20)
   and the Freeze Gun (80, regenerating); thirteen positions share the ring. */
const W_AWK = [...W_Q4.slice(0, 10), ["corecannon", "Core Cannon", "#FF5AC8", 1, 34, 50, 0, "CC"], ["napalmgun", "Napalm launcher", "#FFBF40", 1, 60, 100, 20, "NP"],
  ["freezegun", "Freeze Gun", "#9EEBFF", 1, 52, 80, 0, "FZ"]];
/* Multiplayer (section 14.6): the multiplayer roster, the gauntlet leading; no clips; icons in the item color code. */
const W_MP = [["gauntlet", "Gauntlet", "#00D9FF", 1, Infinity, 0, 0, "GA"], ["machinegun", "Machinegun", "#FFFF00", 1, 150, 300, 0, "MG"],
  ["shotgun", "Shotgun", "#FF8C00", 1, 24, 50, 0, "SG"], ["hyperblaster", "Hyperblaster", "#0073FF", 1, 120, 300, 0, "HB"],
  ["grenadelauncher", "Grenade launcher", "#338F12", 1, 10, 50, 0, "GL"], ["nailgun", "Nailgun", "#99CCCC", 1, 86, 200, 0, "NG"],
  ["rocketlauncher", "Rocket launcher", "#FF4000", 1, 17, 40, 0, "RL"], ["railgun", "Railgun", "#00FF00", 1, 9, 50, 0, "RG"],
  ["lightninggun", "Lightning gun", "#FFFF99", 1, 0, 200, 0, "LG"], ["darkmattergun", "Dark matter gun", "#C433FF", 0, 0, 5, 0, "DM"],
  ["napalmgun", "Napalm launcher", "#FFBF40", 0, 0, 100, 0, "NP"]];
const W_MP_AWK = [...W_MP.slice(0, 10), ["corecannon", "Core Cannon", "#FF5AC8", 1, 22, 50, 0, "CC"], ["napalmgun", "Napalm launcher", "#FFBF40", 0, 0, 100, 0, "NP"],
  ["freezegun", "Freeze Gun", "#9EEBFF", 1, 52, 80, 0, "FZ"]];
const ICON_CODE = { gauntlet: "#00D9FF", machinegun: "#FFFF00", shotgun: "#FF8000", hyperblaster: "#0073FF", grenadelauncher: "#338F12", nailgun: "#99CCCC",
  rocketlauncher: "#FF3300", railgun: "#00FF00", lightninggun: "#FFFFBA", darkmattergun: "#C400FF", napalmgun: "#FFBF40", corecannon: "#FF5AC8", freezegun: "#9EEBFF" };
let W = W_Q4, mp = false, awk = false;
const roster = () => mp ? (awk ? W_MP_AWK : W_MP) : (awk ? W_AWK : W_Q4);
/* Quick chat (14.6, Appendix C 1.21; #str_231261-231268), clockwise from the top. */
const CHAT = ["Attack!", "Defend!", "Need backup!", "Enemy incoming!", "On my way!", "Thanks!", "Nice shot!", "Good game!"];
let channel = 1, mpTeam = "m", stroggOn = false;   // channel 0 All, 1 Team: a team game opens on Team
const FAM = {
  marine: { icon: "#A8A360", sel: "#FF8000", ring: "#B2CC80", name: "#B0C891", read: "#DDEBC3", line: "#B3D06E" },
  mpMarine: { icon: "#A8A360", sel: "#AAE355", ring: "#AAE355", name: "#B0C891", read: "#DDEBC3", line: "#AAE355", mp: true },   // your team's text color
  mpStrogg: { icon: "#A8A360", sel: "#FF8E00", ring: "#FF8E00", name: "#B0C891", read: "#DDEBC3", line: "#FF8E00", light: "#6B3A08", mp: true },
  mpDM: { icon: "#A8A360", sel: "#B2CC80", ring: "#B2CC80", name: "#B0C891", read: "#DDEBC3", line: "#B3D06E", mp: true },
  strogg: { icon: "#F59512", sel: "#FFCC00", ring: "#F2AD0B", name: "#FCFFC8", read: "#FCFFC8", line: "#F59512" },
};
let fam = FAM.marine, chat = false, equipped = 5, previous = 2, hover = 5, lastHover = -1;
const N = () => (chat ? CHAT.length : W.length);
const span = () => 2 * Math.PI / N();
const mid = i => -Math.PI / 2 + i * span();
const P = (r, a) => `${(r * Math.cos(a)).toFixed(2)} ${(r * Math.sin(a)).toFixed(2)}`;
const at = (r, a) => [r * Math.cos(a), r * Math.sin(a)];
// annular segment whose gaps keep a constant width: offset each edge by half the gap at every radius
const seg = (i, r0, r1, gap) => {
  const a0 = mid(i) - span() / 2, a1 = mid(i) + span() / 2;
  const o0 = Math.asin(gap / 2 / r0), o1 = Math.asin(gap / 2 / r1);
  return `M${P(r0, a0 + o0)}L${P(r1, a0 + o1)}A${r1} ${r1} 0 0 1 ${P(r1, a1 - o1)}L${P(r0, a1 - o0)}A${r0} ${r0} 0 0 0 ${P(r0, a0 + o0)}Z`;
};
const band = (i, r0, r1, f0, f1, gap) => {        // part of a segment between two fractions of its span
  const a0 = mid(i) - span() / 2, a1 = mid(i) + span() / 2;
  const o1 = Math.asin(gap / 2 / r1);
  const b0 = a0 + o1 + (a1 - a0 - 2 * o1) * f0, b1 = a0 + o1 + (a1 - a0 - 2 * o1) * f1;
  return `M${P(r0, b0)}L${P(r1, b0)}A${r1} ${r1} 0 0 1 ${P(r1, b1)}L${P(r0, b1)}A${r0} ${r0} 0 0 0 ${P(r0, b0)}Z`;
};
const defs = svgEl("defs", {}, svg);
const wg = svgEl("radialGradient", { id: "wheelWash" }, defs);
svgEl("stop", { offset: .82, "stop-color": "#000", "stop-opacity": .35 }, wg); svgEl("stop", { offset: 1, "stop-color": "#000", "stop-opacity": 0 }, wg);
/* The briefing's signal layers: torn analog-video bands tinted #E0FFDD (rolling down 6 repeats a second) and
   additive scanlines, one line every two units at 0.11 (rolling 0.28 repeats a second). */
const tornPat = svgEl("pattern", { id: "tornBands", width: 180, height: 90, patternUnits: "userSpaceOnUse" }, defs);
svgEl("image", { width: 180, height: 90, href: "data:image/svg+xml," + encodeURIComponent(
  "<svg xmlns='http://www.w3.org/2000/svg' width='180' height='90'><filter id='t'><feTurbulence type='fractalNoise' baseFrequency='.004 .55' numOctaves='3' seed='7' stitchTiles='stitch'/><feColorMatrix values='0 0 0 0 .88 0 0 0 0 1 0 0 0 0 .87 0 0 0 3.2 -1.45'/></filter><rect width='180' height='90' filter='url(#t)'/></svg>") }, tornPat);
const linePat = svgEl("pattern", { id: "hubLines", width: 8, height: 2, patternUnits: "userSpaceOnUse" }, defs);
svgEl("rect", { width: 8, height: 1, fill: "#fff", "fill-opacity": .11 }, linePat);
const body = svgEl("g", {}, svg);
const fx = { segs: [], hub: null, name: null, nameClip: null, rings: [], torn: null, lines: null };

/* One frame loop for the Marine effects. */
let jobs = [], looping = false;
const run = (delay, dur, fn) => { jobs.push([performance.now() + delay, dur, fn]); if (!looping) { looping = true; requestAnimationFrame(tick); } };
function tick(now) {
  jobs = jobs.filter(([t0, dur, fn]) => { const p = dur ? Math.min(1, Math.max(0, (now - t0) / dur)) : 1; if (now >= t0) fn(p); return now < t0 + dur; });
  if (jobs.length) requestAnimationFrame(tick); else looping = false;
}
const mix = (a, b, p) => { const h = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16)); const x = h(a), y = h(b); return `rgb(${x.map((v, i) => Math.round(v + (y[i] - v) * p)).join(",")})`; };


function drawMarine() {
  svgEl("circle", { r: 280, fill: "url(#wheelWash)", class: "wash" }, body);
  // bezel: a tick ramp on the outer rim, long ticks at segment boundaries
  const ticks = svgEl("g", {}, body); fx.rings.push(ticks);
  for (let k = 0; k < 120; k++) { const [x1, y1] = at(247, -Math.PI / 2 + k * Math.PI / 60), [x2, y2] = at(252, -Math.PI / 2 + k * Math.PI / 60);
    svgEl("path", { d: `M${x1.toFixed(1)} ${y1.toFixed(1)}L${x2.toFixed(1)} ${y2.toFixed(1)}`, stroke: fam.line, "stroke-opacity": .18, "stroke-width": .9 }, ticks); }
  for (let i = 0; i < N(); i++) { const a = mid(i) + span() / 2, [x1, y1] = at(242, a), [x2, y2] = at(253, a);
    svgEl("path", { d: `M${x1.toFixed(1)} ${y1.toFixed(1)}L${x2.toFixed(1)} ${y2.toFixed(1)}`, stroke: fam.line, "stroke-opacity": .5, "stroke-width": 1.4 }, ticks); }
  fx.segs = [];
  for (let i = 0; i < N(); i++) {
    const w = chat ? null : W[i];
    const owned = chat || w[3], sel = i === hover, empty = !chat && owned && w[4] === 0;
    const r1 = sel ? R1 + 8 : R1;
    const g = svgEl("g", { "data-seg": i }, body); fx.segs.push(g);
    if (!owned) { svgEl("path", { d: seg(i, R0, R1, GAP), fill: "none", stroke: "#fff", "stroke-opacity": .15, "stroke-width": 1 }, g);
      const [nx, ny] = at(120, mid(i)); svgEl("text", { x: nx, y: ny + 4.5, "text-anchor": "middle", "font-family": F_L, "font-weight": 500, "font-size": 13, fill: "#fff", "fill-opacity": .15 }, g).textContent = i + 1; continue; }
    const plate = svgEl("g", { opacity: sel ? .7 : .5 }, g);   // group opacity keeps the rounded stroke and fill as one surface
    svgEl("path", { d: seg(i, R0 + RAD, r1 - RAD, GAP + 2 * RAD), fill: "#000", stroke: "#000", "stroke-width": 2 * RAD, "stroke-linejoin": "round" }, plate);
    if (sel) {                                     // light layer inside the selection, capped at the family's light peak
      svgEl("path", { d: seg(i, R0 + 2, r1 - 2, GAP + 4), fill: fam.light || "#616F26", "fill-opacity": .35, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
      g.ring = svgEl("path", { d: seg(i, R0, r1, GAP), fill: "none", stroke: fam.ring, "stroke-width": 2.4, "stroke-linejoin": "round" }, g);
    }
    const a = mid(i);
    if (chat) {                                    // Lowpixel 16 dp #DDEBC3 at 168 dp, wrapping inside 106 dp; 0.60 until selected
      const [x, y] = at(168, a), words = CHAT[i].length > 10 ? CHAT[i].split(" ") : [CHAT[i]];
      const t = svgEl("text", { x, y: y + 5.5 - (words.length - 1) * 9.5, "text-anchor": "middle", "font-family": F_L, "font-weight": 500, "font-size": 16, fill: "#DDEBC3", "fill-opacity": sel ? 1 : .6 }, g);
      words.forEach((wd, k) => { const ts = svgEl("tspan", { x, dy: k ? 19 : 0 }, t); ts.textContent = wd; }); continue; }
    // the ammunition band: a curved gauge along the outer edge with the Marine fill construction and cells
    if (w[4] !== Infinity) {
      const b0 = r1 - 25, b1 = r1 - 11, frac = Math.max(0, Math.min(1, w[4] / w[5]));
      svgEl("path", { d: band(i, b0, b1, 0, 1, GAP + 10), fill: w[2], "fill-opacity": .2 }, g);
      if (frac > 0) { svgEl("path", { d: band(i, b0, b1, 0, frac, GAP + 10), fill: w[2], "fill-opacity": .35 }, g);
        svgEl("path", { d: band(i, b0 + 1.2, b1 - 1.2, 0, frac, GAP + 12.4), fill: "none", stroke: w[2], "stroke-opacity": .45, "stroke-width": 2.4 }, g); }
      for (let c = 1; c < 8; c++) svgEl("path", { d: band(i, b0, b1, c / 8 - .006, c / 8 + .006, GAP + 10), fill: "#000", "fill-opacity": .32 }, g);
    }
    const [nx, ny] = at(118, a);
    svgEl("text", { x: nx, y: ny + 4.5, "text-anchor": "middle", "font-family": F_L, "font-weight": 500, "font-size": 12, fill: "#fff", "fill-opacity": .55 }, g).textContent = i + 1;
    const [ix, iy] = at(158, a);
    const col = fam.mp ? ICON_CODE[w[0]] || w[2] : sel && !empty ? fam.sel : fam.icon, op = sel && !empty ? 1 : .6;   // multiplayer keeps the color code
    if (A.ICONS[w[0]]) A.icon(g, w[0], { x: ix, y: iy - 12, h: 24, anchor: "middle", color: col, opacity: op });
    else svgEl("text", { x: ix, y: iy + 7, "text-anchor": "middle", "font-family": F_M, "font-size": 19, fill: col, "fill-opacity": op }, g).textContent = w[7];
    if (empty) { svgEl("circle", { cx: ix, cy: iy, r: 17, fill: "none", stroke: "#A50000", "stroke-width": 4 }, g);
      svgEl("path", { d: `M${ix + 12} ${iy - 12}L${ix - 12} ${iy + 12}`, stroke: "#A50000", "stroke-width": 4 }, g); }
    const [rx, ry] = at(193, a);
    svgEl("text", { x: rx, y: ry + 6, "text-anchor": "middle", "font-family": F_C, "font-weight": 600, "font-size": 17, fill: empty ? "#C72C1B" : fam.read }, g).textContent = w[4] === Infinity ? "" : w[4];
    if (i === previous) { const [px, py] = at(R0 - 3, a), [qx, qy] = at(R0 - 13, a - .05), [sx, sy] = at(R0 - 13, a + .05);
      svgEl("path", { d: `M${px} ${py}L${qx} ${qy}L${sx} ${sy}Z`, fill: fam.read, "fill-opacity": .7 }, g); }
  }
  // hub: the machinegun scope's double ring and compass dots, turned to face the selection, over a signal screen
  const hub = svgEl("g", {}, body); fx.hub = hub;
  svgEl("circle", { r: 86, fill: "#000", "fill-opacity": .55 }, hub);
  svgEl("circle", { r: 79 }, svgEl("clipPath", { id: "hubScreen" }, hub));
  const screen = svgEl("g", { "clip-path": "url(#hubScreen)" }, hub);
  fx.torn = svgEl("g", { opacity: .06 }, screen);
  const tr = svgEl("rect", { x: -180, y: -270, width: 360, height: 540, fill: "url(#tornBands)" }, fx.torn);
  if (!reduce) svgEl("animateTransform", { attributeName: "transform", type: "translate", from: "0 0", to: "0 540", dur: "1s", repeatCount: "indefinite" }, tr);
  fx.lines = svgEl("g", { style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, screen);
  const lr = svgEl("rect", { x: -90, y: -92, width: 180, height: 184, fill: "url(#hubLines)" }, fx.lines);
  if (!reduce) svgEl("animateTransform", { attributeName: "transform", type: "translate", from: "0 0", to: "0 2", dur: "3.6s", repeatCount: "indefinite" }, lr);
  const dbl = svgEl("g", {}, hub); fx.rings.push(dbl);
  fx.hubRings = [86, 80].map(r => svgEl("circle", { r, fill: "none", stroke: fam.line, "stroke-opacity": .35, "stroke-width": 1.1 }, dbl));
  const dots = svgEl("g", { transform: `rotate(${hover < 0 ? 0 : (mid(hover) + Math.PI / 2) * 180 / Math.PI})` }, hub);
  for (let k = 0; k < 16; k++) { const [x, y] = at(93, -Math.PI / 2 + k * Math.PI / 8); svgEl("circle", { cx: x, cy: y, r: k ? 1.6 : 2.6, fill: k ? fam.line : fam.sel, "fill-opacity": k ? .35 : 1 }, dots); }
  if (hover >= 0) {                                // quick chat opens with nothing selected and no pointer
    const [pxx, pyy] = at(R0 - 6, mid(hover)), [l1x, l1y] = at(R0 - 20, mid(hover) - .1), [l2x, l2y] = at(R0 - 20, mid(hover) + .1);
    svgEl("path", { d: `M${pxx} ${pyy}L${l1x} ${l1y}L${l2x} ${l2y}Z`, fill: fam.sel }, hub);
  }
  // the name slot: a clip that tunes in from a 2 u line, as the briefing's picture does
  const slot = svgEl("clipPath", { id: "nameSlot" }, hub); fx.nameClip = svgEl("rect", { x: -80, y: -26, width: 160, height: 22 }, slot);
  const nameG = svgEl("g", { "clip-path": "url(#nameSlot)" }, hub);
  if (chat) {                                       // the channel: ALL in #FFFF8D or TEAM in your team's text color, Marine 16 dp
    const team = mpTeam !== "dm", word = team && channel ? "TEAM" : "ALL";
    fx.nameTwo = false; fx.name = svgEl("text", { y: -4, "text-anchor": "middle", "font-family": F_M, "font-size": 9, fill: word === "ALL" ? "#FFFF8D" : fam.sel }, nameG); fx.name.textContent = word;
    fx.chatWord = word === "ALL" ? "#FFFF8D" : fam.sel;
    if (team) {                                     // in a team game: the zoom control's key and Channel, Lowpixel 12 dp
      const hint = svgEl("g", {}, hub), kw = 40;
      svgEl("path", { d: `M${-kw / 2 - 26} 9H${kw / 2 - 26}V23H${-kw / 2 - 26 + 3.5}L${-kw / 2 - 26} ${23 - 3.5}Z`, fill: "#fff", "fill-opacity": .85 }, hint);
      svgEl("text", { x: -26, y: 19.2, "text-anchor": "middle", "font-family": "IBM Plex Mono, monospace", "font-weight": 500, "font-size": 7.5, fill: "#10140A" }, hint).textContent = "MOUSE2";
      svgEl("text", { x: -1, y: 20.5, "font-family": F_L, "font-weight": 500, "font-size": 12, fill: "#B0C891" }, hint).textContent = "Channel";
    }
  }
  else {
    const w = W[hover];
    const nm = w[1].toUpperCase(), lines = nm.length > 12 && nm.includes(" ") ? nm.split(" ") : [nm];   // labels wrap before they shrink
    fx.name = svgEl("text", { y: lines.length > 1 ? -21 : -9, "text-anchor": "middle", "font-family": F_M, "font-size": 12, fill: fam.name }, nameG);
    lines.forEach((l, k) => { const ts = svgEl("tspan", { x: 0, dy: k ? 15 : 0 }, fx.name); ts.textContent = l; });
    if (lines.length > 1) { fx.nameClip.setAttribute("y", -34); fx.nameClip.setAttribute("height", 36); fx.nameTwo = true; } else fx.nameTwo = false;
    svgEl("text", { y: lines.length > 1 ? 24 : 18, "text-anchor": "middle", "font-family": F_C, "font-weight": 600, "font-size": 21, fill: fam.read }, hub).textContent =
      w[4] === Infinity ? "∞" : w[6] ? `${Math.min(w[6], w[4])} / ${Math.max(0, w[4] - w[6])}` : `${w[4]}`;
  }
}

function draw() {
  body.innerHTML = ""; fx.rings = [];
  const strogg = fam === FAM.strogg && A.drawStrogg && !mp;
  if (strogg) A.drawStrogg(body, { hover, equipped, previous, chat, W, CHAT, n: N(), span: span(), mid, P, reduce });
  else drawMarine();
  if (hover !== lastHover && lastHover !== -1) (strogg ? A.stroggSelect?.() : select());
  lastHover = hover;
}
/* Tune-in: the name slot opens from a 2 u line over `dur` while the name fades in white and settles to its color
   100 ms later over 100 ms, as the briefing's labels do. */
function tuneIn(delay, dur) {
  if (!fx.name) return;
  const target = chat ? fx.chatWord || fam.name : fam.name;
  fx.name.setAttribute("fill", "#FFFFFF");
  const top = fx.nameTwo ? -34 : -26, h = fx.nameTwo ? 36 : 22, mid = top + h / 2;
  run(delay, dur, p => { fx.nameClip.setAttribute("y", mid - 1 - (h / 2 - 1) * p); fx.nameClip.setAttribute("height", 2 + (h - 2) * p); fx.name.setAttribute("opacity", p); });
  run(delay + dur, 100, () => {});
  run(delay + dur + 100, 100, p => fx.name.setAttribute("fill", mix("#FFFFFF", target, p)));
}
/* A static burst: the torn bands rise to `peak` in 50 ms and decay to the 0.06 rest level over `decay`. */
function burst(delay, peak = .4, decay = 500) {
  const el = fx.torn; if (!el) return;
  run(delay, 50, p => el.setAttribute("opacity", .06 + (peak - .06) * p));
  run(delay + 50, decay, p => el.setAttribute("opacity", peak - (peak - .06) * p));
}
/* wheel.select, Marine "cut": each new selection is a cut in the transmission. */
function select() {
  if (reduce || !fx.name) return;
  burst(0, .4, 300);
  tuneIn(0, 120);
  const ring = fx.segs[hover]?.ring; if (ring) run(0, 150, p => ring.setAttribute("stroke", mix("#FFFFFF", fam.ring, p)));
}
/* wheel.open, Marine "tune-in" (the MCC briefing's power-on): the hub opens from a slit in 150 ms with a one-frame
   ring flash, the segments deploy clockwise from the top 8 ms apart, a static burst decays, the name tunes in from a
   2 u line, and the hub jolts to 90% and back. Plates and icons are readable by 150 ms. */
function openFx() {
  if (fam === FAM.strogg && A.stroggOpen) { A.stroggOpen(); return; }
  body.setAttribute("opacity", 1);
  draw();
  if (reduce) { body.setAttribute("opacity", 0); run(0, 80, p => body.setAttribute("opacity", p)); return; }
  fx.segs.forEach(s => s.setAttribute("opacity", 0));
  fx.segs.forEach((s, i) => run(i * 8, 90, p => { s.setAttribute("opacity", p); s.setAttribute("transform", `scale(${.9 + .1 * p})`); }));
  const hub = fx.hub;
  run(0, 150, p => hub.setAttribute("transform", `scale(${.55 + .45 * p} ${.06 + .94 * p})`));
  fx.hubRings.forEach(c => { run(150, 0, () => c.setAttribute("stroke", "#FFFFFF")); run(150, 0, () => c.setAttribute("stroke-opacity", .9));
    run(166, 50, p => { c.setAttribute("stroke", mix("#FFFFFF", fam.line, p)); c.setAttribute("stroke-opacity", .9 - .55 * p); }); });
  burst(150, .4, 500);
  if (fx.name) { fx.name.setAttribute("opacity", 0); tuneIn(200, 200); }
  run(250, 50, p => hub.setAttribute("transform", `scale(${1 - .1 * p})`));
  run(300, 50, p => hub.setAttribute("transform", `scale(${.9 + .1 * p})`));
}
/* wheel.close, Marine "sign-off": segments drop out over 60 ms while the hub collapses to the slit over 150 ms under a
   static burst, then the wheel is gone. */
function closeFx(done) {
  if (fam === FAM.strogg && A.stroggClose) { A.stroggClose(done); return; }
  if (reduce) { run(0, 80, p => body.setAttribute("opacity", 1 - p)); run(90, 0, () => done?.()); return; }
  fx.segs.forEach(s => run(0, 60, p => s.setAttribute("opacity", 1 - p)));
  run(0, 150, p => { fx.hub.setAttribute("transform", `scale(${1 - .45 * p} ${1 - .94 * p})`); fx.torn?.setAttribute("opacity", .8 * (1 - p)); });
  run(0, 150, p => body.querySelector(".wash")?.setAttribute("opacity", 1 - p));
  run(160, 0, () => { body.setAttribute("opacity", 0); done?.(); });
}
draw();

/* Selection by angle past 32 dp, with 4 degrees of hysteresis; unowned segments are skipped. */
const toLocal = e => { const p = svg.createSVGPoint(); p.x = e.clientX; p.y = e.clientY; return p.matrixTransform(svg.getScreenCTM().inverse()); };
svg.addEventListener("pointermove", e => {
  const { x, y } = toLocal(e);
  if (Math.hypot(x, y) < 32) return;
  let a = Math.atan2(y, x) + Math.PI / 2; if (a < 0) a += 2 * Math.PI;
  let i = Math.round(a / span()) % N();
  const edge = Math.abs(((a / span()) % 1) - .5) * span();          // distance from the nearest boundary
  if (i !== hover && edge < 4 * Math.PI / 180) return;
  if (!chat && !W[i][3]) return;
  if (i !== hover) { hover = i; draw(); }
});
A.redrawWheel = draw;
const status = document.getElementById("wStatus");
const equip = () => {
  if (chat) { status.textContent = hover < 0 ? "Released inside the dead zone: nothing is sent." : `Sent to ${mpTeam !== "dm" && channel ? "team" : "all"}: “${CHAT[hover]}”, in the sender's language.`; return; }
  const w = W[hover];
  if (w[4] === 0) { status.textContent = `${w[1]} has no ammunition; the ${W[equipped][1].toLowerCase()} stays equipped.`; return; }
  if (hover !== equipped) { previous = equipped; equipped = hover; }
  status.textContent = `Equipped: ${w[1].toLowerCase()}. The notch marks the ${W[previous][1].toLowerCase()}, the weapon a tap returns to.`;
  closeFx(() => setTimeout(() => { lastHover = -1; openFx(); }, 350));
};
svg.addEventListener("click", equip);
svg.addEventListener("keydown", e => {
  const d = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }[e.key];
  if (d) { e.preventDefault(); let i = hover; do { i = (i + d + N()) % N(); } while (!chat && !W[i][3]); hover = i; draw(); }
  if (e.key === "Enter" || e.key === " ") { e.preventDefault(); equip(); }
});
const toggle = (idName, fn) => { const b = document.getElementById(idName);
  b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); fn(on); lastHover = -1; openFx(); }, true); };
const famFor = (stroggOn) => mp || chat ? FAM[{ m: "mpMarine", s: "mpStrogg", dm: "mpDM" }[mpTeam]] : stroggOn ? FAM.strogg : FAM.marine;
toggle("wStrogg", on => { stroggOn = on; fam = famFor(on); });
toggle("wChat", on => { chat = on; hover = on ? -1 : equipped; channel = 1; fam = famFor(stroggOn); });   // multiplayer only, always the Marine construction
const reroster = () => { W = roster(); equipped = Math.min(equipped, W.length - 1); previous = Math.min(previous, W.length - 1); if (!chat) hover = equipped; };
toggle("wAwk", on => { awk = on; reroster(); });
toggle("wMP", on => { mp = on; fam = famFor(stroggOn); reroster(); });
document.getElementById("wTeam")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; mpTeam = b.dataset.team;
  document.querySelectorAll("#wTeam .seg").forEach(x => x.setAttribute("aria-pressed", x === b)); if (mp || chat) { fam = famFor(stroggOn); channel = 1; lastHover = -1; draw(); } });
document.getElementById("wChannel")?.addEventListener("click", () => { if (!chat || mpTeam === "dm") return; channel = 1 - channel; draw(); select(); });
document.getElementById("wOpen")?.addEventListener("click", () => { lastHover = -1; openFx(); });
document.getElementById("wClose")?.addEventListener("click", () => closeFx(() => setTimeout(() => { body.setAttribute("opacity", 1); lastHover = -1; draw(); }, 600)));
})();
