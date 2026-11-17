
(() => {
const A = window.__atlas, { svgEl } = A;
const F_S = "Oxanium, Chakra Petch, sans-serif";        // stand-in for R_Strogg; backslanted 11.4 degrees like the stock face
const C = { fill: "#FF9000", icon: "#F59512", sel: "#FFCC00", ring: "#F2AD0B", read: "#FCFFC8", path: "#F59512", flash: "#FFD06F", bar: "#FF8000" };
const R0 = 104, R1 = 232, GAP = 6, TW = -0.07;           // TW: the ring twists about 4 degrees backward
const svg = document.getElementById("wheel2");
if (!svg) return;
const defs = svg.querySelector("defs");
const grainPat = svgEl("pattern", { id: "stroggGrain", width: 120, height: 120, patternUnits: "userSpaceOnUse" }, defs);
svgEl("image", { width: 120, height: 120, href: "data:image/svg+xml," + encodeURIComponent(
  "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='120'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='1.1' numOctaves='2' stitchTiles='stitch'/><feColorMatrix values='0 0 0 0 1 0 0 0 0 .75 0 0 0 0 .4 0 0 0 1.4 -.55'/></filter><rect width='120' height='120' filter='url(#n)'/></svg>") }, grainPat);
const pt = (P, r, a) => P(r, a).split(" ").map(Number);
const skew = (x, y) => `translate(${x} ${y}) skewX(11.4) translate(${-x} ${-y})`;   // R_Strogg's backslant about the baseline
let S = {}, reduce = false;

/* An angular claw: raised leading tooth with 30-degree shoulders, chamfered trailing corner, notched inner edge. */
function claw(P, mid, span, i, r0, r1) {
  const a = mid(i), a0 = a - span / 2, a1 = a + span / 2;
  const o0 = Math.asin(GAP / 2 / r0), o1 = Math.asin(GAP / 2 / r1), sh = 17 / r1;
  const pts = [
    pt(P, r0, a0 + o0), pt(P, r1, a0 + o1 + TW), pt(P, r1 + 10, a0 + o1 + TW + sh), pt(P, r1 + 10, a0 + .42 * span + TW),
    pt(P, r1, a0 + .42 * span + TW + sh), pt(P, r1, a1 - o1 + TW - 14 / r1), pt(P, r1 - 14, a1 - o1 + TW),
    pt(P, r0, a1 - o0), pt(P, r0, a + .045), pt(P, r0 + 8, a), pt(P, r0, a - .045),
  ];
  return "M" + pts.map(p => p.map(v => v.toFixed(2)).join(" ")).join("L") + "Z";
}
/* A translatable label: the same text in runes and in R_Strogg, baselines together, runes 1.2x the cap height. */
function label(parent, text, x, y, size, { color = C.read, weight = 600, spacing = .8, anchor = "middle", kind = "row" } = {}) {
  const g = svgEl("g", {}, parent);
  const rune = A.runes(g, text.toUpperCase(), x, y, size * .7 * 1.2, { anchor, fill: kind === "heading" ? C.sel : color, opacity: 0 });
  const latin = svgEl("text", { x, y, "text-anchor": anchor, "font-family": F_S, "font-weight": weight, "font-size": size, "letter-spacing": spacing, fill: color, transform: skew(x, y) }, g);
  latin.textContent = text;
  return { g, rune, latin, x, y, color, kind, text, size, anchor };
}
/* One frame loop drives every running effect: [start ms, duration ms, fn(p)]. */
let jobs = [], loop = false;
const run = (delay, dur, fn) => { jobs.push([performance.now() + delay, dur, fn]); if (!loop) { loop = true; requestAnimationFrame(tick); } };
function tick(now) {
  jobs = jobs.filter(([t0, dur, fn]) => { const p = dur ? Math.min(1, Math.max(0, (now - t0) / dur)) : (now >= t0 ? 1 : 0); if (now >= t0) fn(p); return now < t0 + dur; });
  if (jobs.length) requestAnimationFrame(tick); else loop = false;
}
const scaleAbout = (el, s, x, y) => el.setAttribute("transform", `translate(${x} ${y}) scale(${s}) translate(${-x} ${-y})`);
const mix = (a, b, p) => { const h = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16)); const x = h(a), y = h(b); return `rgb(${x.map((v, i) => Math.round(v + (y[i] - v) * p)).join(",")})`; };

/* The credits' translation, scaled to HUD time (section 8): runes show, then both copies pop 6% larger and settle
   while the runes cross-fade into R_Strogg; names reach white and settle to their color; a heading gets the scan bar
   sliding in with accel(40%, 60%) and dimming over three times its slide. */
function translate(L, delay, dur, { white = false, hold = 60, settle = 150, pop = .06, bar = null } = {}) {
  if (!L) return;
  if (reduce) { L.rune.setAttribute("opacity", 0); L.latin.setAttribute("opacity", 1); return; }
  L.rune.setAttribute("opacity", .8); L.latin.setAttribute("opacity", 0);
  const cx = L.x, cy = L.y - L.size * .35;
  run(delay, dur, p => {
    L.rune.setAttribute("opacity", .8 * (1 - p)); scaleAbout(L.rune, 1 + pop * (1 - p), cx, cy);
    L.latin.setAttribute("opacity", p); L.latin.setAttribute("transform", `translate(${cx} ${cy}) scale(${1 + pop * .9 * (1 - p)}) translate(${-cx} ${-cy}) ${skew(L.x, L.y)}`);
    if (white) L.latin.setAttribute("fill", "#FFFFFF");
  });
  if (white) run(delay + dur + hold, settle, p => L.latin.setAttribute("fill", mix("#FFFFFF", L.color, p)));
  if (bar) {
    const slide = dur * .3, dim = slide * 3.33, { from, to } = bar;
    const g = A.creditsBar(bar.parent, bar.len, bar.h, C.bar); g.setAttribute("opacity", 0);
    run(delay, dim, p => { const q = A.accelDecel(p * dim, slide * .4, slide * .6);
      g.setAttribute("transform", `translate(${from + (to - from) * q} ${bar.y})`); g.setAttribute("opacity", 1 - p); });
    run(delay + dim + 20, 0, () => g.remove());
  }
}

A.drawStrogg = (body, s) => {
  const { hover, previous, chat, W, CHAT, n, span, mid, P } = s;
  reduce = s.reduce;
  S = { body, plates: [], flashes: [], rows: [], paths: [], n, hover, chat, W, CHAT, span, mid, P };
  svgEl("circle", { r: 290, fill: "url(#wheelWash)", class: "wash" }, body);
  // dual counter-rotating rings (the Strogg terminals' +0.2 and -0.5 turns a second)
  const ringG = svgEl("g", {}, body); S.ringG = ringG;
  [[93, C.icon, .38, "14 6 3 6", 5, 360], [99, C.sel, .24, "2 10", 2, -360]].forEach(([r, col, op, dash, dur, to]) => {
    const c = svgEl("circle", { r, fill: "none", stroke: col, "stroke-opacity": op, "stroke-width": 1.3, "stroke-dasharray": dash }, ringG);
    if (!reduce) svgEl("animateTransform", { attributeName: "transform", type: "rotate", from: "0", to: String(to), dur: dur + "s", repeatCount: "indefinite" }, c);
  });
  // plates, with the grain masked to their union and an additive flash layer for the boot cascade
  const clip = svgEl("clipPath", { id: "stroggClaws" }, body);
  for (let i = 0; i < n; i++) {
    const owned = chat || W[i][3], sel = i === hover;
    const d = claw(P, mid, span, i, R0, sel ? R1 + 8 : R1);
    const g = svgEl("g", {}, body); S.plates[i] = g;
    if (!owned) { svgEl("path", { d, fill: "none", stroke: C.icon, "stroke-opacity": .16, "stroke-dasharray": "5 4" }, g); continue; }
    svgEl("path", { d, fill: "#000", "fill-opacity": sel ? .72 : .55 }, g);
    svgEl("path", { d }, clip);
    if (sel) svgEl("path", { d, fill: "none", stroke: C.ring, "stroke-width": 2.4, "stroke-linejoin": "miter" }, g);
    S.flashes[i] = svgEl("path", { d, fill: C.flash, opacity: 0, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, body);
  }
  S.grain = svgEl("g", { "clip-path": "url(#stroggClaws)", opacity: .2, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, body);
  const gr = svgEl("rect", { x: -500, y: -1000, width: 1000, height: 1500, fill: "url(#stroggGrain)" }, S.grain);   // large enough to stay covered while it drifts
  if (!reduce) svgEl("animateTransform", { attributeName: "transform", type: "translate", from: "0 0", to: "-120 480", dur: "2s", repeatCount: "indefinite" }, gr);
  // circuit paths from the core to every owned plate
  for (let i = 0; i < n; i++) {
    if (!(chat || W[i][3])) continue;
    const a = mid(i), sel = i === hover;
    const p1 = pt(P, 88, a), p2 = pt(P, 95, a), p3 = pt(P, 99, a + .1), p4 = pt(P, R0 - 5, a + .1);
    S.paths.push(svgEl("path", { d: `M${p1.join(" ")}L${p2.join(" ")}L${p3.join(" ")}L${p4.join(" ")}`, fill: "none", stroke: sel ? C.sel : C.path, "stroke-opacity": sel ? .95 : .32, "stroke-width": sel ? 1.6 : 1 }, body));
    svgEl("rect", { x: p4[0] - 2.2, y: p4[1] - 2.2, width: 4.4, height: 4.4, fill: sel ? C.sel : "none", stroke: sel ? C.sel : C.path, "stroke-opacity": sel ? 1 : .4 }, body);
  }
  // contents: icons stay upright and never translate; readouts and phrases are labels
  for (let i = 0; i < n; i++) {
    const a = mid(i) + TW / 2, sel = i === hover, g = S.plates[i];
    if (chat) { const [x, y] = pt(P, 168, a);
      S.rows[i] = label(g, CHAT[i].toUpperCase(), x, y + 5, 12.5, { color: sel ? C.sel : C.read, spacing: .4 }); continue; }
    const w = W[i];
    if (!w[3]) { const [x, y] = pt(P, 150, a); svgEl("text", { x, y: y + 4, "text-anchor": "middle", "font-family": F_S, "font-size": 12, fill: C.icon, "fill-opacity": .2, transform: skew(x, y + 4) }, g).textContent = i + 1; continue; }
    const empty = w[4] === 0;
    const [nx, ny] = pt(P, 121, mid(i) + .14);
    svgEl("text", { x: nx, y: ny + 4, "text-anchor": "middle", "font-family": F_S, "font-weight": 500, "font-size": 11, fill: C.read, "fill-opacity": .5, transform: skew(nx, ny + 4) }, g).textContent = i + 1;
    const [rx, ry] = pt(P, 142, a);
    if (w[4] !== Infinity) S.rows[i] = label(g, String(w[4]), rx, ry + 6, 17, { color: empty ? "#C72C1B" : C.read });
    const [ix, iy] = pt(P, 182, a);
    const col = sel && !empty ? C.sel : C.icon, op = sel && !empty ? 1 : .6;
    if (A.ICONS[w[0]]) A.icon(g, w[0], { x: ix, y: iy - 13, h: 26, anchor: "middle", color: col, opacity: op });
    else svgEl("text", { x: ix, y: iy + 7, "text-anchor": "middle", "font-family": F_S, "font-weight": 600, "font-size": 20, "letter-spacing": 1, fill: col, "fill-opacity": op }, g).textContent = w[7];
    if (empty) { svgEl("circle", { cx: ix, cy: iy, r: 16, fill: "none", stroke: "#A50000", "stroke-width": 4 }, g);
      svgEl("path", { d: `M${ix + 11} ${iy - 11}L${ix - 11} ${iy + 11}`, stroke: "#A50000", "stroke-width": 4 }, g); }
    if (w[4] !== Infinity) {                 // reserve as eight slanted cells along the outer edge
      const r1 = sel ? R1 + 8 : R1, lo = r1 - 26, hi = r1 - 15;
      const aStart = mid(i) - span / 2 + TW + .05, aEnd = mid(i) + span / 2 + TW - .12, cw = (aEnd - aStart) / 8;
      const lit = Math.round(8 * Math.min(1, w[4] / w[5]));
      for (let k = 0; k < 8; k++) {
        const b0 = aStart + k * cw + cw * .12, b1 = aStart + (k + 1) * cw - cw * .12, sk = cw * .35;
        const q = [pt(P, lo, b0), pt(P, lo, b1), pt(P, hi, b1 + sk), pt(P, hi, b0 + sk)];
        svgEl("path", { d: "M" + q.map(p => p.join(" ")).join("L") + "Z", fill: C.fill, "fill-opacity": k < lit ? .8 : .14 }, g);
      }
    }
    if (i === previous) { const q = [pt(P, R0 - 3, mid(i)), pt(P, R0 - 12, mid(i) - .06), pt(P, R0 - 12, mid(i) + .06)];
      svgEl("path", { d: "M" + q.map(p => p.join(" ")).join("L") + "Z", fill: C.read, "fill-opacity": .6 }, g); }
  }
  // octagonal core with a lower tab, like the boss bar plate; the translation plays inside it
  const hub = svgEl("g", {}, body); S.hub = hub;
  const oct = [...Array(8)].map((_, k) => pt(P, 84, -Math.PI / 2 + Math.PI / 8 + k * Math.PI / 4));
  const octD = "M" + oct.map(p => p.join(" ")).join("L") + "Z";
  svgEl("path", { d: octD, fill: "#000", "fill-opacity": .68, stroke: C.path, "stroke-opacity": .5, "stroke-width": 1.2 }, hub);
  svgEl("path", { d: "M-20 78L20 78L13 92L-13 92Z", fill: "#000", "fill-opacity": .68, stroke: C.path, "stroke-opacity": .5, "stroke-width": 1.2 }, hub);
  svgEl("path", { d: octD }, svgEl("clipPath", { id: "stroggCore" }, hub));
  S.barLayer = svgEl("g", { "clip-path": "url(#stroggCore)" }, hub);
  const ch = [pt(P, 100, mid(hover)), pt(P, 90, mid(hover) - .07), pt(P, 93, mid(hover)), pt(P, 90, mid(hover) + .07)];
  svgEl("path", { d: "M" + ch.map(p => p.join(" ")).join("L") + "Z", fill: C.sel }, body);
  const name = chat ? "TEAM" : W[hover][1].toUpperCase(), lines = name.length > 12 && name.includes(" ") ? name.split(" ") : [name];   // wrap before shrinking
  S.heads = lines.map((l, k) => label(hub, l, 0, lines.length > 1 ? -18 + k * 17 : -4, 12.5, { color: C.sel, spacing: 1, kind: "heading" }));
  S.head = S.heads[0];
  S.sub = null;
  if (!chat) { const w = W[hover];
    S.sub = label(hub, w[4] === Infinity ? "∞" : w[6] ? `${Math.min(w[6], w[4])} / ${Math.max(0, w[4] - w[6])}` : `${w[4]}`, 0, lines.length > 1 ? 34 : 26, 21, { color: C.read }); }
};
const textW = L => (A.measureText ? A.measureText(L.text, L.size, F_S, 600) : L.text.length * L.size * .7) + L.text.length * 1;
const headBar = (L = S.head) => { const w = textW(L);
  return { parent: S.barLayer, len: Math.max(140, w + 70), h: L.size * 1.7, y: L.y - L.size * .35, from: -84, to: w / 2 + 12 }; };

/* wheel.select, Strogg: the new name shows in runes and translates with the credits' heading recipe. */
A.stroggSelect = () => {
  if (!S.head) return;
  S.heads.forEach((L, k) => translate(L, 40 + k * 50, 300, { bar: headBar(L) }));
  translate(S.sub, 55, 150, { white: true, hold: 60, settle: 150 });
};
/* wheel.open, Strogg: plates boot in scrambled order under additive flashes (the stroggification monitor's hex
   cascade), the rings spin up, the circuit paths trace out from the core, and every label translates, the ring's
   readouts clockwise from the top as list rows and the name as a heading with the scan bar. */
const ORDER = [5, 0, 8, 3, 10, 1, 6, 9, 2, 7, 4];
A.stroggOpen = () => {
  A.redrawWheel?.();
  const body = S.body; if (!body) return;
  if (reduce) { body.setAttribute("opacity", 0); run(0, 80, p => body.setAttribute("opacity", p)); return; }
  body.setAttribute("opacity", 1);
  const order = ORDER.filter(i => i < S.n).concat([...Array(S.n).keys()].filter(i => !ORDER.includes(i)));
  S.plates.forEach(g => g && g.setAttribute("opacity", 0));
  order.forEach((i, k) => { const g = S.plates[i], f = S.flashes[i];
    run(k * 12, 0, () => g?.setAttribute("opacity", 1));
    if (f) run(k * 12, 150, p => f.setAttribute("opacity", .9 * (1 - p))); });
  S.hub.setAttribute("opacity", 0); run(0, 120, p => S.hub.setAttribute("opacity", p));
  run(0, 300, p => S.ringG.setAttribute("transform", `rotate(${-120 * (1 - p) * (1 - p)})`));
  S.paths.forEach(path => { const L = path.getTotalLength(); path.setAttribute("stroke-dasharray", `${L} ${L}`); path.setAttribute("stroke-dashoffset", L);
    run(40, 150, p => path.setAttribute("stroke-dashoffset", L * (1 - p))); });
  run(0, 250, p => S.grain.setAttribute("opacity", .6 - .4 * p));
  const all = [...S.rows, ...S.heads, S.sub].filter(Boolean);
  all.forEach(L => { L.rune.setAttribute("opacity", 0); L.latin.setAttribute("opacity", 0); });
  run(0, 100, p => all.forEach(L => L.rune.setAttribute("opacity", .8 * p)));
  S.rows.forEach((L, i) => translate(L, 140 + i * 18, 90, { white: true, hold: 100, settle: 150, pop: .05 }));
  S.heads.forEach((L, k) => translate(L, 160 + k * 50, 300, { bar: headBar(L) }));
  translate(S.sub, 175, 150, { white: true, hold: 60, settle: 150 });
};
/* wheel.close, Strogg: the plates flash and drop out in scrambled order, then the core and rings fade. */
A.stroggClose = done => {
  const body = S.body; if (!body) { done?.(); return; }
  if (reduce) { run(0, 80, p => body.setAttribute("opacity", 1 - p)); run(90, 0, () => done?.()); return; }
  const order = [...ORDER].reverse().filter(i => i < S.n);
  order.forEach((i, k) => { const g = S.plates[i], f = S.flashes[i];
    if (f) run(k * 8, 60, p => f.setAttribute("opacity", .8 * (1 - p)));
    run(k * 8, 60, p => g?.setAttribute("opacity", 1 - p)); });
  run(40, 120, p => { S.hub.setAttribute("opacity", 1 - p); S.ringG.setAttribute("opacity", 1 - p); S.grain.setAttribute("opacity", .2 * (1 - p)); body.querySelector(".wash")?.setAttribute("opacity", 1 - p); });
  run(170, 0, () => { body.setAttribute("opacity", 0); done?.(); });
};
})();
