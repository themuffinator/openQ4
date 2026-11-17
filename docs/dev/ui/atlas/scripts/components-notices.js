
(() => {
const { svgEl } = window.__atlas;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;

/* Notices: newest first, enter with notice.enter (160 ms, 12 dp from the trailing edge). */
const notices = document.getElementById("notices");
document.getElementById("replayNotices")?.addEventListener("click", () => {
  const items = [...notices.children];
  items.forEach(n => { n.classList.remove("in"); n.style.visibility = "hidden"; });
  items.slice().reverse().forEach((n, i) => setTimeout(() => {
    notices.prepend(n); n.style.visibility = ""; void n.offsetWidth; n.classList.add("in");
  }, 350 * i));
});

/* Search across categories; results update with every keystroke. */
const SETTINGS = [
  ["Mouse sensitivity", "13", "CONTROLS - MOUSE", "aim speed"], ["Look sensitivity", "2.4", "CONTROLS - CONTROLLER", "stick speed"],
  ["Gyro sensitivity", "1.0", "CONTROLS - CONTROLLER", "motion aim"], ["Touch look sensitivity", "1.6", "CONTROLS - TOUCH", ""],
  ["Anisotropic filtering", "16x", "SYSTEM - RENDERING", "texture sharpness"], ["Vertical sync", "On", "SYSTEM - DISPLAY", "vsync tearing"],
  ["Text size", "125%", "INTERFACE - SIZE", "font scale"], ["Subtitles", "On", "INTERFACE - ACCESSIBILITY", "captions"],
  ["Crosshair size", "24 dp", "GAME - CROSSHAIR", "reticle"], ["Reduced motion", "Off", "INTERFACE - ACCESSIBILITY", "animation"],
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

document.querySelectorAll(".ftag").forEach(t => t.addEventListener("click", () => t.classList.toggle("on")));

/* Countdown confirmation: 15 ticks, one drops each second; timing out reverts. */
const ramp = document.getElementById("cdramp"), num = document.getElementById("cdnum");
if (ramp) {
  const ticks = [];
  for (let i = 0; i < 15; i++) { const t = document.createElement("i"); t.style.height = (5 + i * 0.75) + "px"; ramp.appendChild(t); ticks.push(t); }
  let left = 15, timer = null;
  const show = () => { num.textContent = left; ticks.forEach((t, i) => t.classList.toggle("gone", i >= left)); };
  const restart = (msg) => { clearInterval(timer); if (msg) num.textContent = msg; setTimeout(start, 2400); };
  function start() { left = 15; show(); clearInterval(timer); timer = setInterval(() => { left--; show(); if (left <= 0) restart("Reverted"); }, 1000); }
  document.getElementById("cdKeep").addEventListener("click", () => restart("Kept"));
  document.getElementById("cdRevert").addEventListener("click", () => restart("Reverted"));
  start();
}

/* Crosshair editor: generic vector crosshairs, size 16-48 dp, eight colors. */
const XH = [
  (g, s, c) => svgEl("path", { d: `M0 ${-s}V${-s * .3}M0 ${s * .3}V${s}M${-s} 0H${-s * .3}M${s * .3} 0H${s}`, stroke: c, "stroke-width": 2 }, g),
  (g, s, c) => { svgEl("circle", { r: s * .75, fill: "none", stroke: c, "stroke-width": 1.6 }, g); svgEl("circle", { r: 1.8, fill: c }, g); },
  (g, s, c) => svgEl("circle", { r: Math.max(2, s * .16), fill: c }, g),
  (g, s, c) => svgEl("path", { d: `M${-s * .7} ${s * .45}L0 ${-s * .1}L${s * .7} ${s * .45}`, fill: "none", stroke: c, "stroke-width": 2 }, g),
  (g, s, c) => svgEl("path", { d: `M${-s} ${-s * .5}V${-s}H${-s * .5}M${s * .5} ${-s}H${s}V${-s * .5}M${s} ${s * .5}V${s}H${s * .5}M${-s * .5} ${s}H${-s}V${s * .5}`, fill: "none", stroke: c, "stroke-width": 1.8 }, g),
  (g, s, c) => { svgEl("path", { d: `M0 ${-s}V${-s * .45}M${-s} 0H${-s * .45}M${s * .45} 0H${s}`, stroke: c, "stroke-width": 2 }, g); svgEl("circle", { r: 1.6, fill: c }, g); },
];
const COLS = ["#FFFFFF", "#9CFF6B", "#E38900", "#FFBE23", "#39D1FF", "#FF4FD8", "#FF3B30", "#F2F26B"];
const xprev = document.getElementById("xprev"), xgrid = document.getElementById("xgrid"), xcols = document.getElementById("xcols"), xsize = document.getElementById("xsize");
if (xprev) {
  let shape = 0, col = COLS[1];
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

/* Stick response: dead-zone disc, threshold ring, live position; curve with the live point. */
const stick = document.getElementById("stick"), curve = document.getElementById("curve");
if (stick && curve) {
  const DZ = 12, OUT = 94, OLIVE = "#8B964B";
  svgEl("rect", { x: -100, y: -100, width: 200, height: 200, fill: "rgba(0,0,0,.45)" }, stick);
  svgEl("path", { d: "M-100 0H100M0 -100V100", stroke: "rgba(255,255,255,.1)" }, stick);
  svgEl("circle", { r: 100, fill: "none", stroke: "rgba(139,150,75,.6)", "stroke-width": 1.5 }, stick);
  svgEl("circle", { r: OUT, fill: "none", stroke: OLIVE, "stroke-dasharray": "3 4" }, stick);
  svgEl("circle", { r: DZ, fill: "rgba(139,150,75,.35)", stroke: OLIVE }, stick);
  const dot = svgEl("circle", { r: 5, fill: "#E38900" }, stick);
  svgEl("rect", { x: 0, y: -100, width: 100, height: 100, fill: "rgba(0,0,0,.45)" }, curve);
  svgEl("path", { d: "M0 -50H100M50 -100V0", stroke: "rgba(255,255,255,.1)" }, curve);
  let d = `M0 0H${DZ}`; for (let x = DZ; x <= 100; x += 2) { const t = (x - DZ) / (100 - DZ); d += `L${x} ${-100 * Math.pow(t, 1.6)}`; }
  svgEl("path", { d, fill: "none", stroke: "#FFBE23", "stroke-opacity": .85, "stroke-width": 1.6 }, curve);
  svgEl("text", { x: 2, y: 10, "font-family": "IBM Plex Mono, monospace", "font-size": 8, fill: "#8C947A" }, curve).textContent = "input";
  const cdot = svgEl("circle", { r: 4, fill: "#E38900" }, curve);
  const place = t => {
    const x = 78 * Math.sin(t * 1.3), y = 64 * Math.sin(t * 0.9 + 1);
    dot.setAttribute("cx", x); dot.setAttribute("cy", y);
    const m = Math.min(100, Math.hypot(x, y)), o = m <= DZ ? 0 : Math.pow((m - DZ) / (100 - DZ), 1.6) * 100;
    cdot.setAttribute("cx", m); cdot.setAttribute("cy", -o);
  };
  if (reduce) place(1.1); else { const t0 = performance.now(); const tick = now => { place((now - t0) / 1000); requestAnimationFrame(tick); }; requestAnimationFrame(tick); }
}
})();
