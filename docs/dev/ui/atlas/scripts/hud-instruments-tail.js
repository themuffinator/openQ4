
/* The Rhino squad emblem (radio_rhino.tga, 64x64 alpha), traced. */
const RHINO = "M4.5 1.1 3.0 2.8 3.1 61.4 4.9 62.9 57.4 62.8 58.9 61.1 58.8 2.5 57.0 1.0ZM41.2 48.6 42.2 48.5 43.0 49.0 43.1 50.0 42.6 50.6 42.0 50.6 41.1 50.0 40.9 49.1ZM34.4 36.2 35.1 36.0 36.5 36.8 37.8 38.2 37.8 38.9 37.1 39.1 35.5 38.0 34.4 36.6ZM20.6 32.8 20.5 33.6 19.5 34.2 14.0 34.0 12.6 33.5 12.9 33.1 14.0 32.9 18.6 32.6 20.2 32.4ZM6.2 4.8 6.9 4.6 11.9 5.9 16.2 7.5 16.4 7.2 15.9 6.4 13.8 4.6 13.8 4.2 14.2 3.9 18.5 4.0 25.0 9.1 28.2 12.0 29.1 13.1 30.4 14.0 30.5 13.4 29.0 10.2 28.6 8.9 28.0 7.9 27.8 6.8 26.6 4.8 26.8 4.1 27.2 3.9 30.9 4.0 31.9 5.0 33.9 8.0 34.9 9.9 35.2 11.1 35.5 11.4 36.0 11.4 36.1 9.5 35.8 5.5 35.5 5.0 35.6 4.2 36.2 3.9 42.4 4.0 44.8 7.6 45.4 9.1 46.9 11.6 47.1 12.6 47.9 13.8 48.1 14.8 49.9 18.5 50.4 20.2 50.8 20.8 51.0 22.0 51.8 23.8 52.2 26.1 52.6 26.6 53.1 26.8 53.2 22.2 52.6 15.0 51.6 9.5 50.6 5.8 50.0 4.8 50.0 4.4 50.5 3.9 55.5 3.9 56.0 4.4 56.0 59.5 55.8 59.9 38.4 60.0 37.6 59.6 36.9 59.6 35.9 60.0 25.2 60.0 24.6 59.6 24.8 59.1 27.0 58.4 30.8 56.6 31.5 56.0 32.6 55.6 32.8 55.2 32.2 55.0 30.6 55.1 27.1 55.9 17.4 56.9 6.5 57.4 6.0 56.9 5.9 50.1 6.1 49.5 11.6 45.1 13.8 44.1 15.6 43.6 17.8 42.0 22.4 41.9 26.9 47.2 28.5 48.0 30.6 49.8 33.8 51.5 36.9 51.9 38.0 52.8 38.8 52.9 39.5 52.2 39.1 51.5 39.5 51.1 40.6 52.5 40.8 53.1 41.1 53.5 41.9 53.5 43.1 53.0 44.8 51.2 45.8 49.2 46.4 46.8 46.8 46.1 48.9 44.1 51.6 42.0 53.8 39.4 54.5 37.4 54.8 35.9 54.6 35.2 54.1 35.0 53.5 35.2 51.4 37.0 49.6 37.9 47.4 38.5 46.4 38.5 44.6 37.5 44.6 36.8 45.5 33.6 45.1 32.8 44.0 32.6 40.8 33.8 40.1 33.5 38.9 32.4 38.4 31.4 38.4 28.2 38.0 27.0 39.9 24.4 40.0 22.5 39.8 21.5 39.2 20.9 38.5 20.9 36.6 23.0 35.8 23.4 35.2 23.2 34.9 22.6 35.0 22.2 36.6 20.8 37.6 18.8 37.8 17.1 37.2 15.9 36.4 15.8 35.5 16.5 33.9 17.1 32.2 18.6 31.9 19.5 30.8 20.5 30.6 21.4 30.4 21.5 30.1 21.2 30.2 20.2 29.9 19.0 27.0 16.0 23.5 13.2 20.1 11.2 15.2 9.1 9.1 7.2 6.6 6.9 5.9 6.0 5.9 5.2Z";
A.RHINO = RHINO;

/* Radio chatter: the rounded bar, INCOMING / TRANSMISSION and the masked additive waveform (stock hud.gui:1683;
   group x 468-633, y 5-52). With `rhino` the squad emblem and its plate shrink into place over 500 ms; `emblem` swaps
   in another squad's stencil (64-unit box, white), such as The Awakening's Falcon. */
A.radioChatter = (parent, { strogg = false, rhino = false, mode, emblem = null } = {}) => {
  const g = svgEl("g", {}, parent);
  const shear = (cy, k) => `matrix(1 0 ${k} 1 ${-k * cy} 0)`;
  const K = strogg ? .22 : -.22;
  if (rhino) {
    const plate = svgEl("rect", { rx: 1.8, fill: "#000", "fill-opacity": .4 }, g);
    const em = svgEl("svg", { viewBox: "0 0 64 64", preserveAspectRatio: "none", overflow: "visible" }, svgEl("g", {}, g));
    if (emblem) em.innerHTML = A.uniqueIds(emblem.replace(/#FFFFFF/g, strogg ? "#FFCC00" : "#FFFF00"));   // another squad's stencil
    else svgEl("path", { d: RHINO, fill: strogg ? "#FFCC00" : "#FFFF00", "fill-rule": "evenodd" }, em);
    const P0 = strogg ? [474, 6, 46, 47] : [468, 5, 46, 47], P1 = strogg ? [492, 7, 26, 26] : [493, 6, 26, 26];
    const E0 = strogg ? [477, 10, 42, 39] : [471, 9, 42, 39], E1 = strogg ? [494, 9, 22, 22] : [495, 8, 22, 22];
    const lerp = (a, b, p) => a.map((v, i) => v + (b[i] - v) * p);
    const place = p => {
      const [px, py, pw, ph] = lerp(P0, P1, p), [ex, ey, ew, eh] = lerp(E0, E1, p), ins = .031;
      Object.entries({ x: px + pw * ins, y: py + ph * ins, width: pw * (1 - 2 * ins), height: ph * (1 - 2 * ins) }).forEach(([k, v]) => plate.setAttribute(k, v));
      plate.setAttribute("transform", shear(py + ph / 2, K));
      Object.entries({ x: ex, y: ey, width: ew, height: eh }).forEach(([k, v]) => em.setAttribute(k, v));
      em.parentNode.setAttribute("transform", shear(ey + eh / 2, K));
    };
    const t0 = performance.now();
    if (reduce) place(1);
    else { place(0); const step = now => { const p = Math.min(1, Math.max(0, now - t0) / 500); place(p); if (p < 1) requestAnimationFrame(step); }; requestAnimationFrame(step); }
  }
  svgEl("rect", { x: strogg ? 522.8 : 521.8, y: 6.8, width: 109.4, height: 24.4, rx: 1.5, fill: "#000", "fill-opacity": .498, transform: shear(19, K) }, g);
  A.waveform(g, strogg
    ? { rect: [515, 7, 41, 25], clip: "M522 8.56H544.5L548.3 30.44H525.9Z", color: "#FFCC00", mode: mode || (hud.classic ? "table" : "voice") }
    : { rect: [513, 7, 41, 25], clip: "M526.45 8.56H545.67V30.44H522.61Z", color: "#FF8000", mode: mode || (hud.classic ? "table" : "voice") });
  const lab = strogg ? { "font-family": F_C, "font-weight": 600, "font-size": 7.6, "letter-spacing": .6, transform: "skewX(-11.4)" } : { "font-family": F_M, "font-size": 6.7 };
  const lx = (x, y) => strogg ? x + y * Math.tan(11.4 * Math.PI / 180) : x;
  svgEl("text", { ...lab, x: lx(547, 19), y: 19, fill: "#fff" }, g).textContent = "INCOMING";
  svgEl("text", { ...lab, x: lx(547, 26), y: 26, fill: "#fff" }, g).textContent = "TRANSMISSION";
  return g;
};

/* HUD prompts (section 14.8): where the HUD names a control, it uses the menus' prompt construction instead of a
   bracketed line. Each item shows its control, then the action: a key cap (a white plate with the 45-degree cut) naming
   the bound key, a controller's lettered button, or on touch the overlay control's own symbol. Several actions share
   one sheared HUD plate at black 0.50, never wider than its content, contextual actions leading and Exit trailing, as
   Back trails the prompt bar; a refused action shows its state instead (a lock and Vehicle locked in #FF8000).
   Classic sets the same prompt at the stock line's place with the stock drop shadow and no plate. (x, y) is the
   prompt's bottom-trailing corner, in stock units. */
const F_P = "IBM Plex Mono, monospace";
const pctx = document.createElement("canvas").getContext("2d");
const textW = (t, size, fam, weight = 500) => { pctx.font = `${weight} ${size}px ${fam}`; return pctx.measureText(t).width; };
const TOUCH_SYM = { jump: "M-3.4 1.7L0 -1.7L3.4 1.7", crouch: "M-3.4 -1.7L0 1.7L3.4 -1.7" };
A.hudPrompt = (parent, { x, y, items, dev = hud.device || "kbm", classic = false }) => {
  const LBL = 10.6, KH = 10.6, PH = 16, PAD = 5.5, GAP = 11;
  const g = svgEl("g", {}, parent);
  const glyphW = it => it.locked || it.warn ? 9 : dev === "pad" ? 11 : dev === "touch" ? 11.5 : Math.max(KH, textW(it.kbm, 6.4, F_P) + 6);
  const ws = items.map(it => glyphW(it) + 4 + textW(it.label, LBL, F_L, 600));
  const W = ws.reduce((a, b) => a + b, 0) + GAP * (items.length - 1) + (classic ? 0 : 2 * PAD);
  const x0 = x - W, mid = classic ? y - 6 : y - PH / 2;
  if (!classic) svgEl("rect", { x: x0, y: y - PH, width: W, height: PH, rx: 1.6, fill: "#000", "fill-opacity": .5, transform: `matrix(1 0 -.22 1 ${(.22 * mid).toFixed(2)} 0)` }, g);
  let cx = x0 + (classic ? 0 : PAD);
  items.forEach((it, i) => {
    const gw = glyphW(it), col = it.locked ? "#FF8000" : it.warn ? "#FF3333" : "#FFFFFF";
    if (it.locked) {                              // the menus' lock
      const k = svgEl("g", { transform: `translate(${cx + .6} ${mid}) scale(1.2)` }, g);
      svgEl("path", { d: "M1.3 -1.9v-1.6a2.2 2.2 0 0 1 4.4 0v1.6", fill: "none", stroke: col, "stroke-width": .9 }, k);
      svgEl("rect", { x: 0, y: -2.1, width: 7, height: 5.2, rx: .6, fill: col }, k);
    } else if (it.warn) {                         // the boost hazard
      svgEl("path", { d: `M${cx + 4.5} ${mid - 4.6}L${cx + 9} ${mid + 3.8}H${cx}Z`, fill: "#E62323", stroke: "#1C0202", "stroke-width": .6, "stroke-linejoin": "round" }, g);
      svgEl("path", { d: `M${cx + 4.5} ${mid - 1.5}V${mid + 1}M${cx + 4.5} ${mid + 2.3}V${mid + 2.6}`, stroke: "#1C0202", "stroke-width": 1.1, "stroke-linecap": "round" }, g);
    } else if (dev === "pad") {
      svgEl("circle", { cx: cx + 5.5, cy: mid, r: 5.5, fill: "#fff", "fill-opacity": .88 }, g);
      svgEl("text", { x: cx + 5.5, y: mid + 2.4, "text-anchor": "middle", "font-family": F_C, "font-weight": 700, "font-size": 6.8, fill: "#10140A" }, g).textContent = it.pad;
    } else if (dev === "touch") {                 // the overlay control's own symbol in a small cut plate
      const s = 11.5, t = mid - s / 2, c = 3;
      svgEl("path", { d: `M${cx} ${t}H${cx + s}V${t + s}H${cx + c}L${cx} ${t + s - c}Z`, fill: "#000", "fill-opacity": .55, stroke: "#B3D06E", "stroke-width": .8 }, g);
      svgEl("path", { d: TOUCH_SYM[it.touch] || TOUCH_SYM.jump, transform: `translate(${cx + s / 2} ${mid})`, fill: "none", stroke: "#fff", "stroke-width": 1.2, "stroke-linecap": "square" }, g);
    } else {
      const t = mid - KH / 2;
      svgEl("path", { d: `M${cx} ${t}H${cx + gw}V${t + KH}H${cx + 2.6}L${cx} ${t + KH - 2.6}Z`, fill: "#fff", "fill-opacity": .85 }, g);
      svgEl("text", { x: cx + gw / 2, y: mid + 2.2, "text-anchor": "middle", "font-family": F_P, "font-weight": 500, "font-size": 6.4, fill: "#10140A" }, g).textContent = it.kbm;
    }
    const lx = cx + gw + 4, ly = mid + LBL * .36;
    if (classic) svgEl("text", { x: lx + 1, y: ly + 1, "font-family": F_L, "font-weight": 600, "font-size": LBL, fill: "#000", "fill-opacity": .8 }, g).textContent = it.label;
    svgEl("text", { x: lx, y: ly, "font-family": F_L, "font-weight": 600, "font-size": LBL, fill: col, "fill-opacity": it.locked || it.warn ? 1 : .92 }, g).textContent = it.label;
    cx += ws[i] + GAP;
  });
  return { g, w: W };
};

/* Shared HUD pieces. */
A.crosshairAt = (parent, name = "crosshair_machinegun", size = 24) => {     // size in u (16-48); real art arrives with the crosshair set
  if (A.CROSSHAIRS?.[name]) { const s = svgEl("svg", { x: 320 - size / 2, y: 240 - size / 2, width: size, height: size, viewBox: "0 0 64 64", overflow: "visible" }, parent); s.innerHTML = A.uniqueIds(A.CROSSHAIRS[name].svg); return s; }
  return svgEl("path", { d: "M320 233V238M320 242V247M313 240H318M322 240H327", stroke: "#fff", "stroke-opacity": .85, "stroke-width": 1.2 }, parent);
};

/* Cubic-bezier easing (the CSS curve), for the retained timelines' authored eases. */
A.bezier = (x1, y1, x2, y2) => {
  const cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx, cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
  const X = t => ((ax * t + bx) * t + cx) * t, Y = t => ((ay * t + by) * t + cy) * t, D = t => (3 * ax * t + 2 * bx) * t + cx;
  return p => { if (p <= 0) return 0; if (p >= 1) return 1; let t = p;
    for (let i = 0; i < 8; i++) { const e = X(t) - p, d = D(t); if (Math.abs(e) < 1e-5 || Math.abs(d) < 1e-6) break; t -= e / d; }
    return Y(Math.max(0, Math.min(1, t))); };
};
/* One keyed animation per slot: starting a key cancels the one it replaces. fn(p) gets linear progress 0..1; it must
   never throw (a throw would stop the frame loop). */
const anims = new Map();
const animate = (key, dur, fn, done) => {
  const job = { t0: performance.now(), dur, fn, done }; anims.set(key, job);
  const step = now => { if (anims.get(key) !== job) return; const p = dur > 0 ? Math.min(1, Math.max(0, now - job.t0) / dur) : 1;
    try { fn(p); } catch (e) { anims.delete(key); return; }
    if (p < 1) requestAnimationFrame(step); else { anims.delete(key); try { done?.(); } catch (e) {} } };
  requestAnimationFrame(step);
};
const stopAnim = key => anims.delete(key);
/* A piecewise track: keys [[ms, value, ease?]...] (the ease shapes the segment that leaves that key), sampled at t ms. */
const sample = (keys, t) => {
  if (t <= keys[0][0]) return keys[0][1];
  for (let i = 0; i < keys.length - 1; i++) { const [a, va, ease] = keys[i], [b, vb] = keys[i + 1];
    if (t <= b) { const p = b > a ? (t - a) / (b - a) : 1, e = ease ? ease(p) : p;
      return Array.isArray(va) ? va.map((v, k) => v + (vb[k] - v) * e) : va + (vb - va) * e; } }
  return keys[keys.length - 1][1];
};
const EASE_OUT = A.bezier(.25, 1, .5, 1), EASE_IN = A.bezier(.5, 0, .75, 0), EASE_SLIDE = A.bezier(.4, 0, .6, 1);

/* The weapon strip (section 14.5, Appendix C 1.44; remastered_hud_groups.weapon_strip). Owned weapons in slot order with
   no gaps, 24.4 u plates on a 30 u pitch sheared about their own centres, upright 18 u icons, one ring that travels to
   the pending weapon, a return tick under the weapon a quick switch goes back to, the prohibition sign on an empty
   weapon, and the name above the slots (Remastered) or below them (Classic, the stock GUI, no motion).
   roster: [{ glyph, name, ammo, owned }]. Returns { select(i), refuse(i), fire(), setPinned(b), redraw() }. */
A.weaponStrip = (parent, { strogg = false, y = 350, classic = false, roster, ideal = 0, last = -1, pinned = true, rows } = {}) => {
  const k = strogg ? .22 : -.22, P = strogg ? "#FFCC00" : "#FF8000", O = strogg ? "#F59512" : "#A8A360";
  const RING = strogg ? "#F2AD0B" : "#B2CC80", RING_A = strogg ? .6 : 1, RED = strogg ? "#FF3A1A" : "#E0442E";
  const shear = cy => `matrix(1 0 ${k} 1 ${(-k * cy).toFixed(3)} 0)`;
  const top = y - 22, midY = top + 24;                            // the strip box: 640 x 48 u from y - 22
  const root = svgEl("g", {}, parent);
  const name = svgEl("g", {}, root), slotsG = svgEl("g", {}, root);
  const cursor = svgEl("g", {}, root), glide = svgEl("g", {}, cursor), shake = svgEl("g", {}, glide);
  const ringG = svgEl("g", { transform: shear(13) }, shake);
  svgEl("rect", { x: 1.6, y: 1.6, width: 22.8, height: 22.8, rx: 1, fill: "none", stroke: RING, "stroke-opacity": RING_A, "stroke-width": 1.6 }, ringG);
  const refuseRing = svgEl("rect", { x: 1.6, y: 1.6, width: 22.8, height: 22.8, rx: 1, fill: "none", stroke: RED, "stroke-width": 1.6, opacity: 0 }, ringG);
  const pulseG = strogg ? svgEl("g", { opacity: 0 }, shake) : null;
  if (pulseG) svgEl("rect", { x: 1.6, y: 1.6, width: 22.8, height: 22.8, rx: 1, fill: "none", stroke: P, "stroke-width": 1.2, transform: shear(13) }, pulseG);
  const scan = strogg ? svgEl("rect", { height: .9, fill: P, opacity: 0 }, root) : null;
  let open = pinned || classic, timer = 0, slotX = [], held = [], shown = [];
  const owned = () => roster.map((w, i) => w.owned ? i : -1).filter(i => i >= 0);
  const xOf = (pos, count) => 321 - 15 * count + 30 * pos;
  const returnsTo = () => last;
  const setXY = (el, tx = 0, ty = 0, sx = 1, sy = 1, cx = 0, cy = 0) => el.setAttribute("transform",
    `translate(${(cx + tx).toFixed(3)} ${(cy + ty).toFixed(3)}) scale(${sx.toFixed(4)} ${sy.toFixed(4)}) translate(${-cx} ${-cy})`);
  function draw() {
    slotsG.innerHTML = ""; name.innerHTML = "";
    const list = owned(), n = list.length; slotX = []; held = []; shown = list;
    list.forEach((ri, pos) => {
      const w = roster[ri], x = xOf(pos, n), sel = ri === ideal, empty = w.ammo === 0;
      slotX[ri] = x;
      const s = svgEl("g", { transform: `translate(${x} ${y})` }, slotsG);
      svgEl("rect", { x: .8, y: .8, width: 24.4, height: 24.4, rx: 1, fill: "#000", "fill-opacity": .5, transform: shear(13) }, s);
      const ic = svgEl("g", {}, s); held[ri] = { ic };
      A.icon(ic, w.glyph, { x: 13, y: 4, h: 18, anchor: "middle", color: sel && !empty ? P : O, opacity: sel && !empty ? 1 : .6 });
      if (empty) { const sg = svgEl("g", {}, s); held[ri].sign = sg;
        svgEl("circle", { cx: 13, cy: 13, r: 7.4, fill: "none", stroke: "#A50000", "stroke-width": 2.3 }, sg);
        svgEl("path", { d: "M18.2 7.8L7.8 18.2", stroke: "#A50000", "stroke-width": 2.3 }, sg); }
      if (ri === returnsTo() && !sel && last >= 0 && !classic) {
        if (strogg) svgEl("path", { d: "M10.2 29.4L13 26.8L15.8 29.4", fill: "none", stroke: RING, "stroke-opacity": RING_A, "stroke-width": 1.3, "stroke-linejoin": "miter" }, s);
        else svgEl("rect", { x: 8.5, y: 26.9, width: 9, height: 1.5, rx: .4, fill: RING, "fill-opacity": .75, transform: shear(27.65) }, s);
      }
    });
    cursor.setAttribute("transform", `translate(${slotX[ideal] ?? -999} ${y})`);
    cursor.style.display = slotX[ideal] == null ? "none" : "";
    /* Remastered: Marine 18 dp (12 u, small capitals) #B0C891 above the slots, R_Strogg 14.4 dp #FCFFC8; a 1 u shadow. */
    const label = roster[ideal]?.name || "", ny = classic ? 406 : y - 7;
    const font = strogg ? { "font-family": F_S, "font-size": 9.6, "font-weight": 600, "letter-spacing": .4 }
      : { "font-family": F_M, "font-size": 12, style: "font-variant:small-caps" };
    const txt = strogg ? label.toUpperCase() : label;
    svgEl("text", { ...font, x: 321, y: ny + 1, "text-anchor": "middle", fill: "#000" }, name).textContent = txt;
    svgEl("text", { ...font, x: 320, y: ny, "text-anchor": "middle", fill: strogg ? "#FCFFC8" : "#B0C891" }, name).textContent = txt;
    if (scan) { scan.setAttribute("x", 313 - 15 * n); scan.setAttribute("width", 12 + 30 * n); scan.setAttribute("y", top); }
    root.setAttribute("opacity", open ? 1 : 0);
  }
  /* Open: Marine rises 4 u over 140 ms with easeOut, fading in over 110 ms; Strogg tunes in about the row's middle from
     103% wide and 30% tall, fading in over 90 ms, with a 0.9 u hairline sweeping down. Starts from wherever it is. */
  let cur = { o: open ? 1 : 0, ty: open ? 0 : 4, sx: 1, sy: 1 };
  const paint = () => { root.setAttribute("opacity", cur.o.toFixed(3)); setXY(root, 0, cur.ty, cur.sx, cur.sy, 320, midY); };
  function show(wasOpen) {
    clearTimeout(timer);
    const from = { ...cur }, calm = reduce || classic;
    if (classic) { cur = { o: 1, ty: 0, sx: 1, sy: 1 }; paint(); }
    else if (calm) animate(root, 80 * (1 - from.o), p => { cur = { o: from.o + (1 - from.o) * p, ty: 0, sx: 1, sy: 1 }; paint(); });
    else animate(root, 140, p => { const t = p * 140, e = EASE_OUT(p);
      cur = { o: from.o + (1 - from.o) * Math.min(1, t / (strogg ? 90 : 110)), ty: from.ty * (1 - e), sx: from.sx + (1 - from.sx) * e, sy: from.sy + (1 - from.sy) * e }; paint(); });
    if (strogg && !wasOpen && !calm) animate(scan, 150, p => { const t = p * 150;
      scan.setAttribute("transform", `translate(0 ${(-6 + 56 * p).toFixed(2)})`); scan.setAttribute("opacity", sample([[0, 0], [30, .55], [110, .4], [150, 0]], t)); });
    open = true;
    if (!pinned && !classic) timer = setTimeout(() => close(150), 1000);   // the hold: 1000 ms after the latest change
  }
  /* Close: Marine sinks 3 u with easeIn, fading out; Strogg collapses to 102% wide and 35% tall; 150 ms, 90 ms on firing. */
  function close(ms) {
    if (pinned || classic || !open) return;
    clearTimeout(timer);
    const from = { ...cur }, calm = reduce;
    const to = strogg ? { o: 0, ty: 0, sx: 1.02, sy: .35 } : { o: 0, ty: 3, sx: 1, sy: 1 };
    animate(root, calm ? Math.min(80, ms) : ms, p => { const e = calm ? p : EASE_IN(p), eo = strogg ? e : p;
      cur = { o: from.o * (1 - eo), ty: calm ? from.ty : from.ty + (to.ty - from.ty) * e, sx: calm ? from.sx : from.sx + (to.sx - from.sx) * e, sy: calm ? from.sy : from.sy + (to.sy - from.sy) * e }; paint(); },
      () => { open = false; cur = strogg ? { o: 0, ty: 0, sx: 1.03, sy: .3 } : { o: 0, ty: 4, sx: 1, sy: 1 }; paint(); });
  }
  /* A change: the ring travels from the slot it left over 100 ms + 6 ms a slot (at most 170 ms; Strogg 10 ms longer,
     stretching up to 22% at 45% of the way), only along a strip already on screen with an unchanged count. */
  function select(i) {
    const list = owned(), wasOpen = open, before = list.indexOf(ideal), after = list.indexOf(i);
    if (i !== ideal) { last = ideal; ideal = i; }
    draw();
    const steps = after - before;
    if (wasOpen && steps && !classic && !reduce && before >= 0) {
      const ms = Math.min(100 + 6 * Math.abs(steps), 170) + (strogg ? 10 : 0), start = -30 * steps;
      if (strogg) { const st = 1 + Math.min(.05 * Math.abs(steps), .22), mid = Math.round(ms * .45);
        animate(glide, ms, p => { const t = p * ms; const [tx, sx] = sample([[0, [start, 1], EASE_SLIDE], [mid, [start * .3, st], EASE_OUT], [ms, [0, 1]]], t); setXY(glide, tx, 0, sx, 1, 13, 13); });
        animate(pulseG, 220, p => { const e = EASE_OUT(p); pulseG.setAttribute("opacity", (.8 * (1 - p)).toFixed(3)); setXY(pulseG, 0, 0, 1 + .45 * e, 1 + .45 * e, 13, 13); });
      } else {
        animate(glide, ms, p => setXY(glide, start * (1 - EASE_OUT(p)), 0));
        const ic = held[i]?.ic; if (ic) animate(ic, 150, p => { const s = 1.16 - .16 * EASE_OUT(p); setXY(ic, 0, 0, s, s, 13, 13); });
      }
      /* The name slides in from the side the ring came from: Marine 4 u from 0.30 over 130 ms; Strogg slips 3 u then
         1.2 u back in hard 40 ms steps from 0.45. */
      const sg = Math.sign(steps);
      if (strogg) animate(name, 120, p => { const t = p * 120; const [tx, o] = t < 40 ? [3 * sg, .45] : t < 80 ? [-1.2 * sg, .8] : [0, 1]; setXY(name, tx, 0); name.setAttribute("opacity", o); });
      else animate(name, 130, p => { setXY(name, 4 * sg * (1 - EASE_OUT(p)), 0); name.setAttribute("opacity", (.3 + .7 * p).toFixed(3)); });
    } else { stopAnim(glide); glide.removeAttribute("transform"); name.removeAttribute("transform"); name.setAttribute("opacity", 1); }
    show(wasOpen);
  }
  /* A refusal: the ring shakes (Marine -2.5, +2.0, -1.2, +0.6 u by 190 ms; Strogg hard 35 ms steps), a red ring holds
     70 ms (Strogg 50 ms) and is gone by 240 ms (kept under reduced motion), and an empty weapon's sign throbs from 135%. */
  function refuse(i) {
    const wasOpen = open; draw();
    if (!classic && !reduce) {
      if (strogg) animate(shake, 140, p => { const t = p * 140; const [tx, sx] = t < 35 ? [2.2, 1] : t < 70 ? [-1.8, 1.06] : t < 105 ? [1, 1] : [0, 1]; setXY(shake, tx, 0, sx, 1, 13, 13); });
      else animate(shake, 190, p => setXY(shake, sample([[0, 0, EASE_OUT], [30, -2.5], [70, 2], [110, -1.2], [150, .6], [190, 0]], p * 190), 0));
      const sign = held[i]?.sign; if (sign) animate(sign, 180, p => { const s = 1.35 - .35 * EASE_OUT(p); setXY(sign, 0, 0, s, s, 13, 13); });
    }
    const hold = strogg ? 50 : 70;
    if (!classic) animate(refuseRing, 240, p => refuseRing.setAttribute("opacity", sample([[0, 1], [hold, 1], [240, 0]], p * 240)));
    show(wasOpen);
  }
  draw(); paint();
  return { root, select, refuse, fire: () => close(90), redraw: draw, get ideal() { return ideal; }, get last() { return last; },
    setPinned: b => { pinned = b; if (b) show(open); else { clearTimeout(timer); timer = setTimeout(() => close(150), 1000); } } };
};
const F_S = "Oxanium, Chakra Petch, sans-serif";

/* Main HUD composite on the Remastered layout. */
const svg = document.getElementById("hudsvg");
if (svg) {
  const ctl = document.getElementById("hudLayoutCtl"); if (ctl) hud.controls(ctl, { minimal: true });
  /* The stock eleven in slot order: [weapon key, name, strip glyph, crosshair]. */
  const ROSTER = [["blaster", "Blaster", "item_blaster", "crosshair_blaster"], ["machinegun", "Machinegun", "item_machinegun", "crosshair_machinegun"],
    ["shotgun", "Shotgun", "item_shotgun", "crosshair_shotgun"], ["hyperblaster", "Hyperblaster", "item_hyperblaster", "crosshair_blaster"],
    ["grenadelauncher", "Grenade Launcher", "item_grenade", "crosshair_grenadelauncher"], ["nailgun", "Nailgun", "item_nailgun", "crosshair_nailgun"],
    ["rocketlauncher", "Rocket Launcher", "item_rocket", "crosshair_rocketlauncher"], ["railgun", "Railgun", "item_railgun", "crosshair_railgun"],
    ["lightninggun", "Lightning Gun", "item_lightning", "crosshair_lightninggun"], ["darkmattergun", "Dark Matter Gun", "item_darkmatter", "crosshair_grenadelauncher"],
    ["napalmgun", "Napalm Launcher", "item_fire", "crosshair_napalm"]];
  const AMMO = { blaster: { infinite: true }, machinegun: { clip: 40, loaded: 32, total: 164, max: 300 }, shotgun: { clip: 8, loaded: 6, total: 24, max: 50 },
    hyperblaster: { clip: 60, loaded: 0, total: 0, max: 300 }, grenadelauncher: { clip: 8, loaded: 3, total: 11, max: 50 }, nailgun: { clip: 50, loaded: 50, total: 300, max: 300 },
    rocketlauncher: { clip: 0, total: 17, max: 40 }, railgun: { clip: 3, loaded: 2, total: 9, max: 50 }, lightninggun: { clip: 0, total: 140, max: 400 },
    darkmattergun: null, napalmgun: { clip: 20, loaded: 20, total: 60, max: 100 } };
  const stripRoster = () => ROSTER.map(([key, name, glyph]) => ({ glyph, name, owned: !!AMMO[key], ammo: AMMO[key] ? (AMMO[key].infinite ? Infinity : AMMO[key].total) : 0 }));
  let sel = 5, prev = 2;
  const showWeapon = () => ammo?.weapon({ key: ROSTER[sel][0], ...AMMO[ROSTER[sel][0]] });
  let hp = 100, ap = 57, radio = true, rhino = false, feed = [];
  let health, armor, ammo, hitG, feedG, radioG, strip, xhG;
  const GX = () => hud.classic ? [13, 190, 326] : [101, 278, 414];
  const full = () => !hud.minimal || hud.classic;              // Minimal (ui_hudLayout 2) keeps the gauges and the crosshair

  /* Pickup lines (14.8; remastered_hud_groups.pickup_lines): a sheared black 0.50 plate 21 u tall, never wider than its
     content (min 96 u), the item icon at 0.80 and the item name in Marine 14 dp (9.6 u, small capitals) #DCEAC2; a repeat
     merges with " × n". Remastered: up to two lines centred over the status bar, newest at y 401, the older at 377.
     Classic keeps the stock single line over the ammo gauge. */
  const pctx2 = document.createElement("canvas").getContext("2d");
  const measure = (t, size) => { pctx2.font = `${size}px Michroma, Eurostile, sans-serif`; pctx2.fontVariantCaps = "small-caps"; return pctx2.measureText(t).width; };
  function drawFeed() {
    feedG.innerHTML = "";
    if (!full()) return;
    const lines = hud.classic ? feed.slice(-1) : feed.slice(-2);
    lines.forEach((p, k) => {
      const row = lines.length - 1 - k, y = 401 - row * 24;
      const g = svgEl("g", { opacity: p.alpha ?? 1 }, feedG);
      const merged = p.count > 1 && !hud.classic, tw = measure(p.name, 9.6) + (merged ? 6 + measure(`×${p.count}`, 9.6) : 0);
      const w = hud.classic ? 230 : Math.max(96, 25 + tw + 7), x = hud.classic ? 21 : 320 - w / 2;
      svgEl("rect", { x: x + 1, y: y + 1.5, width: w - 2, height: 21, rx: 1, fill: "#000", "fill-opacity": .5, transform: `matrix(1 0 -0.22 1 ${(0.22 * (y + 12)).toFixed(2)} 0)` }, g);
      A.icon(g, p.icon, { x: x + 6, y: y + 4, h: 16, color: "#AAAC6F", opacity: .8 });
      const t = svgEl("text", { x: x + 25, y: y + 15.4, "font-family": F_M, "font-size": 9.6, style: "font-variant:small-caps", fill: "#DCEAC2" }, g);
      t.textContent = p.name;
      if (merged) svgEl("tspan", { dx: 6 }, t).textContent = `×${p.count}`;
    });
  }
  const drawRadio = () => { radioG.innerHTML = ""; if (radio && full()) A.radioChatter(radioG, { rhino }); };
  const drawCrosshair = () => { xhG.innerHTML = ""; A.crosshairAt(xhG, ROSTER[sel][3], 32); };   // g_crosshairSize 32, the weapon's own design
  function build() {
    svg.innerHTML = "";
    const lay = hud.fit(svg);
    hud.regionsLayer(svg, lay);
    xhG = svgEl("g", {}, svg); drawCrosshair();
    const cc = hud.place(svg, "cc", lay);
    /* Damage direction (14.7): the dome, plus Remastered's chevron at its peak so direction never depends on colour. */
    hitG = svgEl("g", { opacity: 0 }, cc);
    const hg = svgEl("radialGradient", { id: "hitDome", cx: "50%", cy: "50%", r: "50%" }, svgEl("defs", {}, svg));
    [[0, 1], [.55, .45], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#FF1A1A", "stop-opacity": a }, hg));
    svgEl("ellipse", { cx: 320, cy: 89, rx: 150, ry: 52, fill: "url(#hitDome)", style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, hitG);
    if (!hud.classic) svgEl("path", { d: "M311 52L320 44L329 52", fill: "none", stroke: "#FF1A1A", "stroke-opacity": .9, "stroke-width": 3.2, "stroke-linejoin": "miter", style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, hitG);
    const sb = hud.place(svg, "bc", lay);
    const [ax, hx, rx] = GX();
    ammo = marineGauge(sb, ax, "ammo"); health = marineGauge(sb, hx, "health"); armor = marineGauge(sb, rx, "armor");
    showWeapon(); armor.set(ap, ap / 100); setHp(hp);
    feedG = svgEl("g", {}, sb); const stripHost = svgEl("g", {}, sb);
    drawFeed();
    strip = full() ? A.weaponStrip(stripHost, { y: hud.classic ? 370 : 350, classic: hud.classic, roster: stripRoster(), ideal: sel, last: prev, pinned: true }) : null;
    radioG = hud.place(svg, "tr", lay); drawRadio();
  }
  const lowState = on => {
    [health.fill, health.trackFill, health.bevel].forEach(el => el.setAttribute(el === health.bevel ? "stroke" : "fill", on ? "#FF4C00" : "#B3D06E"));
    health.fill.setAttribute("fill-opacity", on ? .5 : .35); health.trackFill.setAttribute("fill-opacity", on ? .35 : .2);
    health.ekg?.setColor(on ? "#4C0D00" : "#161B0E");
    health.rim.setAttribute("opacity", on ? 1 : 0); health.rim.classList.toggle("rim-on", on);
  };
  function setHp(v) { hp = Math.max(1, Math.min(125, v)); health.set(hp, Math.min(1, hp / 100)); lowState(hp < 26);
    const box = document.getElementById("hudLow"); box?.classList.toggle("on", hp < 26); box?.setAttribute("aria-pressed", hp < 26); }
  const pop = g => {                     // gain pop: 120% white settling to 100% and the readout color over 250 ms
    if (reduce) return;                  // nonessential: reduced motion drops it
    const t0 = performance.now(), ink = [...g.icon.querySelectorAll("path,circle,rect,polygon")].filter(el => /^(#A8A360|currentColor)$/i.test(el.getAttribute("fill") || ""));
    const was = ink.map(el => el.getAttribute("fill"));
    const step = now => { const p = Math.min(1, Math.max(0, now - t0) / 250);
      g.num.style.transform = `scale(${1.2 - .2 * p})`; g.num.setAttribute("fill", p < 1 ? `rgb(${255 - 34 * p},${255 - 20 * p},${255 - 60 * p})` : "#DDEBC3");
      ink.forEach((el, i) => el.setAttribute("fill", p < 1 ? "#FFFFFF" : was[i]));   // the icon flashes white: only its tinted ink
      if (p < 1) requestAnimationFrame(step); };
    requestAnimationFrame(step);
  };
  const hit = () => {                    // hit indicator: rotated toward the source, 0.70 to zero over 500 ms
    if (!full()) return;
    hitG.setAttribute("transform", `rotate(${Math.round(Math.random() * 360)} 320 240)`);
    const t0 = performance.now();
    const step = now => { const p = Math.min(1, Math.max(0, now - t0) / 500); hitG.setAttribute("opacity", .7 * (1 - p)); if (p < 1) requestAnimationFrame(step); };
    requestAnimationFrame(step);
  };
  let feedTimer;
  const pickup = (name, icon, apply) => {
    const last = feed[feed.length - 1];
    if (last && last.name === name && !hud.classic) { last.count++; last.t = performance.now(); } else feed.push({ name, icon, count: 1, t: performance.now() });
    feed = feed.slice(-2); apply?.(); drawFeed();
    clearInterval(feedTimer);
    feedTimer = setInterval(() => {      // each line holds 3000 ms, then fades over 250 ms
      const now = performance.now(); feed.forEach(p => { p.alpha = Math.max(0, 1 - Math.max(0, now - p.t - 3000) / 250); });
      feed = feed.filter(p => p.alpha > 0); drawFeed(); if (!feed.length) clearInterval(feedTimer); }, 50);
  };
  build();
  hud.on(build);
  document.getElementById("hudHit")?.addEventListener("click", () => { setHp(hp - 15); hit(); });
  document.getElementById("hudPick")?.addEventListener("click", () => pickup("Large Health", "item_health", () => { setHp(hp + 25); pop(health); }));
  document.getElementById("hudShard")?.addEventListener("click", () => pickup("Armor Shard", "item_armorshard", () => { ap = Math.min(200, ap + 5); armor.set(ap, Math.min(1, ap / 100)); pop(armor); }));
  document.getElementById("hudLow")?.addEventListener("click", e => { e.stopImmediatePropagation(); setHp(hp < 26 ? 100 : 20); }, true);
  /* Next weapon skips weapons the player lacks or that are empty; the ring travels and the tick marks the one left. */
  document.getElementById("hudNext")?.addEventListener("click", () => {
    let i = sel; do { i = (i + 1) % ROSTER.length; } while (!AMMO[ROSTER[i][0]] || AMMO[ROSTER[i][0]].total === 0);
    prev = sel; sel = i; strip?.select(i); showWeapon(); drawCrosshair(); });
  document.getElementById("hudRadio")?.addEventListener("click", () => { radio = !radio; rhino = false; drawRadio(); });
  document.getElementById("hudRhino")?.addEventListener("click", () => { radio = true; rhino = true; drawRadio(); });   // each line restarts the shrink
}

/* The weapon strip's motion, magnified: the game's behaviour (open on a change, close 1000 ms later or on firing),
   held open at first so the still drawing shows it. The roster carries every stock weapon but the dark matter gun,
   with the hyperblaster empty. */
const sd = document.getElementById("stripDemo");
if (sd) {
  const R = [["item_blaster", "Blaster", Infinity], ["item_machinegun", "Machinegun", 164], ["item_shotgun", "Shotgun", 24], ["item_hyperblaster", "Hyperblaster", 0],
    ["item_grenade", "Grenade Launcher", 11], ["item_nailgun", "Nailgun", 250], ["item_rocket", "Rocket Launcher", 17], ["item_railgun", "Railgun", 9],
    ["item_lightning", "Lightning Gun", 140], ["item_darkmatter", "Dark Matter Gun", -1], ["item_fire", "Napalm Launcher", 60]]
    .map(([glyph, name, ammo]) => ({ glyph, name, ammo: Math.max(0, ammo), owned: ammo !== -1 }));
  let strogg = false, held = false, still = true, ideal = 5, last = 2, st;   // still: the first drawing, until a control is used
  const build = () => { sd.innerHTML = ""; st = A.weaponStrip(sd, { strogg, y: 350, roster: R, ideal, last, pinned: held || still }); };
  build();
  const usable = i => R[i].owned && R[i].ammo !== 0;
  const step = d => { let i = st.ideal; do { i = (i + d + R.length) % R.length; } while (!usable(i)); return i; };
  const wake = () => { if (still) { still = false; st.setPinned(held); } };
  const go = i => { wake(); if (!usable(i) || i === st.ideal) st.refuse(i); else st.select(i); ideal = st.ideal; last = st.last; };
  const on = (id, f) => document.getElementById(id)?.addEventListener("click", f);
  on("stripPrev", () => go(step(-1))); on("stripNext", () => go(step(1))); on("stripJump", () => go(0)); on("stripRefuse", () => go(3));
  on("stripFire", () => { wake(); st.fire(); });
  const box = document.getElementById("stripHold");
  box?.addEventListener("click", e => { e.stopImmediatePropagation(); held = !held; still = false; box.classList.toggle("on", held); box.setAttribute("aria-pressed", held); st.setPinned(held); }, true);
  document.getElementById("stripFam")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; strogg = b.dataset.f === "s"; still = true;
    document.querySelectorAll("#stripFam .seg").forEach(x => x.setAttribute("aria-pressed", x === b)); build(); });
}

/* Magnified anatomy: Marine (unsheared, measured) and Strogg (sheared +0.22). */
const anat = document.getElementById("gaugeAnat");
if (anat) {
  const g = svgEl("g", { transform: "scale(1.5)" }, anat);
  svgEl("path", { d: BACK, fill: "#000", "fill-opacity": .6, stroke: "rgba(255,255,255,.25)", "stroke-width": .3 }, g);
  svgEl("path", { d: FILL, fill: "#B3D06E", "fill-opacity": .35, stroke: "#B3D06E", "stroke-width": .5 }, g);
  A.icon(g, "health", { x: 100, y: 38, h: 16, color: "#A8A360", opacity: .6 });
  const L = (x1, y1, x2, y2, t, tx, ty, anchor = "start") => { svgEl("path", { d: `M${x1} ${y1}L${x2} ${y2}`, stroke: "#E38900", "stroke-width": .35 }, g);
    svgEl("text", { x: tx, y: ty, "text-anchor": anchor, "font-family": "IBM Plex Mono, monospace", "font-size": 4, fill: "#B8C29E" }, g).textContent = t; };
  L(60, 9.2, 60, 2, "body 123×40.1 u, r 1–1.4", 61, 3);
  L(88, 53, 80, 62, "foot 6.9 u, 45° flank", 79, 63.5, "end");
  L(5.4, 20, -4, 20, "inset 4.4", -4.5, 18.5, "start");
  L(112, 34.6, 126, 26, "ledge at 1/3, 14.2 u", 127, 25);
  L(97, 40.5, 128, 48, "arc r 16.6 u", 129, 49);
  L(108, 50, 116, 62, "icon 16 u", 117, 63.5);
}
const sg = document.getElementById("gaugeStrogg");
if (sg) {
  const g = svgEl("g", { transform: "scale(1.5) matrix(1 0 0.22 1 -7.37 0)" }, sg);
  const SB = "M1 22.5L26.8 7.3H69L72 11H128V53.4H1Z";
  const SF = "M46.7 15.7H124V40.3H69.3L56.3 48.7H5.1V39.8L15.8 33L24.2 32.5L33.4 26.8L33.9 24.1Z";
  svgEl("path", { d: SB, fill: "#000", "fill-opacity": .5 }, g);
  svgEl("path", { d: SF, fill: "#FF9000", "fill-opacity": .35, stroke: "#FF9000", "stroke-opacity": .6, "stroke-width": .5 }, g);
  const up = svgEl("g", { transform: "scale(1.5)" }, sg);
  svgEl("text", { x: 118, y: 33, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 21, fill: "#FCFFC8" }, up).textContent = "100";
  A.icon(up, "health", { x: 12, y: 14, h: 16, color: "#FFCC00", opacity: .6 });
}

/* Transmission, ×4: the display over a neutral scene, the mask outline, and a strip chart of the height driver. */
const ra = document.getElementById("radioAnat");
if (ra) {
  let mode = "table";
  const LBL = { "font-family": "IBM Plex Mono, monospace", "font-size": 9, fill: "#B8C29E" };
  const draw = () => {
    ra.innerHTML = "";
    svgEl("rect", { width: 540, height: 214, fill: "#2a2e30" }, ra);
    const zoom = svgEl("g", { transform: "translate(-2010 -12) scale(4)" }, ra);     // stock x 506-634, y 4-33
    const radio = A.radioChatter(zoom, { mode });
    svgEl("path", { d: "M526.45 8.56H545.67V30.44H522.61Z", fill: "none", stroke: "#E38900", "stroke-width": .25, "stroke-dasharray": "1 .8" }, zoom);
    const lab = (x, y, t, a = "start") => svgEl("text", { ...LBL, x, y, "text-anchor": a }, ra).textContent = t;
    lab(12, 146, "mask: right trapezoid, 19.2 u → 23.1 u");
    lab(12, 160, mode === "table" ? "scroll 24.6 u/s toward the leading edge" : "actual sample peaks · 60 Hz · 650 ms");
    lab(12, 174, mode === "table" ? "core 4 texels; speech spikes to 32%" : "silence flat · pause and seek follow playback");
    lab(12, 188, "added in #FF8000 (Strogg #FFCC00)");
    /* Strip chart: the drawn height over the last 2.5 s. */
    const cx = 330, cw = 198, cy = 128, ch = 76;
    svgEl("rect", { x: cx, y: cy, width: cw, height: ch, fill: "#0B0D0A", stroke: "rgba(139,150,75,.3)" }, ra);
    [0.5, 1, 1.5, 2, 2.5].forEach(v => { const yy = cy + ch - v / 2.6 * ch; svgEl("path", { d: `M${cx} ${yy}h${cw}`, stroke: "#fff", "stroke-opacity": v === 1 ? .18 : .06 }, ra); lab(cx - 4, yy + 3, `×${v}`, "end"); });
    lab(cx, cy - 6, mode === "table" ? "height ×1/k · wavTable · 79 ms" : "height follows the line's level");
    const line = svgEl("path", { fill: "none", stroke: "#FF8000", "stroke-width": 1.4, "stroke-linejoin": "round" }, ra);
    const f = now => { const p = []; for (let i = 0; i <= 120; i++) { const t = now - 2500 + i * 2500 / 120, v = mode === "table" ? 1 / A.wavK(t) : .05 + 2.3 * A.voiceLevel(t); p.push(`${i ? "L" : "M"}${(cx + i * cw / 120).toFixed(1)} ${(cy + ch - Math.min(2.6, v) / 2.6 * ch).toFixed(1)}`); } line.setAttribute("d", p.join("")); };
    f(performance.now()); if (!reduce) A.tick(line, f);
    return radio;
  };
  draw();
  const seg = (btn, m) => btn?.addEventListener("click", () => { mode = m; document.querySelectorAll("#radioTable,#radioVoice").forEach(b => b.setAttribute("aria-pressed", b === btn)); draw(); });
  seg(document.getElementById("radioTable"), "table"); seg(document.getElementById("radioVoice"), "voice");
}

/* EKG, ×3: one beat with its measured waves over the gauge's dark track, then the same trace at the stock gain. */
const ea = document.getElementById("ekgAnat");
if (ea) {
  /* The texture's full width (two beats) at ×3, drawn light at full strength so the construction reads; the gauge
     adds the same trace at #161B0E. */
  svgEl("rect", { width: 540, height: 214, fill: "#0B0D0A" }, ea);
  const z = svgEl("g", { transform: "translate(82 -24) scale(3)" }, ea);
  svgEl("rect", { x: 0, y: 13.8, width: 125, height: 30.9, fill: "#B3D06E", "fill-opacity": .1 }, z);
  const fid = id("eh"), f = svgEl("filter", { id: fid, x: "-10%", y: "-60%", width: "120%", height: "220%" }, svgEl("defs", {}, ea));
  svgEl("feGaussianBlur", { stdDeviation: 1.25 }, f);
  const d = EKG_BEAT(0) + EKG_BEAT(62.5), C = "#E8F2D0";
  svgEl("path", { d, fill: "none", stroke: C, "stroke-width": 2.6, "stroke-opacity": .55, "stroke-linejoin": "round", filter: `url(#${fid})` }, z);
  svgEl("path", { d, fill: "none", stroke: C, "stroke-width": .95, "stroke-linejoin": "round", "stroke-linecap": "round" }, z);
  const b = EKG_B, LBL = { "font-family": "IBM Plex Mono, monospace", "font-size": 3.1, fill: "#E38900" };
  const mark = (x, y, t, dy = -2.4, anchor = "middle", dx = 0) => { svgEl("circle", { cx: x, cy: y, r: .65, fill: "#E38900" }, z); svgEl("text", { ...LBL, x: x + dx, y: y + dy, "text-anchor": anchor }, z).textContent = t; };
  mark(14.9, b - 3.46, "P +3.5"); mark(23.7, b + 2.5, "Q −2.5", 4.2, "end", -1.2); mark(25.8, b - 17.5, "R +17.5"); mark(27.7, b + 6.2, "S −6.2", 3.4, "start", 1.4);
  mark(44.1, b - 6.22, "T +6.2"); mark(55.2, b - 3, "U +3.0");
  svgEl("path", { d: `M0 ${b + 13}V${b + 15.5}M62.5 ${b + 13}V${b + 15.5}M0 ${b + 14.25}H62.5`, stroke: "#B8C29E", "stroke-width": .3 }, z);
  svgEl("text", { ...LBL, x: 31.25, y: b + 19.2, "text-anchor": "middle", fill: "#B8C29E" }, z).textContent = "one beat · 62.5 u · 2.5 s";
  svgEl("text", { ...LBL, x: 125, y: b + 4.2, "text-anchor": "end", fill: "#B8C29E" }, z).textContent = "baseline 58%";
}
})();
