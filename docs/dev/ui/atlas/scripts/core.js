
(() => {
const NS = "http://www.w3.org/2000/svg";
let uid = 0;
const svgEl = (tag, attrs = {}, parent) => {
  const el = document.createElementNS(NS, tag);
  for (const k in attrs) el.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(el);
  return el;
};

/* Plates: light plates are white art tinted marine.olive; dark plates are black art
   with marine.rail.dark rails. Visible height H, lower-leading 45-degree cut C. */
const KINDS = {
  nav:  { H: 37, C: 17, dark: true },
  sec:  { H: 37, C: 21, fillTo: 50, fade: 100 },
  act:  { H: 35, C: 20, fillTo: 33, fade: 95 },
  prim: { H: 50, C: 28, fillTo: 50, fade: 100 },
  row:  { H: 29, C: 16, fillTo: 50, fade: 100 },
  arow: { H: 29, C: 16, fillTo: 50, fade: 100 },
};
function plateArt(el) {
  const kind = Object.keys(KINDS).find(k => el.classList.contains(k));
  if (!kind) return;
  const { H, C, dark, fillTo, fade } = KINDS[kind];
  const rail = dark ? "#CCCC51" : "#8B964B";
  const art = document.createElement("div");
  art.className = "art";
  const fill = document.createElement("div");
  fill.style.cssText = `position:absolute;inset:0;clip-path:polygon(0 0,100% 0,100% 100%,${C}px 100%,0 calc(100% - ${C}px));background:` +
    (dark ? "linear-gradient(90deg,#000 0 44%,rgba(0,0,0,.5) 62%,transparent 88%)"
          : `linear-gradient(90deg,rgba(139,150,75,.49) 0 ${fillTo}%,transparent ${fade}%)`);
  art.appendChild(fill);
  const s = svgEl("svg", { width: "100%", height: H, style: "position:absolute;inset:0;overflow:visible" });
  const g = "g" + (++uid);
  const lg = svgEl("linearGradient", { id: g, gradientUnits: "userSpaceOnUse", x1: 0, x2: "100%", y1: 0, y2: 0 }, svgEl("defs", {}, s));
  svgEl("stop", { offset: dark ? ".44" : fillTo / 100, "stop-color": rail }, lg);
  svgEl("stop", { offset: dark ? ".88" : fade / 100, "stop-color": rail, "stop-opacity": 0 }, lg);
  const w = 1.5, h = w / 2;
  svgEl("line", { class: "lr", x1: h, y1: 0, x2: h, y2: H - C, stroke: rail, "stroke-width": w }, s);
  svgEl("line", { x1: h, y1: H - C, x2: C, y2: H - h, stroke: rail, "stroke-width": w }, s);
  svgEl("line", { x1: C, y1: H - h, x2: "100%", y2: H - h, stroke: `url(#${g})`, "stroke-width": w }, s);
  art.appendChild(s);
  // focus: a fine orange inset rail parallel to the leading rail, cut and bottom
  const d = 3.5, k = d * (Math.SQRT2 - 1);
  const f = svgEl("svg", { class: "frs", width: "100%", height: H });
  const o = { stroke: "#E38900", "stroke-width": 1.2 };
  svgEl("line", { ...o, x1: d, y1: d, x2: d, y2: H - C - k }, f);
  svgEl("line", { ...o, x1: d, y1: H - C - k, x2: C + k, y2: H - d }, f);
  svgEl("line", { ...o, x1: C + k, y1: H - d, x2: "62%", y2: H - d }, f);
  el.prepend(art, f);
}
document.querySelectorAll(".pl").forEach(plateArt);

/* Vector frames drawn at the element's laid-out size. */
const SHAPES = {
  card: (W, H) => cutPoly(W, H, 3, 12, 3, 12),
  pop:  (W, H) => cutPoly(W, H, 2, 12, 2, 12),
};
function cutPoly(W, H, tl, tr, br, bl) {
  return `M${tl} 0H${W - tr}L${W} ${tr}V${H - br}L${W - br} ${H}H${bl}L0 ${H - bl}V${tl}Z`;
}
function drawFrame(host) {
  const shape = host.dataset.shape;
  const W = host.offsetWidth, H = host.offsetHeight;
  if (!W || !H) return;
  let s = host.querySelector(":scope > svg.frame");
  if (s) s.remove();
  s = svgEl("svg", { class: "frame", viewBox: `0 0 ${W} ${H}`, preserveAspectRatio: "none", "aria-hidden": "true" });
  if (shape === "card" || shape === "pop") {
    svgEl("path", { d: SHAPES[shape](W, H), fill: "#000", "fill-opacity": .94, stroke: "#A0A040", "stroke-width": 1.5 }, s);
  } else if (shape === "modal") {
    // the stock art is inset in its rectangle (popup texels 7-504 of 512, 6.56 dp each side at 480 dp; 3.56 dp above the
    // raised sections and 5.34 dp below the bottom edge): under the stock column that inset is the lit margin.
    // Leading tooth, title slot recessed 17 dp across ~73% with 45-degree flanks, raised trailing section, 38 dp
    // lower-leading chamfer, square trailing corners, no rails
    const L = W * 7 / 512, x0 = L, x1 = W - L, y0 = 3.56, y1 = H - 5.34, R = 17, t = 6, e = Math.min(x1 - 8, x0 + t + 0.73 * (x1 - x0));
    svgEl("path", { d: `M${x0} ${y0}H${x0 + t}L${x0 + t + R} ${y0 + R}H${e - R}L${e} ${y0}H${x1}V${y1}H${x0 + 38}L${x0} ${y1 - 38}Z`, fill: "#000", "fill-opacity": .7 }, s);
  } else if (shape === "hband") {
    const g = "h" + (++uid), c = 12;
    const lg = svgEl("linearGradient", { id: g, x1: 0, x2: 1, y1: 0, y2: 0 }, svgEl("defs", {}, s));
    svgEl("stop", { offset: .6, "stop-color": "#8B964B", "stop-opacity": .49 }, lg);
    svgEl("stop", { offset: 1, "stop-color": "#8B964B", "stop-opacity": 0 }, lg);
    const grp = svgEl("g", { opacity: .6 }, s);
    svgEl("path", { d: `M${c} 0H${W}V${H}H0V${c}Z`, fill: `url(#${g})` }, grp);
    svgEl("path", { d: `M.75 ${H}V${c}L${c} .75H${W * .7}`, fill: "none", stroke: "#8B964B", "stroke-width": 1.5 }, grp);
  }
  host.prepend(s);
}
const frames = document.querySelectorAll(".fr[data-shape]");
const ro = new ResizeObserver(entries => entries.forEach(e => drawFrame(e.target)));
frames.forEach(f => { drawFrame(f); ro.observe(f); });

/* Tab strips: the multiplayer menus' construction (section 6), drawn once per strip. The active tab rises with a small
   leading flare and an 18 dp 45-degree trailing shoulder, filled #5A652A from 1.00 to 0.60, and the baseline rail breaks
   under it; the tab body carries the wash below the strip. */
function drawStrip(strip) {
  strip.querySelector(":scope > svg.strip")?.remove();
  const W = strip.clientWidth, H = strip.clientHeight, on = strip.querySelector(".tab.on");
  if (!W || !H) return;
  const s = svgEl("svg", { class: "strip", viewBox: `0 0 ${W} ${H}`, "aria-hidden": "true" });
  const rail = { fill: "none", stroke: "#CCCC51", "stroke-opacity": .85, "stroke-width": 1.5, "stroke-linejoin": "miter" }, y = H - .75;
  if (on) {
    const a = on.offsetLeft, b = a + on.offsetWidth, sh = 18, f = 4, g = "t" + (++uid);
    const lg = svgEl("linearGradient", { id: g, x1: 0, x2: 0, y1: 0, y2: 1 }, svgEl("defs", {}, s));
    svgEl("stop", { offset: 0, "stop-color": "#5A652A", "stop-opacity": 1 }, lg);
    svgEl("stop", { offset: 1, "stop-color": "#5A652A", "stop-opacity": .6 }, lg);
    const shape = `M${a - f} ${y}Q${a} ${y} ${a} ${y - f}V.75H${b - sh}L${b - .75} ${sh}V${y}`;
    svgEl("path", { d: shape + "Z", fill: `url(#${g})` }, s);
    svgEl("path", { d: shape, ...rail }, s);
    svgEl("path", { d: `M0 ${y}H${a - f}M${b - .75} ${y}H${W}`, ...rail }, s);
  } else svgEl("path", { d: `M0 ${y}H${W}`, ...rail }, s);
  strip.prepend(s);
}
const tabText = { "t-game": "GAME · difficulty, HUD, crosshair and subtitles.", "t-controls": "CONTROLS · bindings for keyboard and mouse, controller and touch.",
  "t-system": "SYSTEM · display, rendering and performance settings. Bumpers or a horizontal swipe change category.", "t-audio": "AUDIO · interface volume, mono audio and music." };
document.querySelectorAll(".tabs").forEach(strip => {
  const tabs = [...strip.querySelectorAll(".tab")];
  const pick = t => { tabs.forEach(x => { x.classList.toggle("on", x === t); x.setAttribute("aria-selected", x === t); }); drawStrip(strip);
    const body = strip.nextElementSibling?.matches("[data-tabbody]") ? strip.nextElementSibling : null; if (body && tabText[t.dataset.tab]) body.textContent = tabText[t.dataset.tab]; };
  tabs.forEach(t => t.addEventListener("click", () => pick(t)));
  const keys = strip.querySelectorAll(".tabkey");          // the previous and next glyphs step through the strip
  const step = d => { const i = tabs.findIndex(t => t.classList.contains("on")); pick(tabs[(i + d + tabs.length) % tabs.length]); };
  keys[0]?.addEventListener("click", () => step(-1)); keys[1]?.addEventListener("click", () => step(1));
  strip.addEventListener("keydown", e => { if (e.key === "ArrowLeft" || e.key === "ArrowRight") { e.preventDefault(); step(e.key === "ArrowLeft" ? -1 : 1); strip.querySelector(".tab.on")?.focus(); } });
  drawStrip(strip); new ResizeObserver(() => drawStrip(strip)).observe(strip);
  document.fonts?.ready.then(() => drawStrip(strip));
});

/* Cycling values: activation steps forward, the secondary action steps back. */
document.querySelectorAll(".cyc").forEach(row => {
  const vals = row.dataset.values.split("|"); let i = 0; const out = row.querySelector(".val");
  const step = n => { i = (i + n + vals.length) % vals.length; out.textContent = vals[i]; };
  row.addEventListener("click", () => step(1));
  row.addEventListener("contextmenu", e => { e.preventDefault(); step(-1); });
});
document.querySelectorAll(".spin").forEach(sp => {
  const vals = sp.dataset.values.split("|"); let i = 0; const out = sp.querySelector("output");
  sp.querySelector(".pv").addEventListener("click", () => { i = (i + vals.length - 1) % vals.length; out.textContent = vals[i]; });
  sp.querySelector(".nx").addEventListener("click", () => { i = (i + 1) % vals.length; out.textContent = vals[i]; });
});
document.querySelectorAll(".box").forEach(b => b.addEventListener("click", e => {
  e.preventDefault();
  if (b.getAttribute("role") === "radio") {
    b.closest("[role=radiogroup]").querySelectorAll(".box").forEach(x => { x.classList.toggle("on", x === b); x.setAttribute("aria-checked", x === b); });
  } else { b.classList.remove("mixed"); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); }
}));

/* Slider: the stock tick ramp. Alternating tall and short ticks rise from 3 to 13.5 dp
   across 106.5 dp; a 6x12 dp bar thumb; ticks past the thumb drop to 40%. */
document.querySelectorAll("svg.ramp-s").forEach(sv => {
  const N = 20, X0 = 3, X1 = 103.5, base = 15, out = sv.nextElementSibling;
  let v = +sv.getAttribute("aria-valuenow");
  const ticks = [];
  svgEl("line", { x1: X0, x2: X1, y1: base + .5, y2: base + .5, stroke: "#FFBE23", "stroke-opacity": .35 }, sv);
  for (let i = 0; i <= N; i++) {
    const x = X0 + (X1 - X0) * i / N, tall = 3 + 10.5 * i / N, hgt = i % 2 ? tall * .55 : tall;
    ticks.push(svgEl("rect", { x: x - .6, y: base - hgt, width: 1.2, height: hgt, fill: "#FFBE23" }, sv));
  }
  const th = svgEl("rect", { y: 3, width: 6, height: 12, fill: "rgba(255,190,35,.4)", stroke: "#FFBE23", "stroke-width": 1.2 }, sv);
  const draw = () => {
    ticks.forEach((t, i) => t.setAttribute("fill-opacity", i <= v ? .8 : .32));
    th.setAttribute("x", X0 + (X1 - X0) * v / N - 3);
    sv.setAttribute("aria-valuenow", v); out.textContent = v;
  };
  const fromX = cx => { const r = sv.getBoundingClientRect(); v = Math.max(0, Math.min(N, Math.round((cx - r.left - X0) / (X1 - X0) * N))); draw(); };
  sv.addEventListener("pointerdown", e => { sv.setPointerCapture(e.pointerId); fromX(e.clientX); });
  sv.addEventListener("pointermove", e => { if (sv.hasPointerCapture(e.pointerId)) fromX(e.clientX); });
  sv.addEventListener("keydown", e => {
    const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.key];
    if (d) { e.preventDefault(); v = Math.max(0, Math.min(N, v + d)); draw(); }
  });
  draw();
});
window.__atlas = { svgEl };
})();
