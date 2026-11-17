
(() => {
const A = window.__atlas, { svgEl } = A;
if (!document.getElementById("crosshairs") || !A.CROSSHAIRS) return;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const X = A.CROSSHAIRS, M = A.CROSSHAIR_META || {};
const tint = (name, color) => A.uniqueIds((X[name]?.svg || "").replace(/#FFFFFF/gi, color));
const WEAPONS = [["crosshair_blaster", "Blaster", "Hyperblaster"], ["crosshair_machinegun", "Machinegun", "Vehicle guns"], ["crosshair_shotgun", "Shotgun", ""],
  ["crosshair_grenadelauncher", "Grenade launcher", "Dark matter gun"], ["crosshair_nailgun", "Nailgun", "Walker rockets"], ["crosshair_rocketlauncher", "Rocket launcher", ""],
  ["crosshair_railgun", "Railgun", ""], ["crosshair_lightninggun", "Lightning gun", "Hover-tank cannon"], ["crosshair_napalm", "Napalm launcher", ""], ["crosshair_gauntlet", "Gauntlet", "Multiplayer"]];
const CYCLE = ["crosshair_blaster", "crosshair_grenadelauncher", "crosshair_lightninggun", "crosshair_machinegun", "crosshair_nailgun", "crosshair_railgun", "crosshair_rocketlauncher", "crosshair_shotgun",
  "crosshair_q3_1", "crosshair_q3_2", "crosshair_q3_3", "crosshair_q3_4", "crosshair_q3_5", "crosshair_q3_6", "crosshair_q3_7", "crosshair_q3_8", "crosshair_q3_9", "crosshair_q3_10", "crosshair_gauntlet", "crosshair_napalm"];
const CONTEXT = [["useitem", "Use", null, "pulse"], ["crosstalk", "Talk", "#FF8000", "close"], ["cross_medic", "Talk · medic", null, "close"], ["cross_tech", "Talk · tech", null, "close"],
  ["crossgui3", "World GUI", "#FFCC00", "gui"], ["vehicle", "Enter vehicle", "#FFFF00", "vehicle"], ["cross_broken", "Locked vehicle", "#FF0000", "close"], ["crosshair_friendly", "Friendly", "#00FF00", "friendly"]];
const SWATCH = [["White", "#FFFFFF"], ["Red", "#FF0000"], ["Orange", "#FF8000"], ["Yellow", "#FFFF00"], ["Green", "#00FF00"], ["Cyan", "#00FFFF"], ["Blue", "#0000FF"], ["Pink", "#FF0080"]];
let current = "crosshair_machinegun", color = "#FFFFFF", sizeDp = 48, markers = false, sp = true;

/* Preview over a scene tile: dp map 1:1 to this 640x360 view's units at 100% (one unit = 2 dp here, so halve). */
const prev = document.getElementById("xhPrev");
const scene = () => {
  prev.innerHTML = "";
  const d = svgEl("defs", {}, prev), g = svgEl("linearGradient", { id: "xhSky", x1: 0, y1: 0, x2: 0, y2: 1 }, d);
  [[0, "#56606a"], [.55, "#2c3136"], [1, "#15181b"]].forEach(([o, c]) => svgEl("stop", { offset: o, "stop-color": c }, g));
  svgEl("rect", { width: 640, height: 360, fill: "url(#xhSky)" }, prev);
  svgEl("path", { d: "M0 250L140 190L260 214L380 160L520 206L640 180V360H0Z", fill: "#1d2124" }, prev);
  svgEl("path", { d: "M300 150h40v60h-40z", fill: "#3b4247" }, prev); svgEl("path", { d: "M306 158h28v12h-28z", fill: "#7fa0b0", "fill-opacity": .35 }, prev);
  const holder = svgEl("g", { id: "xhHold" }, prev); drawPreview(holder, sizeDp, color);
};
function drawPreview(holder, dp, col) {
  holder.innerHTML = "";
  const u = dp / 2;                                   // this preview draws 2 dp per unit
  const s = svgEl("svg", { x: 320 - u / 2, y: 180 - u / 2, width: u, height: u, viewBox: "0 0 64 64", overflow: "visible" }, holder);
  s.innerHTML = tint(current, col);
  if (markers) {                                      // hit markers on the diagonals, outside the art: from half the size plus 4 dp
    const r0 = u / 2 + 2, len = 3;
    const m = svgEl("g", { class: "xhMarks", opacity: 0 }, holder);
    [[1, 1], [1, -1], [-1, 1], [-1, -1]].forEach(([sx, sy]) => svgEl("path", { d: `M${320 + sx * r0 * .707} ${180 + sy * r0 * .707}l${sx * len} ${sy * len}`, stroke: "#fff", "stroke-width": 1.2, "stroke-linecap": "round" }, m));
  }
}
const info = () => { const m = M[current] || {}; document.getElementById("xhInfo").textContent = `${current.replace(/_/g, " ")} · ${m.uses || ""}`; };
scene(); info();

/* Size slider, swatches, hit feedback. */
const sizeIn = document.getElementById("xhSize"), sizeV = document.getElementById("xhSizeV");
sizeIn?.addEventListener("input", () => { sizeDp = Number(sizeIn.value); sizeV.textContent = `${sizeDp} dp${[24, 36, 48, 60, 72].includes(sizeDp) ? " · stock " + sizeDp / 1.5 + " u" : ""}`; scene(); });
const sw = document.getElementById("xhSw");
SWATCH.forEach(([name, c]) => { const b = document.createElement("button"); b.style.setProperty("--c", c); b.title = name; b.setAttribute("aria-label", name); b.setAttribute("aria-pressed", c === color);
  b.addEventListener("click", () => { color = c; A.animatedCrosshairColor?.(c); sw.querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", x === b)); scene(); refreshGrids(); }); sw.appendChild(b); });
document.getElementById("xhHit")?.addEventListener("click", () => {
  const t0 = performance.now(), hold = document.getElementById("xhHold");
  const step = now => { const t = Math.max(0, now - t0), p = Math.min(1, t / 150);
    drawPreview(hold, reduce ? sizeDp : sizeDp + 15 * (1 - p), sp && t < 100 ? "#FF0000" : color);
    const m = hold.querySelector(".xhMarks"); if (m) m.setAttribute("opacity", t < 150 ? 1 : 0);
    if (t < 160) requestAnimationFrame(step); else drawPreview(hold, sizeDp, color); };
  requestAnimationFrame(step);
});
const toggle = (id, fn) => { const b = document.getElementById(id); b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); fn(on); scene(); }, true); };
toggle("xhMarkers", on => { markers = on; }); toggle("xhSP", on => { sp = on; });

/* Grids. */
function tile(host, name, title, sub, { num, tintCol, anim } = {}) {
  const f = document.createElement("figure"); f.tabIndex = 0; f.setAttribute("role", "button"); f.setAttribute("aria-pressed", name === current);
  f.dataset.name = name;
  const s = svgEl("svg", { viewBox: "0 0 64 64", "aria-hidden": "true" });
  const paint = (col = tintCol) => {
    if (name === "vehicle") { s.innerHTML = `<svg x="4" y="18" width="56" height="41" viewBox="0 0 64 64" preserveAspectRatio="none" overflow="visible" class="xv-tri">${tint("crossvehicle1", col)}</svg><svg x="4" y="12" width="56" height="41" viewBox="0 0 64 64" preserveAspectRatio="none" overflow="visible" class="xv-chev">${tint("crossvehicle2", col)}</svg>`; return; }
    s.innerHTML = tint(name, col || color);
  };
  paint(); f.paint = paint;
  const cap = document.createElement("figcaption"); cap.innerHTML = `<b>${title}</b>${sub || ""}`;
  if (num) { const n = document.createElement("span"); n.className = "num"; n.textContent = num; f.appendChild(n); }
  f.append(s, cap); host.appendChild(f);
  const pick = () => { if (anim) play(s, anim); if (!CYCLE.includes(name)) return;
    current = name; document.querySelectorAll("#crosshairs figure").forEach(x => x.setAttribute("aria-pressed", x.dataset.name === current)); scene(); info(); };
  f.addEventListener("click", pick); f.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); pick(); } });
  return f;
}
/* Context-cursor motion, from cursor.gui (section 14.15). */
function play(s, kind) {
  if (reduce) return;
  const t0 = performance.now(), dur = kind === "pulse" ? 1200 : kind === "vehicle" ? 1500 : kind === "spin" ? 2400 : 400;
  const step = now => { const t = Math.max(0, now - t0);
    if (kind === "pulse") { const c = (t % 600) / 600, k = c < .5 ? c * 2 : 2 - c * 2; s.style.transform = `scale(${1 + k * (28 / 24 - 1)})`; }
    if (kind === "close") { const p = Math.min(1, t / 200); s.style.transform = `scale(${(34 - 12 * p) / 22})`; s.style.opacity = p; }
    if (kind === "friendly") { const p = Math.min(1, t / 200); s.style.transform = `scale(${(40 - 6 * p) / 34})`; }
    if (kind === "gui") { s.style.opacity = t < 50 ? .5 : 1; }
    if (kind === "spin") s.style.transform = `rotate(${(t * .3) % 360}deg)`;                    // the lock guide turns
    if (kind === "vehicle") { const c = t % 750, p = Math.min(1, c / 500), ch = s.querySelector(".xv-chev"), tr = s.querySelector(".xv-tri");
      ch?.setAttribute("y", 12 - 7 * 1.4 * p); ch?.setAttribute("opacity", 1 - p); tr?.setAttribute("opacity", 1 - .6 * Math.min(1, c / 750)); }
    if (t < dur) requestAnimationFrame(step); else { s.style.transform = ""; s.style.opacity = ""; s.querySelector(".xv-chev")?.setAttribute("y", 12); s.querySelector(".xv-chev")?.setAttribute("opacity", 1); s.querySelector(".xv-tri")?.setAttribute("opacity", 1); } };
  s.style.transformOrigin = "50% 50%"; requestAnimationFrame(step);
}
const W = document.getElementById("xhWeapons"), C = document.getElementById("xhCustom"), K = document.getElementById("xhContext");
WEAPONS.forEach(([n, t, sub]) => tile(W, n, t, sub));
const TITLE = Object.fromEntries(WEAPONS.map(([n, t]) => [n, t]));
CYCLE.forEach((n, i) => tile(C, n, TITLE[n] || n.replace("crosshair_q3_", "mark "), i === 9 || i === 15 ? "same art as " + (i === 9 ? "16" : "10") : "", { num: i + 1 }));
CONTEXT.forEach(([n, t, col, anim]) => tile(K, n, t, col ? col : "own colors", { tintCol: col || null, anim }));
function refreshGrids() { [W, C].forEach(h => h.querySelectorAll("figure").forEach(f => f.paint())); }
/* The Awakening (section 14.15): the space cannon's reticule by lock state, its guide effects and two new cursors. */
const AW = document.getElementById("xhAwakening");
if (AW) [["aa_space_default", "Space cannon", "#FFFFFF", "nothing targeted"], ["aa_space_acquire", "Acquiring", "#FFFF00", "rockets"],
  ["aa_space_lock", "Locked", "#40FF40", "rockets"], ["aa_space_friendly", "Friendly", "#FF1A1A", "hold fire"], ["aa_space_oor", "Out of range", "#FFFFFF", "side bars off"],
  ["nailgun_scope_lock", "Acquiring guide", "#F8C001", "stock ring, turning", "spin"], ["aa_space_lock_fx", "Lock guide", "#E60000", "turns on a lock", "spin"],
  ["nailgun_scope_lock", "Nearest guide", "#808080", "nearest enemy", "spin"], ["aa_space_oor_fx", "Range guide", "#BABABA", "Remastered only"],
  ["crosstalk_wait", "Talk · busy", null, "own colors"], ["vehicle_cannon", "Vehicle cannon", "#FFFFFF", "openQ4 design"]]
  .forEach(([n, t, col, sub, anim]) => { if (X[n]) tile(AW, n, t, sub, { tintCol: col, anim }); });
})();

/* The animated crosshair (hud_crosshairAnimated, on by default under ui_retained 1; src/game/gamesys/AnimatedCrosshair.inl).
   Each design comes apart into the pieces it was drawn from and answers the weapon: it assembles as the weapon comes up,
   kicks on a shot, closes as it charges, waits while it cycles and stands down while it reloads; hit feedback docks into
   it. A port of the drawing code: master units are the 64-unit design, its centre the aim point. */
(() => {
const A = window.__atlas, { svgEl } = A;
const stage = document.getElementById("xhAnim");
if (!stage || !A.CROSSHAIRS) return;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const DIAG = .70710678, HALF = 32;
/* Clear space per design: hit dock and scale, gauge radius, outer edge, kick strength. */
const STYLE = { generic: [17, .9, 20, 16, 1], blaster: [14.5, 1, 16.5, 13, 1], machinegun: [14.5, .8, 16, 19, .8], shotgun: [9.5, .9, 11, 7.5, 1.5],
  grenade: [5.5, .62, 13.5, 22, 1.2], nailgun: [5, .6, 11.5, 30, .5], rocket: [5, .55, 25.5, 29, 1.3], railgun: [8.5, .85, 20, 17.5, 1.4],
  lightning: [0, 1, 10, 28, .6], napalm: [6, .6, 13.5, 24.5, 1], gauntlet: [10.5, .85, 12.5, 8.5, .6], cannon: [5, .6, 12.5, 23, 1.5] };
/* The weapons, with their stock fire rate and clip (def/weapons/*.def). charge: s to full; spin: reports barrel speed. */
const WEAPONS = [
  ["blaster", "Blaster", "crosshair_blaster", { interval: 150, charge: .8 }],
  ["hyperblaster", "Hyperblaster", "crosshair_blaster", { interval: 80, clip: 60, spin: true, design: "blaster" }],
  ["machinegun", "Machinegun", "crosshair_machinegun", { interval: 100, clip: 40 }],
  ["shotgun", "Shotgun", "crosshair_shotgun", { interval: 800, clip: 8 }],
  ["grenade", "Grenade launcher", "crosshair_grenadelauncher", { interval: 800, clip: 8 }],
  ["nailgun", "Nailgun", "crosshair_nailgun", { interval: 160, clip: 50, spin: true, lock: true }],
  ["rocket", "Rocket launcher", "crosshair_rocketlauncher", { interval: 800, clip: 1 }],
  ["railgun", "Railgun", "crosshair_railgun", { interval: 1600, clip: 3 }],
  ["lightning", "Lightning gun", "crosshair_lightninggun", { interval: 50 }],
  ["napalm", "Napalm launcher", "crosshair_napalm", { interval: 800, clip: 20 }],
  ["gauntlet", "Gauntlet", "crosshair_gauntlet", { interval: 400 }],
  ["cannon", "Vehicle cannon", "vehicle_cannon", { interval: 1000, lock: true }],
  ["generic", "Custom mark", "crosshair_q3_5", { interval: 100, clip: 40 }]];
/* Hit marker tiers (HitMarker.cpp): duration, hold, alpha, spread, length, width scales; punch. */
const TIERS = { normal: [240, .28, .85, 1, 1, 1, .7], kill: [420, .38, 1, 1.7, 1.35, 1.25, 1.25] };
const HIT = [.95, .17, .14], KILL = [1, .88, .82], ACQUIRE = [.97, .75, 0], LOCKED = [.9, .1, .05];

const easeOut = x => 1 - (1 - Math.min(1, Math.max(0, x))) ** 3;
const easeInOut = x => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); };
const easeBack = x => { const r = Math.min(1, Math.max(0, x)) - 1; return 1 + r * r * (2.70158 * r + 1.70158); };
const approach = (v, t, s) => v < t ? Math.min(t, v + s) : Math.max(t, v - s);
const mixc = (a, b, f) => a.map((v, i) => v + ((b[i] ?? v) - v) * f);
const rgb = c => `rgb(${c.slice(0, 3).map(v => Math.round(Math.min(1, Math.max(0, v)) * 255)).join(",")})`;
const hex = h => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255);
const flicker = (slot, time) => { let h = (Math.floor(time / 33) * 2654435761 + slot * 40503) >>> 0; h ^= h >>> 13; h = Math.imul(h, 2246822519) >>> 0; h ^= h >>> 16; return (h & 0xffff) / 32767.5 - 1; };
const bearing = d => { const a = d * Math.PI / 180; return [Math.sin(a), -Math.cos(a)]; };

/* The canvas: parallelograms, arcs, discs and windows of the design's own art. */
function canvas(g, tint, artId) {
  const col = c => ({ fill: rgb(c), "fill-opacity": Math.max(0, Math.min(1, c[3])).toFixed(3) });
  const c = {
    ink: (f, a) => { let k = [...tint.slice(0, 3), a * f.alpha]; if (f.hitMix > 0) k = [...mixc(k.slice(0, 3), f.hitColor, f.hitMix), k[3]]; return k; },
    shade: (f, a) => [0, 0, 0, a * f.alpha],
    box([cx, cy], [ax, ay], hl, hw, color) {
      if (color[3] <= .004 || hl <= 0) return; hw = Math.max(hw, .25);
      const lx = ax * hl, ly = ay * hl, wx = -ay * hw, wy = ax * hw;
      svgEl("path", { d: `M${cx - lx - wx} ${cy - ly - wy}L${cx + lx - wx} ${cy + ly - wy}L${cx + lx + wx} ${cy + ly + wy}L${cx - lx + wx} ${cy - ly + wy}Z`, ...col(color) }, g);
    },
    line(a, b, w, color) { const dx = b[0] - a[0], dy = b[1] - a[1], L = Math.hypot(dx, dy); if (L < .001) return; c.box([(a[0] + b[0]) / 2, (a[1] + b[1]) / 2], [dx / L, dy / L], L / 2, w / 2, color); },
    spoke(b, r0, r1, w, color) { if (r1 <= r0) return; const d = bearing(b); c.box([d[0] * (r0 + r1) / 2, d[1] * (r0 + r1) / 2], d, (r1 - r0) / 2, w / 2, color); },
    arc(r, t, from, to, color) {
      const sweep = to - from; if (color[3] <= .004 || sweep <= .05 || r <= 0) return;
      const st = { fill: "none", stroke: rgb(color), "stroke-opacity": color[3].toFixed(3), "stroke-width": t };
      if (sweep >= 359.9) { svgEl("circle", { r, ...st }, g); return; }
      const [x0, y0] = bearing(from), [x1, y1] = bearing(to);
      svgEl("path", { d: `M${x0 * r} ${y0 * r}A${r} ${r} 0 ${sweep > 180 ? 1 : 0} 1 ${x1 * r} ${y1 * r}`, ...st }, g);
    },
    disc([x, y], r, color) { if (r > 0 && color[3] > .004) svgEl("circle", { cx: x, cy: y, r, ...col(color) }, g); },
    /* One window of the art (x0..x1, y0..y1 from the centre), turned and scaled about the aim point, then moved. */
    art(x0, y0, x1, y1, deg, scale, [ox, oy], color) {
      if (color[3] <= .004 || scale <= 0) return;
      const t = svgEl("g", { transform: `translate(${ox} ${oy}) rotate(${deg}) scale(${scale})` }, g);
      const id = "xhw" + (++canvas.n);
      svgEl("rect", { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }, svgEl("clipPath", { id }, t));
      svgEl("use", { href: "#" + artId, color: rgb(color), opacity: color[3].toFixed(3), "clip-path": `url(#${id})` }, t);
    },
  };
  return c;
}
canvas.n = 0;

const D = {
  blaster(c, f) {
    const ch = easeInOut(f.charge), halfGap = Math.min(40, Math.max(0, 4.8 * (1 - ch) + 7 * Math.max(0, f.kick) + 9 * f.standby));
    const half = (45 - halfGap) * f.slide, turn = -90 * (1 - f.slide) + 90 * ch + f.spinPhase + 45 * easeInOut(f.standby);
    const r = 1 + .55 * (1 - f.slide) + .14 * f.kick - .12 * ch - .1 * f.punch + .06 * f.spin + .1 * f.standby;
    for (let i = 0; i < 4; i++) { const m = 45 + 90 * i + turn; c.arc(9.1 * r, 3.2, m - half, m + half, c.shade(f, .66)); c.arc(11.7 * r, 2, m - half, m + half, c.ink(f, 1)); }
    if (f.charge > .97) { const br = f.pulse * (.5 + .5 * Math.sin(f.seconds * 12)); c.arc(13.9 + .7 * br * f.motion, .9, 0, 360, c.ink(f, .45 + .25 * br)); }
    c.disc([0, 0], 3 * f.seat, c.shade(f, .45)); c.disc([0, 0], 1.15 * f.seat, c.ink(f, 1));
  },
  machinegun(c, f) {
    const r = 1 + .05 * f.kick - .05 * f.punch - .16 * f.zoom + .06 * f.standby, sweep = 360 * f.slide;
    c.arc(9.25 * r, 3.5, 0, sweep, c.shade(f, .79)); c.arc(12 * r, 2, 0, sweep, c.ink(f, 1));
    const tr = 3 * f.kick + 2.4 * f.fire - 3.5 * f.zoom + 12 * (1 - f.slide), len = 12 * (1 - .45 * f.standby), turn = 45 * easeInOut(f.empty);
    for (let i = 0; i < 4; i++) c.spoke(90 * i + turn, 7 + tr, 7 + tr + len, 2, c.ink(f, f.open));
    c.disc([0, 0], 1.05 * f.seat, c.ink(f, 1));
  },
  shotgun(c, f) {
    const r = f.seat * (1 + .55 * f.kick + .12 * f.standby) - .1 * f.punch;
    c.arc(4 * r, 2, 0, 360, c.shade(f, .8)); c.arc(6 * r, 2, 0, 360, c.ink(f, 1));
    if (f.shotAge >= 0 && f.shotAge < 380 && f.motion > .5) { const a = f.shotAge / 380; c.arc(7.5 + 11 * easeOut(a), 1, 0, 360, c.ink(f, .5 * (1 - a) ** 2)); }
  },
  rocket(c, f) {
    const r = 1 + .05 * f.kick - .04 * f.punch + .04 * f.standby, h = 180 * f.slide;
    c.arc(21.2 * r, .9, -h, h, c.shade(f, .45)); c.arc(17 * r, 4, -h, h, c.shade(f, .78)); c.arc(20 * r, 2, -h, h, c.ink(f, 1));
    const tr = 3 * f.kick + 10 * (1 - f.slide), turn = 45 * easeInOut(f.empty);
    for (let i = 0; i < 4; i++) c.spoke(90 * i + turn, 21 + tr, 29 + tr, 2, c.ink(f, f.open));
    if (f.reload < .02) { if (f.cycle < 1) c.arc(25.5, 1.1, 0, 360 * f.cycle, c.ink(f, .55)); else c.arc(25.5, 1.1, 0, 360 * f.slide, c.ink(f, .14 + .5 * f.ready)); }
    c.disc([0, 0], 1.05 * f.seat, c.ink(f, .7));
  },
  railgun(c, f) {
    const w = 1 - f.cycle, tr = 7 * w * Math.sqrt(w) + 2.5 * f.kick + 14 * (1 - f.slide) - 1.5 * f.punch + 2 * f.standby;
    const inner = 7 + tr, outer = inner + 10 + 2 * f.ready, turn = 45 * easeInOut(f.empty) + 45 * easeInOut(f.zoom);
    const ink = mixc(c.ink(f, f.open), [1, 1, 1, f.open * f.alpha], .6 * f.ready);
    for (let i = 0; i < 4; i++) { const b = 90 * i + turn;
      c.spoke(b, 3, inner, 1, c.shade(f, .45 * f.open)); c.spoke(b, inner - 1, outer + 1, 4, c.shade(f, .52 * f.open)); c.spoke(b, inner, outer, 2, ink); }
    const arm = 3 * f.seat;
    c.box([0, 0], [1, 0], arm + .9, 1.9, c.shade(f, .5)); c.box([0, 0], [0, 1], arm + .9, 1.9, c.shade(f, .5));
    c.box([0, 0], [1, 0], arm, 1, c.ink(f, 1)); c.box([0, 0], [0, 1], arm, 1, c.ink(f, 1));
  },
  gauntlet(c, f) {
    c.disc([0, 0], 8 * f.seat, c.shade(f, .9)); c.disc([0, 0], 3 * f.seat * (1 + .35 * f.punch), c.ink(f, .63 + .37 * f.hitMix));
    if (f.spin > .01) { const r = 11.5 + 1.5 * f.punch; for (let i = 0; i < 3; i++) { const a = f.spinPhase + 120 * i; c.arc(r, 1.6, a, a + 62, c.ink(f, .9 * f.spin)); } }
  },
  grenade(c, f) {
    const w = 1 - f.cycle, ch = easeInOut(f.charge), sc = 1 - .24 * ch - .05 * f.punch;
    const apart = 3.2 * w + 4 * f.reload + 1.5 * f.empty, breech = 2.6 * f.kick, away = 12 * (1 - f.slide), turn = 32 * (1 - f.slide) + 45 * ch, ink = c.ink(f, f.open);
    for (let i = 0; i < 4; i++) { const sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1;
      c.art(Math.min(0, sx * HALF), Math.min(0, sy * HALF), Math.max(0, sx * HALF), Math.max(0, sy * HALF), turn, sc, [sx * (apart + away * DIAG), sy * (breech + away * DIAG)], ink); }
  },
  nailgun(c, f) {
    const locked = easeInOut(Math.min(1, Math.max(0, (f.lock - .5) * 2)));
    const seeking = f.lock > .02 && f.lock < .98 ? .8 * Math.sin(f.seconds * 18) * f.motion : 0;
    const tremble = f.spin > .6 ? .35 * (f.spin - .6) * 2.5 * Math.sin(f.seconds * 190) * f.motion : 0;
    const apart = Math.max(-6.2, 11 * (1 - f.slide) - 4.5 * f.spin - 6.2 * locked + 1.2 * f.kick + 5 * f.reload + 2 * f.empty - 1.2 * f.punch + seeking + tremble);
    let ink = c.ink(f, f.open);
    if (f.lock > .02) ink = [...mixc(ink.slice(0, 3), locked > .5 ? LOCKED : ACQUIRE, Math.min(1, f.lock * 2) * .85), ink[3]];
    c.art(-HALF, -HALF, 0, HALF, 0, 1, [-apart, 0], ink); c.art(0, -HALF, HALF, HALF, 0, 1, [apart, 0], ink);
    if (locked > .02) { const pip = [...LOCKED, f.alpha * locked], reach = 6 + 5 * (1 - locked) + 2.5 * f.lockFlash;
      for (let i = 0; i < 4; i++) { c.spoke(45 + 90 * i, reach - .6, reach + 3.6, 2.6, [0, 0, 0, .5 * pip[3]]); c.spoke(45 + 90 * i, reach, reach + 3, 1.4, pip); } }
  },
  lightning(c, f) {
    const w = 1 - f.cycle, tr = 15 * (1 - f.slide) - 3.5 * f.fire - 5 * f.punch + 9 * w * Math.sqrt(w) + 2 * f.kick + 4 * f.standby;
    let ink = [...c.ink({ ...f, hitMix: 0 }, f.open)];
    if (f.hitMix > 0) ink = [...mixc(ink.slice(0, 3), f.hitColor, Math.min(1, f.hitMix * 1.15)), ink[3]];
    for (let i = 0; i < 4; i++) { const sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1, d = (tr + .55 * f.fire * f.motion * flicker(i, f.time)) * DIAG;
      c.art(Math.min(0, sx * HALF), Math.min(0, sy * HALF), Math.max(0, sx * HALF), Math.max(0, sy * HALF), 0, 1, [sx * d, sy * d], ink); }
  },
  cannon(c, f) {
    const w = 1 - f.cycle, ink = c.ink(f, f.open), spread = (2.2 * f.kick + 3 * w * Math.sqrt(w) + 2 * f.standby) * DIAG, turn = 45 * (1 - f.slide), tick = 8 * (1 - f.slide) + 3 * f.kick;
    for (let i = 0; i < 4; i++) { const sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1;
      c.art(sx > 0 ? 1.3 : -24, sy > 0 ? 1.3 : -24, sx > 0 ? 24 : -1.3, sy > 0 ? 24 : -1.3, turn, 1 - .04 * f.punch, [sx * spread, sy * spread], ink); }
    c.art(-1.3, -24, 1.3, -15, 0, 1, [0, -tick], ink); c.art(-1.3, 15, 1.3, 24, 0, 1, [0, tick], ink);
    c.art(-24, -1.3, -15, 1.3, 0, 1, [-tick, 0], ink); c.art(15, -1.3, 24, 1.3, 0, 1, [tick, 0], ink);
    c.art(-3, -3, 3, 3, 0, 1, [0, 0], ink); c.art(-6, 3.2, 6, 14, 0, 1, [0, 4 * f.kick + 5 * w], c.ink(f, f.open * (1 - .6 * w)));
  },
  napalm(c, f) {
    const w = 1 - f.cycle, sp = w * Math.sqrt(w), sc = (1 + .4 * (1 - f.slide)) * (1 + .1 * f.kick + .16 * sp * f.motion - .06 * f.punch + .06 * f.standby);
    c.art(-HALF, -HALF, HALF, HALF, -60 * (1 - f.slide) + 9 * f.kick + 90 * sp * f.motion, sc, [0, 0], c.ink(f, f.open * (1 - .35 * w)));
  },
  generic(c, f) {
    const sc = (1 + .4 * (1 - f.slide)) * (1 + .1 * f.kick - .06 * f.punch + .06 * f.standby);
    c.art(-HALF, -HALF, HALF, HALF, 0, sc, [0, 0], c.ink(f, f.open));
  },
};
/* Overlays: the aim point a folded design leaves, the reload gauge, a vehicle gun's lock corners, the docked hit. */
function overlays(c, f, design, sig, st) {
  const s = STYLE[design];
  if (f.open > 0 && f.reload > .02 && s[2] > 0) {
    const a = f.reload * f.open, lit = [...c.ink({ ...f, hitMix: 0, alpha: 1 }, .95 * a)], dark = [0, 0, 0, .5 * a];
    if (sig.clipSize >= 2 && sig.clipSize <= 12 && sig.clip >= 0) {
      for (let i = 0; i < sig.clipSize; i++) { const b = 360 * (i + .5) / sig.clipSize, pop = i === sig.clip - 1 ? 1.6 * st.clipPop : 0;
        c.spoke(b, s[2] - 1.7, s[2] + 1.7 + pop, 2.8, dark); c.spoke(b, s[2] - 1.1, s[2] + 1.1 + pop, 1.6, i < sig.clip ? lit : [...lit.slice(0, 3), .3 * a]); }
    } else [0, 180].forEach(o => { c.arc(s[2], 2.4, st.busy + o, st.busy + o + 110, dark); c.arc(s[2], 1.2, st.busy + o, st.busy + o + 110, lit); });
  }
  if (f.open > 0 && design !== "nailgun" && f.lock > .02) {
    const reach = s[3] + 3 + 7 * (1 - easeOut(f.lock)) + 2 * f.lockFlash, ink = [...(f.lock > .98 ? LOCKED : ACQUIRE), f.alpha * f.open * Math.min(1, f.lock * 2)];
    for (let i = 0; i < 4; i++) { const [x, y] = bearing(45 + 90 * i).map(v => v * reach), hx = x > 0 ? -1 : 1, vy = y > 0 ? -1 : 1;
      c.line([x, y], [x + hx * 4.5, y], 1.4, ink); c.line([x, y], [x, y + vy * 4.5], 1.4, ink); }
  }
  if (f.hitAlpha > 0) {
    const color = [...f.hitColor, f.hitAlpha];
    if (f.kill) c.arc(s[3] * easeOut(f.open) + 2 + 9 * f.hitExpansion, 1.3 * (1 - .5 * f.hitExpansion), 0, 360, [...f.hitColor, f.hitAlpha * .8]);
    if (f.marks) {
      let dock = s[0], sc = s[1];
      if (dock <= 0) { dock = 9; sc = .9; color[3] *= 1 - f.open; } else { const k = easeInOut(f.open); dock = 9 + (dock - 9) * k; sc = .9 + (sc - .9) * k; }
      if (color[3] > .004) { const t = TIERS[f.tier], size = sc * 2, inner = dock + 3.4 * t[3] * f.hitExpansion * size;
        const len = 6.4 * t[4] * (1 - .25 * f.hitExpansion) * size, wid = 1.7 * t[5] * (1 - .2 * f.hitExpansion) * size, sh = .75 * 2 * sc;
        for (let i = 0; i < 4; i++) { const b = 45 + 90 * i; c.spoke(b, inner - sh, inner + len + sh, wid + 2 * sh, [0, 0, 0, color[3] * .55]); c.spoke(b, inner, inner + len, wid, color); } }
    }
  }
  const aim = f.aim * (1 - easeInOut(f.open));
  if (aim > .004) { c.disc([0, 0], 2.45, [0, 0, 0, .5 * aim]); c.disc([0, 0], 1.15, [...c.ink({ ...f, hitMix: 0, alpha: 1 }, aim)]); }
}

/* The live stage: the eased state follows the weapon's signals, as XH_Advance does. */
let weapon = WEAPONS[2], color = [1, 1, 1], marks = false;
const artDefs = svgEl("defs", {}, stage);
const artIds = {};
const artFor = name => { if (artIds[name]) return artIds[name]; const id = "xhArt-" + name.replace(/\W/g, "");
  const s = svgEl("svg", { id, x: -32, y: -32, width: 64, height: 64, viewBox: "0 0 64 64", overflow: "visible" }, artDefs);
  s.innerHTML = A.uniqueIds((A.CROSSHAIRS[name]?.svg || "").replace(/#FFFFFF/gi, "currentColor")); return artIds[name] = id; };
const g = svgEl("g", {}, stage);
const sig = { raised: true, firing: false, reloading: false, empty: false, zoomed: false, next: 0, clip: -1, clipSize: 0, charge: -1, spin: 0, lock: 0 };
const st = { deploy: 0, aim: 1, kick: 0, kv: 0, shotT: 0, shotS: -1e9, interval: 0, cycling: false, ready: 0, fire: 0, reload: 0, empty: 0, zoom: 0,
  charge: 0, lastCharge: 0, spin: 0, lock: 0, lockFlash: 0, spinPhase: 0, busy: 0, lastClip: -1, clipPop: 0, seconds: 0, design: null, swapTo: null, hit: null };
let last = performance.now(), chargeHeld = false, lockT = 0, reloadT = 0;
const designOf = w => w[3].design || w[0];
function shoot(now) { const iv = weapon[3].interval; sig.next = now + iv;
  st.shotT = now; st.shotS = st.seconds; st.interval = iv; st.cycling = iv >= 450; st.kv += 56 * STYLE[designOf(weapon)][4] * (reduce ? .3 : 1);
  if (sig.clip > 0) sig.clip--;
  if (sig.clip === 0 && sig.clipSize > 0) setTimeout(() => { if (!sig.empty && sig.clip === 0) { sig.reloading = true; reloadT = performance.now(); } }, iv); }   // an empty clip reloads
function frame(now) {
  let dt = Math.min(.25, Math.max(0, (now - last) / 1000)); last = now; st.seconds += dt;
  const w = weapon[3], want = designOf(weapon);
  /* signals */
  if (sig.firing && !sig.reloading && now >= sig.next && sig.clip !== 0 && !sig.empty) shoot(now);
  if (chargeHeld) sig.charge = Math.min(1, sig.charge + dt / (w.charge || 1));
  if (sig.reloading) { const t = (now - reloadT) / 1500;
    if (sig.clipSize >= 2 && sig.clipSize <= 12) sig.clip = Math.min(sig.clipSize, Math.floor(t * sig.clipSize) + 0);
    if (t >= 1) { sig.reloading = false; sig.clip = sig.clipSize || -1; } }
  if (w.spin) sig.spin = approach(sig.spin, sig.firing ? 1 : 0, dt / .6);
  if (w.lock && lockT) sig.lock = Math.min(1, (now - lockT) / 1200); else if (!lockT) sig.lock = 0;
  /* deploy: a different weapon folds the old design before the new one assembles */
  if (st.design !== want) { st.deploy = st.design ? approach(st.deploy, 0, dt / .07) : 0;
    if (st.deploy <= 0) { Object.assign(st, { design: want, kick: 0, kv: 0, cycling: false, charge: 0, spin: 0, lock: 0, spinPhase: 0, shotS: -1e9 }); st.material = weapon[2]; } }
  else st.deploy = sig.raised ? approach(st.deploy, 1, dt / .26) : approach(st.deploy, 0, dt / .11);
  if (st.cycling && now >= sig.next) { st.cycling = false; st.ready = reduce ? 0 : 1; }
  for (let left = dt; left > 0; left -= .004) { const h = Math.min(left, .004); st.kv += (-900 * st.kick - 2 * .52 * 30 * st.kv) * h; st.kick += st.kv * h; }
  st.kick = Math.min(2.2, Math.max(-.5, st.kick));
  st.fire = approach(st.fire, sig.firing ? 1 : 0, dt / (sig.firing ? .08 : .25));
  st.empty = approach(st.empty, sig.empty ? 1 : 0, dt / .2); st.zoom = approach(st.zoom, sig.zoomed ? 1 : 0, dt / .15);
  st.spin = approach(st.spin, st.design === "gauntlet" ? (sig.firing ? 1 : 0) : sig.spin, dt / .12);
  const was = st.reload; st.reload = approach(st.reload, sig.reloading ? 1 : 0, dt / (sig.reloading ? .15 : .2));
  if (!sig.reloading && was >= 1 && !sig.empty && !reduce) st.ready = 1;
  st.ready = approach(st.ready, 0, dt / .28); st.busy = (st.busy + 300 * dt * (reduce ? .4 : 1)) % 360;
  if (sig.clip !== st.lastClip) { if (sig.clip > st.lastClip && sig.reloading) st.clipPop = 1; st.lastClip = sig.clip; }
  st.clipPop = approach(st.clipPop, 0, dt / .18);
  const charge = Math.max(0, sig.charge);
  if (st.lastCharge > .2 && charge <= 0) { st.shotT = now; st.shotS = st.seconds; st.kv += 56 * STYLE[st.design][4] * (1 + st.lastCharge) * (reduce ? .3 : 1); }
  st.lastCharge = charge; st.charge = approach(st.charge, charge, dt / (charge > st.charge ? .05 : .14));
  const wasL = st.lock; st.lock = approach(st.lock, sig.lock, dt / .12); if (st.lock >= 1 && wasL < 1 && !reduce) st.lockFlash = 1; st.lockFlash = approach(st.lockFlash, 0, dt / .22);
  const sym = st.design === "gauntlet" ? 120 : 90, spd = st.design === "gauntlet" ? 620 : 300;
  if (reduce) st.spinPhase = 0; else if (st.spin > .02) st.spinPhase = (st.spinPhase + spd * st.spin * dt) % sym;
  else if (st.spinPhase > 0) { st.spinPhase += Math.max(40, spd * .25) * dt; if (st.spinPhase >= sym) st.spinPhase = 0; }
  /* the frame */
  const since = Math.max(0, st.seconds - st.shotS) * 1000, standby = Math.max(st.reload, st.empty);
  const f = { open: st.deploy, slide: reduce ? 1 : easeOut(st.deploy), seat: reduce ? 1 : easeBack(st.deploy), kick: st.kick, fire: st.fire, reload: st.reload, empty: st.empty,
    standby, zoom: st.zoom, charge: st.charge, spin: st.spin, spinPhase: st.spinPhase, lock: st.lock, lockFlash: st.lockFlash, ready: st.ready, time: now, seconds: st.seconds,
    shotAge: st.shotS > -1e8 ? since : -1, alpha: (reduce ? st.deploy : 1) * (1 - .45 * standby), aim: 1, motion: reduce ? 0 : 1, pulse: reduce ? 0 : 1,
    cycle: st.cycling && st.interval > 0 ? Math.min(1, since / st.interval) : 1, hitColor: HIT, hitMix: 0, hitAlpha: 0, punch: 0, marks, tier: "normal", kill: false, hitExpansion: 0 };
  if (st.hit) { const t = TIERS[st.hit.tier], life = (now - st.hit.t0) / t[0];
    if (life >= 1 || life < 0) st.hit = null;
    else { const fade = life <= t[1] ? 1 : Math.max(0, 1 - (life - t[1]) / (1 - t[1])) ** 2;
      Object.assign(f, { hitColor: st.hit.tier === "kill" ? KILL : HIT, tier: st.hit.tier, kill: st.hit.tier === "kill", hitExpansion: 1 - (1 - life) ** 3,
        hitAlpha: t[2] * fade, punch: (1 - life) ** 2 * t[6] * (reduce ? .3 : 1), hitMix: fade * t[2] * .9 });
      if (marks && STYLE[st.design][0] > 0) f.hitMix *= .3; } }
  g.innerHTML = "";
  const c = canvas(g, color, artFor(st.material || weapon[2]));
  if (st.deploy > 0) D[st.design](c, f);
  overlays(c, f, st.design, sig, st);
}
const loop = now => { if (stage.isConnected) { try { frame(now); } catch (e) { return; } requestAnimationFrame(loop); } };
requestAnimationFrame(loop);

/* Controls. */
const picker = document.getElementById("xhAnimWeapon"), acts = document.getElementById("xhAnimActs"), status = document.getElementById("xhAnimInfo");
const reset = () => { Object.assign(sig, { firing: false, reloading: false, empty: false, zoomed: false, charge: weapon[3].charge ? 0 : -1, spin: 0, lock: 0,
  clip: weapon[3].clip || -1, clipSize: weapon[3].clip || 0 }); chargeHeld = false; lockT = 0; sync(); };
function sync() {
  acts?.querySelectorAll("[data-a]").forEach(b => { const a = b.dataset.a, w = weapon[3];
    const can = a === "charge" ? !!w.charge : a === "lock" ? !!w.lock : a === "reload" ? !!w.clip : true;
    b.disabled = !can; b.style.opacity = can ? "" : ".35";
    if (b.hasAttribute("aria-pressed")) b.setAttribute("aria-pressed", a === "hold" ? sig.firing : a === "empty" ? sig.empty : a === "zoom" ? sig.zoomed : a === "charge" ? chargeHeld : a === "lock" ? !!lockT : a === "marks" ? marks : false); });
  if (status) status.textContent = `${weapon[1]} · ${weapon[2]} · fires every ${weapon[3].interval} ms${weapon[3].clip ? `, clip ${weapon[3].clip}` : ""}${weapon[3].charge ? `, charges in ${weapon[3].charge * 1000} ms` : ""}`;
}
if (picker) { picker.innerHTML = `<span class="lbl">Weapon</span>` + WEAPONS.map((w, i) => `<button class="seg" data-i="${i}" aria-pressed="${w === weapon}">${w[1]}</button>`).join("");
  picker.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; weapon = WEAPONS[+b.dataset.i];
    picker.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); reset(); }); }
acts?.addEventListener("click", e => { const b = e.target.closest("[data-a]"); if (!b || b.disabled) return; const now = performance.now();
  switch (b.dataset.a) {
    case "raise": st.deploy = 0; st.design = designOf(weapon); st.material = weapon[2]; break;
    case "fire": if (!sig.reloading && sig.clip !== 0 && !sig.empty) shoot(now); break;
    case "hold": sig.firing = !sig.firing; break;
    case "charge": chargeHeld = !chargeHeld; sig.charge = 0; break;   // letting go of a held charge is the shot
    case "reload": if (sig.clipSize) { sig.reloading = true; reloadT = now; sig.empty = false; if (sig.clipSize <= 12) sig.clip = 0; } break;
    case "empty": sig.empty = !sig.empty; if (sig.empty) { sig.firing = false; if (sig.clipSize) sig.clip = 0; } else if (sig.clipSize) sig.clip = sig.clipSize; break;
    case "zoom": sig.zoomed = !sig.zoomed; break;
    case "lock": lockT = lockT ? 0 : now; break;
    case "hit": st.hit = { tier: "normal", t0: now }; break;
    case "kill": st.hit = { tier: "kill", t0: now }; break;
    case "marks": marks = !marks; break;
  }
  sync(); });
reset();
A.animatedCrosshairColor = hexColor => { color = hex(hexColor); };
})();
