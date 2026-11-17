
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
  b.addEventListener("click", () => { color = c; sw.querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", x === b)); scene(); refreshGrids(); }); sw.appendChild(b); });
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
