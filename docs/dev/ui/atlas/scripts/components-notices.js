(() => {
const { svgEl } = window.__atlas;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const OLIVE = "#8B964B", ORANGE = "#E38900", VALUE = "#FFBE23";

/* Notices (tools/ui/retained_notices.py): 400x30 dp header-band plates, top 14 dp, trailing 24 dp, 6 dp apart.
   Kinds: 1 the favorite's star, 2 a lost controller crossed out. */
const W = 400, H = 30, CUT = 8;
const plate = id => `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" aria-hidden="true"><defs>
  <linearGradient id="${id}f" x1="0" x2="${W}" gradientUnits="userSpaceOnUse"><stop offset=".5" stop-color="${OLIVE}" stop-opacity=".49"/><stop offset=".75" stop-color="${OLIVE}" stop-opacity=".245"/><stop offset=".95" stop-color="${OLIVE}" stop-opacity=".049"/><stop offset="1" stop-color="${OLIVE}" stop-opacity="0"/></linearGradient>
  <linearGradient id="${id}r" x1="0" x2="${W}" gradientUnits="userSpaceOnUse"><stop offset=".5" stop-color="${OLIVE}"/><stop offset=".75" stop-color="${OLIVE}" stop-opacity=".5"/><stop offset=".95" stop-color="${OLIVE}" stop-opacity=".1"/><stop offset="1" stop-color="${OLIVE}" stop-opacity="0"/></linearGradient></defs>
  <path d="M${CUT} 0H${W}V${H}H0V${CUT}Z" fill="rgba(0,0,0,.85)"/><path d="M${CUT} 0H${W}V${H}H0V${CUT}Z" fill="url(#${id}f)"/>
  <path d="M.75 ${H}V${CUT}L${CUT} .75H${W}" fill="none" stroke="url(#${id}r)" stroke-width="1.5"/></svg>`;
const tile = `<path d="M3 0H16V16H0V3Z" fill="rgba(0,0,0,.5)"/>`;
const star = () => { let p = []; for (let i = 0; i < 10; i++) { const r = i % 2 ? 2.5 : 6.2, a = Math.PI / 2 + i * Math.PI / 5; p.push(`${(8 + r * Math.cos(a)).toFixed(3)} ${(8.6 - r * Math.sin(a)).toFixed(3)}`); }
  return `<path d="M${p.join("L")}Z" fill="${VALUE}" stroke="rgba(200,80,30,.8)" stroke-width=".8"/>`; };
const SYMBOL = {
  1: `<svg class="sy" viewBox="0 0 16 16" aria-hidden="true">${tile}${star()}</svg>`,
  2: `<svg class="sy" viewBox="0 0 16 16" aria-hidden="true">${tile}<path d="M2.5 6L5 4.5H11L13.5 6L14.5 11L12.5 12.5L10.5 10.5H5.5L3.5 12.5L1.5 11Z" fill="none" stroke="rgba(255,255,255,.9)" stroke-width="1.2"/><path d="M2 14L14 2" stroke="${ORANGE}" stroke-width="1.4"/></svg>`,
};
// newest first: a favorite removed (Undo), a favorite added, a lost controller that persists
const NOTICES = [[1, "Removed from favorites.", "Undo"], [1, "Added to favorites."], [2, "Controller disconnected."]];
const stack = document.querySelector("#cpNotices .cp-in");
const items = [];
if (stack) {
  NOTICES.forEach(([kind, text, action], i) => {
    const n = document.createElement("div");
    n.className = "cp-ntc";
    n.innerHTML = plate("cpN" + i) + SYMBOL[kind] + `<span class="t">${text}</span>` + (action ? `<button class="a">${action}</button>` : "");
    stack.appendChild(n); items.push(n);
  });
  const place = shown => items.forEach(n => { const slot = shown.indexOf(n); n.style.display = slot < 0 ? "none" : "";
    if (slot >= 0) n.style.top = `calc(${14 + slot * (H + 6)} * var(--dp))`; });
  place(items);
  document.getElementById("cpReplay")?.addEventListener("click", () => {
    // oldest enters first; each newer one takes the top slot and pushes the others down
    const order = items.slice().reverse(), shown = [];
    place(shown);
    order.forEach((n, i) => setTimeout(() => { shown.unshift(n); place(shown); n.classList.remove("in"); void n.offsetWidth; if (!reduce) n.classList.add("in"); }, 400 * i));
  });
}

/* Search across categories (spec only); results update with every keystroke. */
const SETTINGS = [
  ["Mouse sensitivity", "13", "CONTROLS - MOUSE", "aim speed"], ["Look sensitivity", "2.4", "CONTROLS - CONTROLLER", "stick speed"],
  ["Gyro sensitivity", "1.0", "CONTROLS - CONTROLLER", "motion aim"], ["Touch look sensitivity", "1.6", "CONTROLS - TOUCH", ""],
  ["Anisotropic filtering", "16x", "DISPLAY - IMAGE QUALITY", "texture sharpness"], ["Vertical sync", "On", "DISPLAY - SCREEN", "vsync tearing"],
  ["Text size", "125%", "DISPLAY - GRAPHICS", "font scale"], ["Subtitles", "On", "AUDIO", "captions"],
  ["Crosshair", "8", "GAME OPTIONS - HUD", "reticle"], ["Reduced motion", "Off", "GAME OPTIONS - GENERAL", "animation"],
];
const input = document.getElementById("search"), results = document.getElementById("results");
const esc = s => s.replace(/[&<>]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
const mark = (text, q) => { const i = text.toLowerCase().indexOf(q); return i < 0 ? esc(text) : esc(text.slice(0, i)) + "<mark>" + esc(text.slice(i, i + q.length)) + "</mark>" + esc(text.slice(i + q.length)); };
function runSearch() {
  const q = input.value.trim().toLowerCase();
  const hits = q ? SETTINGS.filter(s => (s[0] + " " + s[1] + " " + s[3]).toLowerCase().includes(q)) : SETTINGS.slice(0, 4);
  results.innerHTML = hits.length ? hits.slice(0, 6).map((s, i) =>
    `<div class="res${i === 0 ? " hov" : ""}"><b>${mark(s[0], q)}</b><span class="v">${esc(s[1])}</span><span class="p">${s[2]}</span></div>`).join("")
    : `<div class="none">No settings match “${esc(input.value)}”. Try a shorter word.</div>`;
}
input?.addEventListener("input", runSearch); if (input) runSearch();

/* Tags: a filter tag toggles; the source tags hold one choice between them. */
document.querySelectorAll("#cpFilters .cp-tag").forEach(t => t.addEventListener("click", () => t.classList.toggle("on")));
document.querySelectorAll("#cpSource .cp-tag").forEach(t => t.addEventListener("click", () =>
  document.querySelectorAll("#cpSource .cp-tag").forEach(x => x.classList.toggle("on", x === t))));

/* Network quality (retained_mp_menus.ping_meter). */
const nq = document.getElementById("cpNq");
if (nq) {
  const bars = ms => ms <= 60 ? 4 : ms <= 120 ? 3 : ms <= 200 ? 2 : 1;
  nq.innerHTML = [["Hangar Brawl", 24], ["Rail Only Nights", 88], ["Arena Ladder", 150], ["Low Grav Fun", 260]].map(([name, ms]) => {
    const lit = bars(ms), c = lit >= 3 ? "#B5C784" : lit === 2 ? "#E3AD36" : "#E46D56";
    const r = [0, 1, 2, 3].map(b => { const h = 4.5 + b * 3.3; return `<rect x="${2 + b * 5}" y="${(15 - h).toFixed(1)}" width="3.3" height="${h.toFixed(1)}" fill="${c}" fill-opacity="${b < lit ? 1 : .2}"/>`; }).join("");
    return `<div><span>${name}</span><svg viewBox="0 0 22 16" aria-label="${lit} of 4 bars">${r}</svg><span>${ms} ms</span></div>`;
  }).join("");
}

/* Setting rows: a choice unfolds its list under the row; a check box toggles. */
const rowsBox = document.getElementById("cpRows"), list = document.getElementById("cpList");
if (rowsBox && list) {
  let openRow = null;
  const close = () => { list.classList.remove("open"); openRow = null; };
  const open = row => {
    const values = row.dataset.values.split("|"), at = +(row.dataset.at || 0);
    list.innerHTML = values.map((v, i) => `<div role="option" class="${i === at ? "sel hi" : ""}" data-i="${i}">${v}</div>`).join("");
    list.style.top = row.offsetTop + row.offsetHeight + "px";
    list.classList.add("open"); openRow = row;
  };
  rowsBox.querySelectorAll(".cp-row").forEach(row => row.addEventListener("click", () => {
    rowsBox.querySelectorAll(".cp-row").forEach(x => x.classList.toggle("f", x === row));
    if (row.dataset.kind === "toggle") { row.querySelector(".cp-box").classList.toggle("on"); close(); return; }
    openRow === row ? close() : open(row);
  }));
  list.addEventListener("click", e => {
    const o = e.target.closest("[data-i]"); if (!o || !openRow) return;
    openRow.dataset.at = o.dataset.i; openRow.querySelector(".v").textContent = o.textContent; close();
  });
  list.addEventListener("mouseover", e => { const o = e.target.closest("[data-i]"); if (o) list.querySelectorAll("[data-i]").forEach(x => x.classList.toggle("hi", x === o)); });
  open(rowsBox.querySelector(".cp-row.f"));   // as in the subpages-game-list capture
}

/* row.reveal: each loading row fades in over 120 ms, 15 ms after the one above, 90 ms at most. */
document.getElementById("cpReveal")?.addEventListener("click", () => {
  document.querySelectorAll("#cpLoad .skel").forEach((r, i) => {
    if (reduce) return;
    r.animate([{ opacity: 0 }, { opacity: 0, offset: Math.min(15 * i, 90) / (Math.min(15 * i, 90) + 120) }, { opacity: .4 }], { duration: Math.min(15 * i, 90) + 120, fill: "backwards" });
  });
});

/* The DISPLAY page's dialog plate and its buttons (retained_system.dialog, dialog_button), sized to their boxes. */
const cutPath = (w, h, c) => `M${c} 0H${w - c}L${w} ${c}V${h - c}L${w - c} ${h}H0V${c}Z`;
const drawPlate = (svg, rimW, fill, edge, c, focus) => {
  const r = svg.getBoundingClientRect(), w = r.width, h = r.height; if (!w) return;
  svg.setAttribute("viewBox", `0 0 ${w} ${h}`);
  const d = cutPath(w, h, c);
  svg.innerHTML = (rimW ? `<path d="${d}" fill="none" stroke="#000" stroke-width="${rimW}"/>` : "") + `<path d="${d}" fill="${fill}" stroke="${edge}" stroke-width="1"/>` +
    (focus ? `<path class="fo" d="${d}" fill="none" stroke="${ORANGE}" stroke-width="2"/>` : "");
};
const dlg = document.querySelector(".cp-dlg");
const paintDialog = () => {
  if (!dlg) return;
  drawPlate(dlg.querySelector(":scope > svg"), 4, "#090C08", "rgba(139,150,75,.7)", 12, false);
  dlg.querySelectorAll(".cp-btn svg").forEach(s => drawPlate(s, 0, "rgba(9,12,8,.96)", "rgba(139,150,75,.6)", 6, true));
};
paintDialog(); addEventListener("resize", paintDialog);
if (document.fonts) document.fonts.ready.then(paintDialog);
const count = document.getElementById("cpCount");
if (count) {
  let left = 15, timer = null;
  const restart = msg => { clearInterval(timer); count.textContent = msg; setTimeout(start, 2400); };
  function start() { left = 15; count.textContent = left; clearInterval(timer); timer = setInterval(() => { left--; count.textContent = left; if (left <= 0) restart("Reverted"); }, 1000); }
  document.getElementById("cpKeep")?.addEventListener("click", () => restart("Kept"));
  document.getElementById("cpRevert")?.addEventListener("click", () => restart("Reverted"));
  if (reduce) count.textContent = "12"; else start();
}

/* Crosshair editor (spec only): generic vector crosshairs, the stock menu's eight colors. */
const XH = [
  (g, s, c) => svgEl("path", { d: `M0 ${-s}V${-s * .3}M0 ${s * .3}V${s}M${-s} 0H${-s * .3}M${s * .3} 0H${s}`, stroke: c, "stroke-width": 2 }, g),
  (g, s, c) => { svgEl("circle", { r: s * .75, fill: "none", stroke: c, "stroke-width": 1.6 }, g); svgEl("circle", { r: 1.8, fill: c }, g); },
  (g, s, c) => svgEl("circle", { r: Math.max(2, s * .16), fill: c }, g),
  (g, s, c) => svgEl("path", { d: `M${-s * .7} ${s * .45}L0 ${-s * .1}L${s * .7} ${s * .45}`, fill: "none", stroke: c, "stroke-width": 2 }, g),
  (g, s, c) => svgEl("path", { d: `M${-s} ${-s * .5}V${-s}H${-s * .5}M${s * .5} ${-s}H${s}V${-s * .5}M${s} ${s * .5}V${s}H${s * .5}M${-s * .5} ${s}H${-s}V${s * .5}`, fill: "none", stroke: c, "stroke-width": 1.8 }, g),
  (g, s, c) => { svgEl("path", { d: `M0 ${-s}V${-s * .45}M${-s} 0H${-s * .45}M${s * .45} 0H${s}`, stroke: c, "stroke-width": 2 }, g); svgEl("circle", { r: 1.6, fill: c }, g); },
];
// g_crosshairColor's eight stock values (retained_game.CROSSHAIR_COLORS): white, red, orange, yellow, green, cyan, blue, pink
const COLS = ["#FFFFFF", "#FF0000", "#FF8000", "#FFFF00", "#00FF00", "#00FFFF", "#0000FF", "#FF0080"];
const xprev = document.getElementById("xprev"), xgrid = document.getElementById("xgrid"), xcols = document.getElementById("xcols"), xsize = document.getElementById("xsize");
if (xprev) {
  let shape = 0, col = COLS[4];
  const size = () => 24 + (72 - 24) * (+xsize.getAttribute("aria-valuenow")) / 20;
  const draw = () => { xprev.innerHTML = ""; XH[shape](svgEl("g", {}, xprev), size() / 2, col); };
  XH.forEach((fn, i) => { const b = document.createElement("button"); b.className = i === shape ? "on" : ""; b.setAttribute("aria-label", "Crosshair " + (i + 1));
    const s = svgEl("svg", { viewBox: "-17 -17 34 34" }); fn(s, 11, "#fff"); b.appendChild(s);
    b.addEventListener("click", () => { shape = i; xgrid.querySelectorAll("button").forEach(x => x.classList.toggle("on", x === b)); draw(); }); xgrid.appendChild(b); });
  COLS.forEach(c => { const b = document.createElement("button"); b.style.setProperty("--c", c); b.className = c === col ? "on" : ""; b.setAttribute("aria-label", "Color " + c);
    b.addEventListener("click", () => { col = c; xcols.querySelectorAll("button").forEach(x => x.classList.toggle("on", x === b)); draw(); }); xcols.appendChild(b); });
  new MutationObserver(draw).observe(xsize, { attributes: true, attributeFilter: ["aria-valuenow"] });
  draw();
}

/* Stick response (spec only): dead-zone disc, threshold ring, live position; curve with the live point. */
const stick = document.getElementById("stick"), curve = document.getElementById("curve");
if (stick && curve) {
  const DZ = 12, OUT = 94;
  svgEl("rect", { x: -100, y: -100, width: 200, height: 200, fill: "rgba(0,0,0,.45)" }, stick);
  svgEl("path", { d: "M-100 0H100M0 -100V100", stroke: "rgba(255,255,255,.1)" }, stick);
  svgEl("circle", { r: 100, fill: "none", stroke: "rgba(139,150,75,.6)", "stroke-width": 1.5 }, stick);
  svgEl("circle", { r: OUT, fill: "none", stroke: OLIVE, "stroke-dasharray": "3 4" }, stick);
  svgEl("circle", { r: DZ, fill: "rgba(139,150,75,.35)", stroke: OLIVE }, stick);
  const dot = svgEl("circle", { r: 5, fill: ORANGE }, stick);
  svgEl("rect", { x: 0, y: -100, width: 100, height: 100, fill: "rgba(0,0,0,.45)" }, curve);
  svgEl("path", { d: "M0 -50H100M50 -100V0", stroke: "rgba(255,255,255,.1)" }, curve);
  let d = `M0 0H${DZ}`; for (let x = DZ; x <= 100; x += 2) { const t = (x - DZ) / (100 - DZ); d += `L${x} ${-100 * Math.pow(t, 1.6)}`; }
  svgEl("path", { d, fill: "none", stroke: VALUE, "stroke-opacity": .85, "stroke-width": 1.6 }, curve);
  svgEl("text", { x: 2, y: 10, "font-family": "IBM Plex Mono, monospace", "font-size": 8, fill: "#8C947A" }, curve).textContent = "input";
  const cdot = svgEl("circle", { r: 4, fill: ORANGE }, curve);
  const placeDot = t => {
    const x = 78 * Math.sin(t * 1.3), y = 64 * Math.sin(t * 0.9 + 1);
    dot.setAttribute("cx", x); dot.setAttribute("cy", y);
    const m = Math.min(100, Math.hypot(x, y)), o = m <= DZ ? 0 : Math.pow((m - DZ) / (100 - DZ), 1.6) * 100;
    cdot.setAttribute("cx", m); cdot.setAttribute("cy", -o);
  };
  if (reduce) placeDot(1.1); else { const t0 = performance.now(); const tick = now => { placeDot((now - t0) / 1000); requestAnimationFrame(tick); }; requestAnimationFrame(tick); }
}
})();
