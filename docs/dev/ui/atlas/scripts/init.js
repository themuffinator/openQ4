
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const svg = document.getElementById("inSvg");
if (!svg || !hud) return;
const F_M = "Michroma, Eurostile, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter";
let uid = 0; const id = p => `in${p}${++uid}`;

/* The initializing screen (section 14.17). The stock draws gfx/splashScreen: splash.tga, 512x512, stretched over the
   640x480 canvas. Its art is drawn pre-squashed, so the rings are circles on the canvas. Everything here is measured on
   the canvas, in u. The rings are concentric about the emblem's own ring. */
const CX = 319, CY = 214.5;
const TONE = { 26: "#171A09", 27: "#181B09", 28: "#181C09", 29: "#191D0A", 30: "#1A1E0A", 31: "#1C1F0A", 33: "#1D210B",
  32: "#1C200B", 35: "#1E230C", 36: "#1F240C", 37: "#1F250C", 38: "#20260C", 39: "#21270C" };
const CLEAR = "#181A08";          // openQ4's pillarbox fill under the aspect-corrected picture: 24,26,8
const INK = "#0E1000", LETTER = "#F2F0EC", GLOW = "#FF8340", STATUS = "#F09E0D";
const BAND_TURN = 180000, TEETH_TURN = 120000;   // ms per turn: the band counterclockwise, the teeth clockwise
/* The stock's startup messages (#str_104343-104351) with illustrative durations and the rule segment each fills
   (RetainedUI_DrawInitialization: SOUND to MENUS are steps 1-6, anything else 0); LOADING is printed twice. */
const STEPS = [["LOADING...", 0, 700], ["LOADING...", 0, 500], ["INITIALIZING SOUND...", 1, 900], ["INITIALIZING NETWORK...", 2, 650],
  ["INITIALIZING RENDER SYSTEM...", 3, 1300], ["INITIALIZING USER INTERFACE...", 4, 900], ["LOADING GAME...", 5, 1200],
  ["INITIALIZING MENUS...", 6, 1100]];
const NSTEP = 7;
/* QUAKE 4 in the q4text letterforms (1024x256), traced from the texture's white core; the splash draws the same
   logotype at 0.367 u per texel from 129.4,148.8 u. */
const GLYPHS = window.__atlas.Q4TEXT;
const WORDMARK = "translate(129.42 148.76) scale(.3667)";

/* Geometry: angles in degrees from the top, clockwise. */
const r2 = v => Math.round(v * 100) / 100;
const pt = (r, a) => { const t = a * Math.PI / 180; return [r2(CX + r * Math.sin(t)), r2(CY - r * Math.cos(t))]; };
const disc = r => `M${r2(CX - r)} ${CY}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0Z`;
const ring = (g, r0, r1, fill) => svgEl("path", { d: disc(r1) + disc(r0), fill, "fill-rule": "evenodd" }, g);
const sector = (r0, r1, a0, a1) => {
  const [x0, y0] = pt(r1, a0), [x1, y1] = pt(r1, a1), [x2, y2] = pt(r0, a1), [x3, y3] = pt(r0, a0);
  return `M${x0} ${y0}A${r1} ${r1} 0 0 1 ${x1} ${y1}L${x2} ${y2}A${r0} ${r0} 0 0 0 ${x3} ${y3}Z`;
};
const radial = (defs, r, stops) => {
  const gid = id("rg"), g = svgEl("radialGradient", { id: gid, cx: CX, cy: CY, r, gradientUnits: "userSpaceOnUse" }, defs);
  stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${gid})`;
};
/* Remastered accents: the sweep turns clockwise once every 10 s from its key light at -38 degrees; the halo breathes
   over 6 s; the glint crosses the lettering in 900 ms every 9 s; embers rise for 6-11 s each. */
const SWEEP_TURN = 10000, BREATHE = 6000, GLINT_EVERY = 9000, KEY_LIGHT = -38;
/* build_retained_initialization.py's embers: diamonds at fixed places in the ring assembly, all starting together. */
const MOTES = Array.from({ length: 14 }, (_, n) => { const r = .6 + (n % 4) * .2;
  return [r2(130 + (n * 71) % 370 - 1 + r), r2(310 + (n * 37) % 90 - 25.5 + r), r2(r), 110 + n * 7, 6000 + (n * 379) % 5000]; });

/* Timeline helpers: cubic-bezier easings and keyed tracks, [ms, value, easing of the interval that starts there]. */
const bez = (x1, y1, x2, y2) => p => { if (p <= 0) return 0; if (p >= 1) return 1; let lo = 0, hi = 1, s = p;
  for (let i = 0; i < 24; i++) { s = (lo + hi) / 2; const x = 3 * (1 - s) * (1 - s) * s * x1 + 3 * (1 - s) * s * s * x2 + s * s * s; if (x < p) lo = s; else hi = s; }
  return 3 * (1 - s) * (1 - s) * s * y1 + 3 * (1 - s) * s * s * y2 + s * s * s; };
const EASE = bez(.33, 0, .2, 1), EASE_OUT = bez(.16, 1, .3, 1), DROP = bez(.22, 1, .36, 1), SETTLE = bez(.45, 0, .55, 1), LIFT = bez(.25, .1, .25, 1), LOCK = bez(.3, 0, .2, 1);
const track = (keys, t) => { if (t <= keys[0][0]) return keys[0][1];
  for (let i = 0; i < keys.length - 1; i++) { const [a, v0, e] = keys[i], [b, v1] = keys[i + 1]; if (t < b) { const p = (t - a) / (b - a); return v0 + (v1 - v0) * (e ? e(p) : p); } }
  return keys[keys.length - 1][1]; };
const clamp01 = v => Math.max(0, Math.min(1, v));
/* The entrance (Appendix C 1.37, the generator's `enter` timeline, 1300 ms): from black, the mechanism drops from 1.8x
   to its seat about the rings' center through a 1.4% undershoot; the teeth wind in from 150 degrees counterclockwise and
   the plate band from 90 degrees clockwise; the clamps wait 16% wide and 20 degrees back, then close; the halo flares
   as they lock; the sweep, then the status block, arrive; the lettering never moves. */
const ENTER_AT = 120;                      // the first, black frame that builds the document
const ENTER = {
  veil: [[0, 1, LIFT], [240, 0]],
  scale: [[0, 1.8, DROP], [820, .986, SETTLE], [1080, 1]],
  teeth: [[0, -150, DROP], [960, 0]], band: [[0, 90, DROP], [960, 0]],
  clampS: [[0, 1.16], [470, 1.16, LOCK], [740, .985, EASE], [900, 1]], clampR: [[0, -20], [470, -20, LOCK], [740, 0], [900, 0]],
  halo: [[0, 0], [560, 0, EASE], [760, 1, EASE], [1300, .4]], sweep: [[0, 0], [600, 0, EASE], [1050, 1]],
  status: [[0, 0], [640, 0, EASE], [960, 1]], rise: [[0, 6], [640, 6, EASE_OUT], [1000, 0]],
};

/* The layers, bottom to top, as build_retained_initialization.py stacks them. Each draws alone, so the tiles below and
   the screen share one construction. `mech` layers belong to the mechanism, which scales in the entrance. */
const LAYERS = [
  { key: "field", name: "Field", note: "#171A09 over the whole view. The halo is what warms the center (the stock picture rises in soft steps to #1C1F0A by 141 u).",
    draw(g, defs, box, classic) {
      svgEl("rect", { ...box, fill: TONE[26] }, g);
      if (classic) svgEl("circle", { cx: CX, cy: CY, r: 169, fill: radial(defs, 169, [[0, TONE[27]], [.089, TONE[27]], [.225, TONE[28]], [.503, TONE[29]],
        [.71, TONE[30]], [.917, TONE[31]], [1, TONE[31]]]) }, g);
    } },
  { key: "inner", name: "Inner ring", mech: true, note: "169–175.5 u in #1F240C, a solid annulus (the stock one lifts over a soft edge from 161 u).",
    draw(g, defs, box, classic) {
      if (classic) svgEl("path", { d: disc(169) + disc(161), "fill-rule": "evenodd", fill: radial(defs, 169, [[.953, TONE[36], 0], [1, TONE[36], 1]]) }, g);
      ring(g, 169, 175.5, TONE[36]);
    } },
  { key: "band", name: "Plate band", mech: true, spin: "band", note: "175.5–204.5 u. Two framed plates, 90° wide with an 8 u frame, turn counterclockwise once every 180 s.",
    draw(g) {
      ring(g, 175.5, 204.5, TONE[37]);
      const s = svgEl("g", { "data-spin": "band" }, g);
      [0, 180].forEach(c => {
        svgEl("path", { d: sector(175.5, 183.5, c - 45, c + 45), fill: TONE[39] }, s);
        svgEl("path", { d: sector(183.5, 204.5, c - 45, c - 43), fill: TONE[39] }, s);
        svgEl("path", { d: sector(183.5, 204.5, c + 43, c + 45), fill: TONE[39] }, s);
      });
    } },
  { key: "rim", name: "Rim", mech: true, note: "204.5–210.5 u, one step brighter than the band, then the teeth ring's #1A1E0A ground out to 264.8 u.",
    draw(g, defs, box, classic) {
      ring(g, 204.5, 210.5, TONE[38]);
      if (!classic) { ring(g, 210.5, 264.8, TONE[30]); return; }
      ring(g, 210.5, 222, TONE[30]);                                     // the stock rim fades into the teeth ring by 222 u
      svgEl("path", { d: disc(222) + disc(210.5), "fill-rule": "evenodd", fill: radial(defs, 222, [[.948, TONE[37], 1], [.962, TONE[37], .85],
        [.973, TONE[37], .3], [.982, TONE[37], .14], [1, TONE[37], 0]]) }, g);
    } },
  { key: "teeth", name: "Teeth", mech: true, spin: "teeth", note: "204.5–247.5 u. Eight teeth, 22.5° wide on a 45° pitch, stepping through seven tones outward; they turn clockwise once every 120 s.",
    draw(g, defs, box, classic) {
      if (classic) { ring(g, 222, 247, TONE[30]); ring(g, 236.5, 245, TONE[31]); }
      const s = svgEl("g", { "data-spin": "teeth" }, g);
      for (let k = 0; k < 8; k++) {
        const a0 = k * 45 - 11.25, a1 = k * 45 + 11.25;
        [[204.5, 210.5, 39], [210.5, 213.5, 38], [213.5, 216.5, 36], [216.5, 233.5, 35], [233.5, 237, 36], [237, 244, 33], [244, 247.5, 32]]
          .forEach(([r0, r1, tn]) => svgEl("path", { d: sector(r0, r1, a0, a1), fill: TONE[tn] }, s));
      }
    } },
  { key: "grooves", name: "Grooves", mech: true, note: "264.8–300.5 u: a dark groove at 273–283.5 u between two lighter rings.",
    draw(g, defs, box, classic) {
      if (classic) ring(g, 247.5, 264.8, TONE[30]);
      [[264.8, 271.8, 29], [271.8, 273, 28], [273, 283.5, 27], [283.5, 285.5, 28], [285.5, 295.5, 29], [295.5, 297.5, 28],
        [297.5, 300.5, 27]].forEach(([r0, r1, tn]) => ring(g, r0, r1, TONE[tn]));
    } },
  { key: "sweep", name: "Light sweep", mech: true, remastered: true, note: "A warm light turns clockwise round the rings once every 10 s, added in #5E6E24: it rises over its first 7.5° to 0.26 (half that on the grooves), then fades through a 60° tail.",
    draw(g) {
      const s = svgEl("g", { "data-sweep": "1", transform: `rotate(${KEY_LIGHT} ${CX} ${CY})` }, g);
      const amp = a => a < 7.5 ? a / 7.5 : Math.pow(1 - (a - 7.5) / 60, 1.6);
      for (let k = 0; k < 27; k++) {
        const a = (k + .5) * 2.5;
        svgEl("path", { d: sector(161, 250, -(k + 1) * 2.5, -k * 2.5 + .02), fill: "#5E6E24", "fill-opacity": r2(.26 * amp(a)), style: ADD }, s);
        svgEl("path", { d: sector(250, 300, -(k + 1) * 2.5, -k * 2.5 + .02), fill: "#5E6E24", "fill-opacity": r2(.13 * amp(a)), style: ADD }, s);
      }
    } },
  { key: "halo", name: "Halo", mech: true, remastered: true, note: "#FF8340 added behind the emblem at up to 0.10, gone by 168 u, breathing between 0.40 and 1.00 of that over 6 s.",
    draw(g, defs) {
      svgEl("circle", { "data-breathe": "1", cx: CX, cy: CY, r: 168, style: ADD, fill: radial(defs, 168, [[0, GLOW, .1], [.446, GLOW, .07], [.798, GLOW, .02], [1, GLOW, 0]]) }, g);
    } },
  { key: "clamps", name: "Clamps", mech: true, note: "Four fixed clamps on the diagonals, 8° wide from 174 to 211 u, in the field's tone with a #1C200B leading edge. In the entrance they close on the plates. (The stock picture has only shade at 3 and 9 o'clock.)",
    draw(g, defs, box, classic) {
      if (classic) {                                                     // the stock: shade 80 u tall from 183 u, darker over the grooves
        [[183, 272, 174.5, 80, .03], [246, 272, 177, 75, .035]].forEach(([r0, r1, y, h, o]) => [1, -1].forEach(side =>
          svgEl("rect", { x: side > 0 ? CX + r0 : CX - r1, y, width: r1 - r0, height: h, fill: "#000", "fill-opacity": o }, g)));
        return;
      }
      const c = svgEl("g", { "data-clamps": "1" }, g);
      for (let n = 0; n < 4; n++) {
        svgEl("path", { d: sector(174, 211, n * 90 + 41, n * 90 + 49), fill: TONE[26] }, c);
        svgEl("path", { d: sector(174, 211, n * 90 + 41, n * 90 + 42), fill: TONE[32] }, c);
      }
    } },
  { key: "embers", name: "Embers", mech: true, remastered: true, note: "Fourteen sparks of 0.6–1.2 u in #FFB070 rise 110–201 u through the rings, each over 6–11 s, at most 0.50.",
    draw(g) {
      const m = svgEl("g", { "data-embers": "1" }, g);
      MOTES.forEach(([x, y, r, rise, dur]) => svgEl("path", { d: `M${x} ${r2(y - r)}l${r} ${r}l${-r} ${r}l${-r} ${-r}Z`, fill: "#FFB070", "fill-opacity": .4, style: ADD, "data-m": [rise, dur].join(",") }, m));
    } },
  { key: "emblem", name: "Emblem", mech: true, note: "The traced q4logo in #0E1000, in its 260 u window at 190,125 u, 15 u lower than the stock picture's, which is centered on the canvas.",
    draw(g, defs, box, classic) { if (A.EMBLEM) svgEl("path", { d: A.EMBLEM, fill: INK }, svgEl("g", { transform: classic ? "translate(190.07 110.19) scale(1.0015)" : "translate(190 125)" }, g)); } },
  { key: "glow", name: "Glow", lit: true, view: "150 160 340 72", note: "The letters filled and stroked in #FF8340, added three times: 12 u wide at 0.016, 8.7 u at 0.027 and 5.3 u at 0.042.",
    draw(g) {
      const w = svgEl("g", { transform: WORDMARK }, g);
      [[12, .016], [8.667, .027], [5.333, .042]].forEach(([sw, a]) => Object.values(GLYPHS).forEach(d =>
        svgEl("path", { d, fill: GLOW, "fill-opacity": a, stroke: GLOW, "stroke-opacity": a, "stroke-width": r2(sw / .3667), "stroke-linejoin": "round", "fill-rule": "evenodd", style: ADD }, w)));
    } },
  { key: "lettering", name: "Lettering", lit: true, view: "150 160 340 72", note: "QUAKE 4 in the q4text logotype, #F2F0EC, cap height 26 u. It never moves.",
    draw(g) {
      const w = svgEl("g", { transform: WORDMARK }, g);
      Object.values(GLYPHS).forEach(d => svgEl("path", { d, fill: LETTER, "fill-rule": "evenodd" }, w));
    } },
  { key: "glint", name: "Glint", lit: true, remastered: true, view: "150 160 340 72", note: "A vertical white light, 38 u wide at up to 0.50, crosses QUAKE 4 left to right in 900 ms every 9 s, clipped to the letters.",
    draw(g, defs, box, classic, still) {
      const cid = id("gc"), cp = svgEl("clipPath", { id: cid }, defs);
      Object.values(GLYPHS).forEach(d => svgEl("path", { d, transform: WORDMARK, "fill-rule": "evenodd" }, cp));
      const lg = id("gb"), grad = svgEl("linearGradient", { id: lg, x1: 0, x2: 1, y1: 0, y2: 0 }, defs);
      [[0, 0], [.47, 0], [.5, .5], [.53, 0], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#FFFFFF", "stop-opacity": a }, grad));
      svgEl("rect", { "data-glint": "1", x: still ? 0 : -640, y: 0, width: 640, height: 480, fill: `url(#${lg})` }, svgEl("g", { "clip-path": `url(#${cid})` }, g));
    } },
  { key: "status", name: "Status", lit: true, view: "215 395 210 48", note: "Marine 16 dp in #F09E0D, its middle at 412 u, over a rule of seven 21 u segments at 437 u: done 0.55, pending 0.15, and a marker pulsing 0.62–0.95.",
    draw(g, defs, box, classic) { return statusLine(g, classic); } },
];

function statusLine(g, classic) {
  if (classic) return { classic: true, text: svgEl("text", { x: 320, y: 422.6, "text-anchor": "middle", "font-family": F_P, "font-weight": 600, "font-size": 13, "letter-spacing": .2, fill: STATUS }, g), shown: "" };
  const block = svgEl("g", {}, g);
  const text = svgEl("text", { x: 320, y: 414.8, "text-anchor": "middle", "font-family": F_M, "font-size": 8.2, "letter-spacing": 1.35, fill: STATUS }, block);
  const seg = (i, a) => svgEl("rect", { x: 236 + i * 24, y: 437, width: 21, height: 1, fill: STATUS, "fill-opacity": a }, block);
  const pend = [], done = [];
  for (let i = 0; i < NSTEP; i++) { pend.push(seg(i, .15)); done.push(seg(i, .55)); done[i].setAttribute("opacity", 0); }
  const marker = seg(0, 1); marker.setAttribute("opacity", .62);
  return { block, text, pend, done, marker, shown: "", seg: -1, fill: [], arrive: -1e9, mFrom: 0, mTo: 0, mAt: -1e9 };
}
/* Shows step `i` of STEPS at `now`: a new message rises in; the segments up to its own fade in to done one after
   another, 70 ms apart; the marker glides there over 440 ms. Reduced motion changes them at once, without the marker. */
function showStep(s, i, now) {
  const [msg, seg] = STEPS[i];
  if (s.classic) { s.text.textContent = msg; return; }
  if (msg !== s.shown) { s.shown = msg; s.text.textContent = msg; s.arrive = now; }
  if (seg !== s.seg) {
    for (let k = s.seg + 1; k <= seg; k++) s.fill[k] = now + 70 * (k - s.seg - 1);
    s.mFrom = markerX(s, now); s.mTo = seg * 24; s.mAt = now; s.seg = seg;
  }
}
const markerX = (s, now) => s.mFrom + (s.mTo - s.mFrom) * EASE(clamp01((now - s.mAt) / 440));
function drawStatus(s, now, motion) {
  if (s.classic) return;
  const a = motion ? clamp01((now - s.arrive) / 280) : 1, rise = motion ? 5 * (1 - EASE_OUT(clamp01((now - s.arrive) / 320))) : 0;
  s.text.setAttribute("opacity", r2(motion ? EASE(a) : 1)); s.text.setAttribute("transform", `translate(0 ${r2(rise)})`);
  s.done.forEach((d, k) => d.setAttribute("opacity", k > s.seg ? 0 : motion ? r2(EASE(clamp01((now - s.fill[k]) / 300))) : 1));
  s.marker.setAttribute("display", motion ? "inline" : "none");
  if (motion) { const p = ((now - s.mAt) % 800) / 800;
    s.marker.setAttribute("transform", `translate(${r2(markerX(s, now))} 0)`); s.marker.setAttribute("opacity", r2(.62 + .33 * (p < .5 ? 2 * p : 2 - 2 * p))); }
}

/* One clock for the slow turns: angle = base + (now - ref) x speed, so a speed change never jumps. */
const clock = { base: 0, ref: performance.now(), speed: 1 };
const turning = now => reduce ? 0 : clock.base + (now - clock.ref) * clock.speed;
const spinTo = (root, t, off = { band: 0, teeth: 0 }) => root.querySelectorAll("[data-spin]").forEach(s => {
  const turn = s.dataset.spin === "band" ? -t / BAND_TURN : t / TEETH_TURN;
  s.setAttribute("transform", `rotate(${r2((turn % 1) * 360 + off[s.dataset.spin])} ${CX} ${CY})`);
});

/* The screen. */
let status = null, seq0 = performance.now(), last = 0, ui = {};
function build() {
  svg.innerHTML = "";
  const lay = hud.fit(svg), classic = hud.classic, defs = svgEl("defs", {}, svg);
  const scene = svgEl("g", {}, svg);
  if (document.getElementById("inReveal")?.getAttribute("aria-pressed") === "true") scene.setAttribute("filter", "url(#inRevealF)");
  if (classic) svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: CLEAR }, scene);
  const box = classic ? { x: 0, y: 0, width: 640, height: 480 } : { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H };
  const pic = svgEl("g", classic ? { "clip-path": `url(#${clip(defs)})` } : {}, scene);
  let mech = null; const lit = svgEl("g", {}, svg);
  LAYERS.forEach(L => { if (L.remastered && classic) return;             // Classic keeps the stock picture
    if (L.mech && !mech) mech = svgEl("g", {}, pic);                     // over the field
    const host = L.lit ? lit : L.mech ? mech : pic;
    const out = L.draw(svgEl("g", { "data-layer": L.key }, host), defs, box, classic); if (L.key === "status") status = out; });
  // the first frame is black; the veil lifts over the entrance's first 240 ms (Classic and reduced motion have none)
  const veil = classic || reduce ? null : svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: "#000" }, svg);
  ui = { mech, veil, lit, clamps: svg.querySelector("[data-clamps]"), statusBlock: status.block };
  hud.regionsLayer(svg, lay);
  last = 0; update(performance.now());
  document.getElementById("inStatus").textContent = classic
    ? "Classic: the stock picture on the 4:3 canvas, pillarboxed in #181A08 by openQ4's aspect correction, with the console-font status line; nothing moves."
    : reduce ? "Remastered with reduced motion: the screen is at rest from the first frame, the sweep held as a key light, and each step changes at once."
      : "Remastered: the entrance from black, then the band and the teeth turn in opposite directions under the fixed lettering.";
}
function clip(defs) {
  const cid = id("cl"); svgEl("rect", { x: 0, y: 0, width: 640, height: 480 }, svgEl("clipPath", { id: cid }, defs)); return cid;
}
const about = (s, r = 0) => `translate(${CX} ${CY}) rotate(${r2(r)}) scale(${s.toFixed(4)}) translate(${-CX} ${-CY})`;
function update(now) {
  const classic = hud.classic, motion = !classic && !reduce, e = now - seq0 - ENTER_AT;   // ms into the entrance
  const at = (keys, rest) => !motion ? rest : track(keys, Math.max(0, e));
  if (!classic) {
    ui.mech.setAttribute("transform", about(at(ENTER.scale, 1)));
    ui.clamps?.setAttribute("transform", about(at(ENTER.clampS, 1), at(ENTER.clampR, 0)));
    if (ui.veil) ui.veil.setAttribute("opacity", r2(e < 0 ? 1 : track(ENTER.veil, e)));
    ui.statusBlock?.setAttribute("opacity", r2(at(ENTER.status, 1)));
    ui.statusBlock?.setAttribute("transform", `translate(0 ${r2(at(ENTER.rise, 0))})`);
  }
  const ent = e < 1300 && motion;
  if (ent || now - last >= 33) {                                  // 30 updates a second are plenty once it has settled
    last = now;
    spinTo(svg, classic ? 0 : turning(now), { band: at(ENTER.band, 0), teeth: at(ENTER.teeth, 0) });
    if (!classic) accents(svg, now, Math.max(0, e));
  }
  if (!status) return;
  let t = now - seq0, i = 0;
  while (i < STEPS.length - 1 && t >= STEPS[i][2]) t -= STEPS[i++][2];   // the sequence then holds on its last step
  showStep(status, i, now); drawStatus(status, now, motion);
}
function accents(root, now, e) {
  const motion = !reduce;
  root.querySelectorAll("[data-sweep]").forEach(s => {
    s.setAttribute("transform", `rotate(${motion ? r2(KEY_LIGHT + (e % SWEEP_TURN) / SWEEP_TURN * 360) : KEY_LIGHT} ${CX} ${CY})`);
    if (s.closest("#inSvg")) s.setAttribute("opacity", motion ? r2(track(ENTER.sweep, e)) : 1); });
  root.querySelectorAll("[data-breathe]").forEach(h => { const p = ((e - 1300) % BREATHE) / BREATHE;
    h.setAttribute("opacity", !motion ? .8 : e < 1300 ? r2(track(ENTER.halo, e)) : r2(.4 + .6 * (p < .5 ? 2 * p : 2 - 2 * p))); });
  root.querySelectorAll("[data-glint]").forEach(r => { const p = (e % GLINT_EVERY - 940) / 900;
    r.setAttribute("display", motion ? "inline" : "none"); r.setAttribute("x", r2(-640 + 1280 * clamp01(p))); });
  root.querySelectorAll("[data-m]").forEach(c => { const [rise, dur] = c.dataset.m.split(",").map(Number), p = (e % dur) / dur;
    c.setAttribute("transform", `translate(0 ${r2(-rise * p)})`);
    c.setAttribute("fill-opacity", motion ? r2(track([[0, 0], [.25, .3], [.75, .5], [1, 0]], p)) : 0); });
}

/* The layer tiles: each layer alone, in place on the Remastered 16:9 view, at rest. */
const host = document.getElementById("inLayers");
const tiles = LAYERS.map((L, n) => {
  const fig = document.createElement("figure");
  const t = svgEl("svg", { viewBox: L.view || "19 -85.5 600 600", role: "img", "aria-label": `${L.name} layer` }, fig);
  const defs = svgEl("defs", {}, t), g = svgEl("g", L.lit ? {} : { filter: "url(#inRevealT)" }, t);
  if (L.key !== "field") svgEl("rect", { x: -107, y: -90, width: 854, height: 660, fill: TONE[26] }, g);
  const out = L.draw(svgEl("g", {}, g), defs, { x: -107, y: -90, width: 854, height: 660 }, false, true);
  if (out && out.text) { out.text.textContent = "INITIALIZING SOUND..."; out.done.forEach((d, k) => d.setAttribute("opacity", k <= 1 ? 1 : 0)); out.marker.setAttribute("transform", "translate(24 0)"); }
  t.querySelectorAll("[data-m]").forEach(c => c.setAttribute("fill-opacity", .45));
  t.querySelectorAll("[data-breathe]").forEach(c => c.setAttribute("opacity", .8));
  const cap = document.createElement("figcaption");
  cap.innerHTML = `<b>${String(n + 1).padStart(2, "0")} · ${L.name}</b>${L.note}`;
  fig.appendChild(cap); host?.appendChild(fig);
  return t;
});
tiles.forEach(t => { if (t.querySelector("[data-spin]")) { let tl = 0; A.tick(t, now => { if (now - tl >= 33) { tl = now; spinTo(t, turning(now)); } }); } });

build();
hud.on(build);
A.tick(svg, update);
const press = (b, on) => b.setAttribute("aria-pressed", on);
const fast = document.getElementById("inFast");
if (reduce && fast) { fast.disabled = true; fast.title = "Reduced motion holds the rings still"; }
fast?.addEventListener("click", () => {
  const now = performance.now(), on = fast.getAttribute("aria-pressed") !== "true";
  clock.base = clock.base + (now - clock.ref) * clock.speed; clock.ref = now; clock.speed = on ? 8 : 1; press(fast, on);
});
document.getElementById("inReveal")?.addEventListener("click", e => { press(e.currentTarget, e.currentTarget.getAttribute("aria-pressed") !== "true"); build(); });
document.getElementById("inReplay")?.addEventListener("click", () => { clock.base = 0; clock.ref = performance.now(); seq0 = performance.now(); build(); });
const ctl = document.getElementById("inLayoutCtl"); if (ctl) hud.controls(ctl, { scale: false });
})();
