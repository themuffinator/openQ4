
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_L = "Archivo, sans-serif";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
let uid = 0; const id = p => p + (++uid);

/* One frame loop for every live instrument (EKG, transmission traces, meters), paused while its drawing is off screen. */
const ticks = new Set(), onScreen = new WeakMap();
const io = "IntersectionObserver" in window ? new IntersectionObserver(es => es.forEach(e => onScreen.set(e.target, e.isIntersecting))) : null;
let running = false;
const loop = now => {
  // One sheet's failing instrument must not stop every other sheet's: drop it and report it once.
  for (const t of ticks) {
    if (!t.el.isConnected) { ticks.delete(t); continue; }
    if (onScreen.get(t.root) === false) continue;
    try { t.fn(now); } catch (error) { ticks.delete(t); console.error(error); }
  }
  running = ticks.size > 0; if (running) requestAnimationFrame(loop);
};
A.tick = (el, fn) => {
  const root = el.ownerSVGElement || el;
  if (io && !onScreen.has(root)) { onScreen.set(root, true); io.observe(root); }
  ticks.add({ el, fn, root }); if (!running) { running = true; requestAnimationFrame(loop); }
};

/* Measured Marine gauge geometry, gauge-local u inside the 125x59 rect (section 6, B.5). */
const BACK = "M1 10.4Q1 9.2 2.2 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H2.2Q1 49.3 1 48.1Z";
const FILL = "M5.4 14.8Q5.4 13.8 6.4 13.8H118.6Q119.6 13.8 119.6 14.8V34.6H105.4A16.6 16.6 0 0 0 93.6 44.7H6.4Q5.4 44.7 5.4 43.7Z";
const DIVIDERS = [15.6, 27.3, 39.1, 50.8, 62.5, 74.2, 85.9, 97.7, 108.9];

/* EKG: one beat of ekg.tga (256x128 drawn at 125x59, two beats per width), traced along its core. Baseline on texel
   rows 74-75; P rises 3.46 u, Q dips 2.5 u, R rises 17.5 u, S falls 6.2 u, T and U rise 6.22 u and 3.0 u, all with
   straight flanks (T has a short flat crest). The core is 0.95 u; the halo falls from 0.24 to nothing over 3.7 u. */
const EKG_B = 34.57;
const EKG_BEAT = o => {
  const b = EKG_B, r = v => Math.round(v * 100) / 100;
  return `M${r(o)} ${b}H${r(o + 11.2)}L${r(o + 14.9)} ${r(b - 3.46)}L${r(o + 18.6)} ${b}H${r(o + 22.7)}L${r(o + 23.7)} ${r(b + 2.5)}` +
    `L${r(o + 25.8)} ${r(b - 17.5)}L${r(o + 27.7)} ${r(b + 6.2)}L${r(o + 29.2)} ${b}H${r(o + 37.4)}L${r(o + 43.5)} ${r(b - 6.22)}` +
    `H${r(o + 44.7)}L${r(o + 49.6)} ${b}H${r(o + 51.8)}L${r(o + 55.2)} ${r(b - 3)}L${r(o + 58.6)} ${b}H${r(o + 62.5)}`;
};
A.EKG_BEAT = EKG_BEAT; A.EKG_B = EKG_B;
/* Added through the fill's outline beneath the fill, travelling toward the trailing edge at 25 u/s (one beat per
   2.5 s). An instrument, so it keeps moving under reduced motion. */
A.ekgTrace = (parent, clipId, { color = "#161B0E", gain = 1 } = {}) => {
  const fid = id("eg");
  const f = svgEl("filter", { id: fid, x: "-10%", y: "-60%", width: "120%", height: "220%" }, svgEl("defs", {}, parent));
  svgEl("feGaussianBlur", { stdDeviation: 1.25 }, f);
  const g = svgEl("g", { "clip-path": `url(#${clipId})`, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter", opacity: gain }, parent);
  const mover = svgEl("g", {}, g), d = [-62.5, 0, 62.5, 125].map(EKG_BEAT).join("");
  const halo = svgEl("path", { d, fill: "none", stroke: color, "stroke-width": 2.6, "stroke-opacity": .55, "stroke-linejoin": "round", filter: `url(#${fid})` }, mover);
  const core = svgEl("path", { d, fill: "none", stroke: color, "stroke-width": .95, "stroke-linejoin": "round", "stroke-linecap": "round" }, mover);
  svgEl("animateTransform", { attributeName: "transform", type: "translate", from: "-62.5 0", to: "0 0", dur: "2.5s", repeatCount: "indefinite" }, mover);
  return { g, setColor: c => { halo.setAttribute("stroke", c); core.setAttribute("stroke", c); } };
};

/* The weapon's shaded render (its mtr_icon, gun_*) sits upright behind the clip count at #C7BD78, 0.40, where the stock
   draws the right-aligned 256x64 texture into the 106x26 u current_gun window: x = texel origin x 106/256, scale 26/24.
   Single player only. */
const GUN_X0 = { blaster: 141, machinegun: 124, shotgun: 29, hyperblaster: 70, grenadelauncher: 82, nailgun: 128, rocketlauncher: 92,
  railgun: 93, lightninggun: 82, darkmattergun: 95, napalmgun: 125, gauntlet: 113 };
const GUN_ART = { darkmattergun: "gun_darkmatter" };
function marineGauge(parent, gx, kind, opts = {}) {
  const color = opts.color || "#B3D06E", gy = opts.y ?? 420;
  const root = svgEl("g", { transform: `translate(${gx} ${gy})` }, parent);
  const sh = svgEl("g", { transform: "matrix(1 0 -0.22 1 6.49 0)" }, root);   // shear 0.22 about the gauge center (y 29.5)
  const clipF = id("gf"), clipV = id("gv"), clipR = id("gr");
  svgEl("path", { d: FILL }, svgEl("clipPath", { id: clipF }, sh));
  const vis = svgEl("rect", { x: 5.4, y: 0, width: 114.2, height: 59 }, svgEl("clipPath", { id: clipV }, sh));
  svgEl("path", { d: BACK }, svgEl("clipPath", { id: clipR }, sh));
  svgEl("path", { d: BACK, fill: "#000", "fill-opacity": .5 }, sh);
  const rim = svgEl("path", { d: BACK, fill: "none", stroke: "#FF0000", "stroke-width": 9, "clip-path": `url(#${clipR})`, opacity: 0, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, sh);
  const track = svgEl("g", { "clip-path": `url(#${clipF})` }, sh);
  const trackFill = svgEl("path", { d: FILL, fill: color, "fill-opacity": .2 }, track);
  const ekg = kind === "health" ? A.ekgTrace(sh, clipF) : null;
  const fillG = svgEl("g", { "clip-path": `url(#${clipV})` }, sh);
  const fillInner = svgEl("g", { "clip-path": `url(#${clipF})` }, fillG);
  const fill = svgEl("path", { d: FILL, fill: color, "fill-opacity": .35 }, fillInner);
  const bevel = svgEl("path", { d: FILL, fill: "none", stroke: color, "stroke-opacity": .45, "stroke-width": 6 }, fillInner);
  if (kind === "armor") DIVIDERS.forEach(x => svgEl("rect", { x: x - .75, y: 13.8, width: 1.5, height: 31, fill: "#000", "fill-opacity": .28 }, fillInner));
  const gunG = kind === "ammo" ? svgEl("g", { transform: "matrix(1 0 0.22 1 -6.49 0)" }, sh) : null;   // upright inside the sheared plate
  const num = svgEl("text", { x: kind === "ammo" ? 80 : 118, y: 32.7, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 21.6, fill: "#DDEBC3" }, sh);
  num.style.transformBox = "fill-box"; num.style.transformOrigin = "100% 70%";
  const reserve = kind === "ammo" ? svgEl("text", { x: 118, y: 31, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 13.8, fill: "#DDEBC3" }, sh) : null;
  const icon = svgEl("g", {}, root);
  const drawIcon = name => { icon.innerHTML = ""; A.icon(icon, name, { x: 100, y: 38, h: 16, color: "#A8A360", opacity: .6 }); };
  drawIcon(opts.icon || kind);
  const setFill = pct => { const left = Math.max(5.4, 120 - 115 * Math.max(0, Math.min(1, pct))); vis.setAttribute("x", left); vis.setAttribute("width", 119.6 - left); };
  const set = (value, pct) => { num.textContent = value; setFill(pct); };
  /* The ammunition readout for a weapon { key, clip, loaded, total, max, infinite } (stock UpdateHudAmmo): a clip of two
     or more shows the clip count large and the reserve small, the fill on the clip; otherwise the total alone at the
     trailing edge against the maximum; unlimited weapons show no number and a full fill. Multiplayer always shows the
     total and has no weapon render. The notch icon is the weapon's own glyph. */
  const weapon = w => {
    if (gunG) { gunG.innerHTML = ""; const x0 = GUN_X0[w.key];
      if (!opts.mp && x0 != null) A.icon(gunG, GUN_ART[w.key] || `gun_${w.key}`, { x: -2 + x0 * 106 / 256, y: 15, h: 26, color: "#C7BD78", opacity: .4 }); }
    drawIcon(w.key);
    if (w.infinite) { num.textContent = ""; if (reserve) reserve.textContent = ""; setFill(1); return; }
    const clip = !opts.mp && w.clip >= 2;
    num.setAttribute("x", clip ? 80 : 118); num.textContent = clip ? w.loaded : w.total;
    if (reserve) reserve.textContent = clip ? w.total - w.loaded : "";
    setFill(clip ? w.loaded / w.clip : w.total / w.max);
  };
  return { root, num, icon, fill, trackFill, bevel, rim, ekg, set, weapon };
}
A.marineGauge = marineGauge;

/* Transmission waveform: waveform.tga (256x128) rebuilt column by column. A 4-texel core on rows 64-67 runs the full
   width; each column adds its recorded speech spike above and below the core. The ink digits bucket each column's
   brightness (thin grey spikes draw dimmer). */
const WAVE_UP = "07>BDG9@;:;3LWP<<83120110008?QS29WPEXYP31>THPB9EAA3OPG12UXM14000000265@?165:0=>=70FD44?>379;=;;>;?6971=;<;DVQ>889914=24B90743000000HH;EC;?8;911XSF?>LE30ERUUXB;6100>>>300000000000000054007;;674003?76177362:>=00423400000000NW84009E<2269:73456:<<<5B>B@6200000";
const WAVE_DN = "03<?BE9B6753MOB58641011000049AE15OE?RH@208KBCB8=@@3IF@01OMD14100000044;;15570:9760BB3398755287:=;94750;;76=D>;872414724>40543000001<L:<A9;57413JM=7C=<20CTSLR<5200068<30000000000000001000556132003;520442506<900102300000000FG30009;:113674223556133:7@97200000";
const WAVE_INK = "9399989877488878896644639997979977998988358997886898986789987399999477744988979899878989778399798868778999788988794888988989799999589998688986588969967949987997499688799999999999999996998897769968994967833989988699999999958879969868778898997545879789399999";
const WAVE_OP = [1, .78, .55];
const WAVE = (() => {
  const up = ["", "", ""], dn = ["", "", ""];
  for (let x = 0; x < 256; x++) {
    const u = WAVE_UP.charCodeAt(x) - 48, d = WAVE_DN.charCodeAt(x) - 48, k = +WAVE_INK[x], b = k >= 8 ? 0 : k >= 6 ? 1 : 2;
    if (u > 0) up[b] += `M${x} ${64 - u}h1V64h-1Z`;
    if (d > 0) dn[b] += `M${x} 68h1V${68 + d}h-1Z`;
  }
  return { up, dn };
})();
/* wavTable (tables.mtr:21): 21 entries cycled every 1.667 s (79.4 ms each), interpolated. The value scales texture
   coordinates about the center, so the drawn height is 1/k: 0.71 to 2.5 times. */
A.WAV_TABLE = [0.95, 0.4, 1, 0.5, 0.8, 1, 0.6, 1.4, 0.95, 0.6, 0.75, 1.2, 0.7, 1.3, 0.6, 0.8, 0.95, 0.6, 1, 0.7, 0.95];
A.wavK = now => { const f = ((now / 1000 * .6) % 1 + 1) % 1 * 21, i = Math.floor(f), a = A.WAV_TABLE[i], b = A.WAV_TABLE[(i + 1) % 21]; return a + (b - a) * (f - i); };
/* Playback controls below provide the Remastered drive. */
A.voiceLevel = () => 0;
/* The trace in a rect where the stock draws the 256x128 texture, masked by `clip` and added in `color`.
   mode "table": the whole texture scales about its center by 1/k (stock centerscale); mode "voice": the core stays
   and only the spikes follow the level, so the trace lies flat between words. */
A.waveform = (parent, { rect, clip, color = "#FF8000", mode = "table", speed = .6, still = false, offset = 0 }) => {
  const cid = id("wv"), [x, y, w, h] = rect;
  svgEl("path", { d: clip }, svgEl("clipPath", { id: cid }, parent));
  const g = svgEl("g", { "clip-path": `url(#${cid})` }, parent);
  const add = svgEl("g", { style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
  const tex = svgEl("g", { transform: `translate(${x} ${y}) scale(${w / 256} ${h / 128})` }, add);
  const all = svgEl("g", {}, tex), scroll = svgEl("g", {}, all);
  const upG = svgEl("g", {}, scroll), dnG = svgEl("g", {}, scroll);
  [0, 256].forEach(ox => {
    svgEl("rect", { x: ox, y: 64, width: 256, height: 4, fill: color }, scroll);
    WAVE.up.forEach((d, i) => d && svgEl("path", { d, fill: color, "fill-opacity": WAVE_OP[i], transform: `translate(${ox} 0)` }, upG));
    WAVE.dn.forEach((d, i) => d && svgEl("path", { d, fill: color, "fill-opacity": WAVE_OP[i], transform: `translate(${ox} 0)` }, dnG));
  });
  const level = now => mode === "voice" ? .05 + 2.3 * A.voiceLevel(now) : 1 / A.wavK(now);
  const set = t => {
    const now = t + offset, s = level(now).toFixed(3);
    if (mode === "voice") { all.removeAttribute("transform"); upG.setAttribute("transform", `translate(0 64) scale(1 ${s}) translate(0 -64)`); dnG.setAttribute("transform", `translate(0 68) scale(1 ${s}) translate(0 -68)`); }
    else { all.setAttribute("transform", `translate(0 64) scale(1 ${s}) translate(0 -64)`); upG.removeAttribute("transform"); dnG.removeAttribute("transform"); }
    scroll.setAttribute("transform", `translate(${(-(now / 1000 * speed * 256) % 256).toFixed(2)} 0)`);
  };
  set(0);
  if (!(still || reduce)) A.tick(g, set);
  return { g, set, level };
};

// openq4-playback-graphs-start
