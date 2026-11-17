
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

/* Main HUD composite on the Remastered layout. */
const svg = document.getElementById("hudsvg");
if (svg) {
  const ctl = document.getElementById("hudLayoutCtl"); if (ctl) hud.controls(ctl);
  const OWNED = [["blaster", "BLASTER", 1, "BL"], ["machinegun", "MACHINEGUN", 1, "MG"], ["shotgun", "SHOTGUN", 1, "SG"], ["hyperblaster", "HYPERBLASTER", 0, "HB"], ["grenadelauncher", "GRENADE LAUNCHER", 1, "GL"], ["nailgun", "NAILGUN", 1, "NG"], ["rocketlauncher", "ROCKET LAUNCHER", 1, "RL"], ["railgun", "RAILGUN", 1, "RG"]];
  const AMMO = { blaster: { infinite: true }, machinegun: { clip: 40, loaded: 32, total: 164, max: 300 }, shotgun: { clip: 8, loaded: 6, total: 24, max: 50 },
    hyperblaster: { clip: 60, loaded: 0, total: 0, max: 300 }, grenadelauncher: { clip: 8, loaded: 3, total: 11, max: 50 }, nailgun: { clip: 50, loaded: 36, total: 86, max: 200 },
    rocketlauncher: { clip: 0, total: 17, max: 40 }, railgun: { clip: 3, loaded: 2, total: 9, max: 50 } };
  const showWeapon = () => ammo?.weapon({ key: OWNED[sel][0], ...AMMO[OWNED[sel][0]] });
  let hp = 100, ap = 57, sel = 5, radio = true, rhino = false, feed = [];
  let health, armor, ammo, hitG, feedG, stripG, radioG, radioLay;
  const GX = () => hud.classic ? [13, 190, 326] : [101, 278, 414];

  function drawStrip() {
    stripG.innerHTML = "";
    const classic = hud.classic, y = classic ? 370 : 350, left = (640 - (OWNED.length * 30 - 2)) / 2;
    OWNED.forEach(([key, name, ammoLeft, ab], i) => {
      const x = left + i * 30, on = i === sel;
      const plate = svgEl("g", { transform: `matrix(1 0 -0.22 1 ${0.22 * (y + 13)} 0)` }, stripG);
      svgEl("rect", { x: x + .8, y: y + .8, width: 24.4, height: 24.4, rx: 1, fill: "#000", "fill-opacity": .5 }, plate);
      if (on) svgEl("rect", { x: x + 1.6, y: y + 1.6, width: 22.8, height: 22.8, rx: 1, fill: "none", stroke: "#B2CC80", "stroke-width": 1.6 }, plate);
      if (A.ICONS[key]) A.icon(stripG, key, { x: x + 13, y: y + 4, h: 18, anchor: "middle", color: on && ammoLeft ? "#FF8000" : "#A8A360", opacity: on && ammoLeft ? 1 : .6 });
      else svgEl("text", { x: x + 13, y: y + 16, "text-anchor": "middle", "font-family": F_M, "font-size": 7.5, fill: on && ammoLeft ? "#FF8000" : "#A8A360", "fill-opacity": on && ammoLeft ? 1 : .6 }, stripG).textContent = ab;
      if (!ammoLeft) { svgEl("circle", { cx: x + 13, cy: y + 13, r: 7.4, fill: "none", stroke: "#A50000", "stroke-width": 2.3 }, stripG); svgEl("path", { d: `M${x + 18.2} ${y + 7.8}L${x + 7.8} ${y + 18.2}`, stroke: "#A50000", "stroke-width": 2.3 }, stripG); }
    });
    const nm = OWNED[sel][1], ny = classic ? 406 : y - 7;          // Remastered puts the name above the slots, clear of the pickup lines
    svgEl("text", { x: 321, y: ny + 1, "text-anchor": "middle", "font-family": F_M, "font-size": 8.4, fill: "#000" }, stripG).textContent = nm;
    svgEl("text", { x: 320, y: ny, "text-anchor": "middle", "font-family": F_M, "font-size": 8.4, fill: "#B0C891" }, stripG).textContent = nm;
  }
  /* Pickup lines: Classic keeps the stock single line over the ammo gauge; Remastered centers up to two lines over the
     status bar, newest nearest the gauges, and merges repeats with a count. */
  function drawFeed() {
    feedG.innerHTML = "";
    const lines = hud.classic ? feed.slice(-1) : feed.slice(-2);
    lines.forEach((p, k) => {
      const row = lines.length - 1 - k, y = 401 - row * 24;
      const g = svgEl("g", { opacity: p.alpha ?? 1 }, feedG);
      const label = p.count > 1 && !hud.classic ? `${p.name}  ×${p.count}` : p.name;
      const w = hud.classic ? 230 : Math.max(96, 30 + label.length * 5.9), x = hud.classic ? 21 : 320 - w / 2;
      svgEl("rect", { x: x + 1.8, y: y + 1.5, width: w - 3.6, height: 21, rx: 1, fill: "#000", "fill-opacity": .5, transform: `matrix(1 0 -0.22 1 ${0.22 * (y + 12)} 0)` }, g);
      A.icon(g, p.icon, { x: x + 6, y: y + 4, h: 16, color: "#AAAC6F", opacity: .8 });
      svgEl("text", { x: x + 25, y: y + 15, "font-family": F_M, "font-size": 6.7, fill: "#DCEAC2" }, g).textContent = label;
    });
  }
  const drawRadio = () => { radioG.innerHTML = ""; if (radio) A.radioChatter(radioG, { rhino }); };
  function build() {
    svg.innerHTML = "";
    const lay = hud.fit(svg);
    hud.regionsLayer(svg, lay);
    A.crosshairAt(svg);
    const cc = hud.place(svg, "cc", lay);
    hitG = svgEl("g", { opacity: 0 }, cc);
    const hg = svgEl("radialGradient", { id: "hitDome", cx: "50%", cy: "50%", r: "50%" }, svgEl("defs", {}, svg));
    [[0, 1], [.55, .45], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#FF1A1A", "stop-opacity": a }, hg));
    svgEl("ellipse", { cx: 320, cy: 89, rx: 150, ry: 52, fill: "url(#hitDome)", style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, hitG);
    const sb = hud.place(svg, "bc", lay);
    const [ax, hx, rx] = GX();
    ammo = marineGauge(sb, ax, "ammo"); health = marineGauge(sb, hx, "health"); armor = marineGauge(sb, rx, "armor");
    showWeapon(); armor.set(ap, ap / 100); setHp(hp);
    feedG = svgEl("g", {}, sb); stripG = svgEl("g", {}, sb);
    drawFeed(); drawStrip();
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
    const t0 = performance.now();
    const step = now => { const p = Math.min(1, Math.max(0, now - t0) / 250);
      g.num.style.transform = `scale(${1.2 - .2 * p})`; g.num.setAttribute("fill", p < 1 ? `rgb(${255 - 34 * p},${255 - 20 * p},${255 - 60 * p})` : "#DDEBC3");
      g.icon.setAttribute("opacity", p < 1 ? 1 : .999); g.icon.querySelectorAll("path,circle,rect,polygon").forEach(el => el.setAttribute("fill", p < 1 ? "#FFFFFF" : "#A8A360"));
      if (p < 1) requestAnimationFrame(step); };
    requestAnimationFrame(step);
  };
  const hit = () => {                    // hit indicator: rotated toward the source, 0.70 to zero over 500 ms
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
  document.getElementById("hudPick")?.addEventListener("click", () => pickup("LARGE HEALTH", "health", () => { setHp(hp + 25); pop(health); }));
  document.getElementById("hudShard")?.addEventListener("click", () => pickup("ARMOR SHARD", "armorshard", () => { ap = Math.min(200, ap + 5); armor.set(ap, Math.min(1, ap / 100)); pop(armor); }));
  document.getElementById("hudLow")?.addEventListener("click", e => { e.stopImmediatePropagation(); setHp(hp < 26 ? 100 : 20); }, true);
  document.getElementById("hudNext")?.addEventListener("click", () => { do { sel = (sel + 1) % OWNED.length; } while (!OWNED[sel][2]); drawStrip(); showWeapon(); });
  document.getElementById("hudRadio")?.addEventListener("click", () => { radio = !radio; rhino = false; drawRadio(); });
  document.getElementById("hudRhino")?.addEventListener("click", () => { radio = true; rhino = true; drawRadio(); });   // each line restarts the shrink
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
