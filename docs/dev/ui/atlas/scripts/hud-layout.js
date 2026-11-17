
(() => {
const A = window.__atlas, { svgEl } = A;
/* The Remastered HUD layout, in stock units (section 14.4). The 4:3 stock canvas is the center screen and keeps
   its height; a wider view adds leading and trailing screens (horizontal expansion), a narrower one adds top and
   bottom bands (vertical expansion), down to the 9:16 portrait view, where the center screen spans the width. Groups
   keep their stock offsets from the edge they belong to, so at 4:3 the Remastered layout lands on the stock rectangles.
   Side screens stop growing at 21:9 unless the HUD width is Full. */
const ASPECTS = [["9:16", 9 / 16], ["5:4", 5 / 4], ["4:3", 4 / 3], ["16:10", 16 / 10], ["16:9", 16 / 9], ["21:9", 64 / 27], ["32:9", 32 / 9]];
const SCALES = [["100%", 1], ["125%", 1.25], ["150%", 1.5]];
const hud = { aspect: 16 / 9, regions: false, scale: 1, classic: false, minimal: false, device: "kbm", fns: new Set() };
const CAP = 480 * 64 / 27;

hud.layout = () => {
  const a = hud.aspect, wide = a >= 4 / 3;
  const W = wide ? 480 * a : 640, H = wide ? 480 : 640 / a;
  const x0 = (640 - W) / 2, y0 = (480 - H) / 2;
  const hw = Math.min(W, CAP), hx = (640 - hw) / 2;
  const c = hud.classic;
  return { a, W, H, x0, y0, vb: `${x0} ${y0} ${W} ${H}`, side: (W - 640) / 2, band: (H - 480) / 2, capped: W > CAP,
    L: c ? 0 : hx, R: c ? 640 : hx + hw, T: c ? 0 : y0, B: c ? 480 : y0 + H, s: c ? 1 : hud.scale };
};
/* A group authored in stock coordinates around the stock anchor point, placed at the live anchor and scaled
   about it. Anchors: t/c/b then l/c/r. */
hud.place = (parent, anchor, lay, extra = "") => {
  const v = anchor[0], h = anchor[1];
  const ax = h === "l" ? lay.L : h === "r" ? lay.R : 320, ay = v === "t" ? lay.T : v === "b" ? lay.B : 240;
  const sx = h === "l" ? 0 : h === "r" ? 640 : 320, sy = v === "t" ? 0 : v === "b" ? 480 : 240;
  return svgEl("g", { transform: `translate(${ax} ${ay}) scale(${lay.s}) translate(${-sx} ${-sy})${extra}`, "data-anchor": anchor }, parent);
};
/* Optional diagnostic: the screens, bands and anchor points. Off by default; it is not part of the HUD. */
hud.regionsLayer = (svg, lay) => {
  if (!hud.regions) return;
  const g = svgEl("g", { "pointer-events": "none" }, svg), mono = "IBM Plex Mono, monospace";
  const tint = (x, y, w, h) => w > .5 && h > .5 && svgEl("rect", { x, y, width: w, height: h, fill: "#B3D06E", "fill-opacity": .06 }, g);
  tint(lay.x0, 0, lay.side, 480); tint(640, 0, lay.side, 480); tint(0, lay.y0, 640, lay.band); tint(0, 480, 640, lay.band);
  const dash = { stroke: "#B3D06E", "stroke-opacity": .45, "stroke-width": .8, "stroke-dasharray": "4 3", fill: "none" };
  if (lay.side > .5) svgEl("path", { d: `M0 ${lay.y0}V${lay.y0 + lay.H}M640 ${lay.y0}V${lay.y0 + lay.H}`, ...dash }, g);
  if (lay.band > .5) svgEl("path", { d: `M${lay.x0} 0H${lay.x0 + lay.W}M${lay.x0} 480H${lay.x0 + lay.W}`, ...dash }, g);
  if (lay.capped) svgEl("path", { d: `M${lay.L} ${lay.y0}V${lay.y0 + lay.H}M${lay.R} ${lay.y0}V${lay.y0 + lay.H}`, ...dash, stroke: "#E38900", "stroke-dasharray": "2 3" }, g);
  const label = (x, y, t, anchor = "middle") => { const tx = svgEl("text", { x, y, "text-anchor": anchor, "font-family": mono, "font-size": 7, "letter-spacing": .6, fill: "#D6E6A8", "fill-opacity": .75 }, g); tx.textContent = t; };
  const dp = u => Math.round(u * 720 / Math.min(lay.W, lay.H));     // the short side is 720 dp: 1.5 dp a unit in landscape, 1.125 at 9:16
  label(320, lay.y0 + (lay.band > 12 ? lay.band / 2 + 3 : 11) + (lay.band > 12 ? 0 : 0), lay.band > 12 ? `TOP BAND · ${dp(lay.band)} dp` : "CENTER SCREEN · 4:3");
  if (lay.band > 12) { label(320, 480 + lay.band / 2 + 3, `BOTTOM BAND · ${dp(lay.band)} dp`); label(320, 11, "CENTER SCREEN · 4:3"); }
  if (lay.side > 30) { label(lay.x0 + lay.side / 2, 11, "LEADING"); label(lay.x0 + lay.side / 2, 20, `${dp(lay.side)} dp`); label(640 + lay.side / 2, 11, "TRAILING"); label(640 + lay.side / 2, 20, `${dp(lay.side)} dp`); }
  if (lay.capped) label(lay.L + 4, lay.y0 + lay.H - 6, "21:9 HUD WIDTH", "start");
  const tick = (x, y, dx, dy) => svgEl("path", { d: `M${x + dx * 7} ${y}H${x}V${y + dy * 7}`, stroke: "#E38900", "stroke-width": 1.4, fill: "none" }, g);
  [[lay.L, lay.T, 1, 1], [lay.R, lay.T, -1, 1], [lay.L, lay.B, 1, -1], [lay.R, lay.B, -1, -1]].forEach(p => tick(...p));
  [[320, lay.T], [320, lay.B], [lay.L, 240], [lay.R, 240]].forEach(([x, y]) => svgEl("circle", { cx: x, cy: y, r: 2.2, fill: "#E38900" }, g));
};
/* Shared controls: every HUD stage on the page follows one aspect ratio, region toggle and HUD scale. */
const rows = [];
/* minimal: offer the Minimal layout (ui_hudLayout 2) beside Classic; only stages that draw it pass it. */
hud.controls = (host, { scale = true, classic = true, device = false, minimal = false } = {}) => {
  const bar = document.createElement("div"); bar.className = "hudbar";
  const group = (label, items, key) => { const s = document.createElement("div"); s.className = "segs"; s.setAttribute("role", "group"); s.setAttribute("aria-label", label);
    s.innerHTML = `<span class="lbl">${label}</span>` + items.map(([t, v]) => `<button class="seg" data-k="${key}" data-v="${v}" aria-pressed="false">${t}</button>`).join("");
    bar.appendChild(s); };
  group("Aspect", ASPECTS, "aspect");
  if (scale) group("HUD scale", SCALES, "scale");
  const tg = document.createElement("div"); tg.className = "segs";
  tg.innerHTML = `<button class="seg" data-t="regions" aria-pressed="false">Screen regions</button>` + (classic ? `<button class="seg" data-t="classic" aria-pressed="false">Classic</button>` : "") +
    (minimal ? `<button class="seg" data-t="minimal" aria-pressed="false">Minimal</button>` : "");
  bar.appendChild(tg);
  if (device) {                                   // HUD prompts follow the active device (section 14.8)
    const dg = document.createElement("div"); dg.className = "segs"; dg.setAttribute("role", "group"); dg.setAttribute("aria-label", "Prompts");
    dg.innerHTML = `<span class="lbl">Prompts</span>` + [["Keyboard", "kbm"], ["Controller", "pad"], ["Touch", "touch"]].map(([t, v]) => `<button class="seg" data-d="${v}" aria-pressed="false">${t}</button>`).join("");
    bar.appendChild(dg);
  }
  bar.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
    if (b.dataset.d) hud.set("device", b.dataset.d); else if (b.dataset.k) hud.set(b.dataset.k, Number(b.dataset.v)); else hud.set(b.dataset.t, !hud[b.dataset.t]); });
  host.appendChild(bar); rows.push(bar); sync();
  return bar;
};
function sync() {
  rows.forEach(bar => bar.querySelectorAll(".seg").forEach(b => {
    const on = b.dataset.d ? hud.device === b.dataset.d : b.dataset.k ? Math.abs(hud[b.dataset.k] - Number(b.dataset.v)) < 1e-6 : !!hud[b.dataset.t];
    b.setAttribute("aria-pressed", on);
  }));
  document.querySelectorAll(".frameAR").forEach(fr => fr.style.setProperty("--ar", hud.aspect.toFixed(4)));
}
hud.on = f => hud.fns.add(f);
/* The layouts are exclusive: Classic (0), Remastered (1, neither set) and Minimal (2). */
hud.set = (k, v) => { hud[k] = v; if (v === true && k === "classic") hud.minimal = false; if (v === true && k === "minimal") hud.classic = false; sync(); hud.fns.forEach(f => f(k)); };
hud.fit = svg => { const lay = hud.layout(); svg.setAttribute("viewBox", lay.vb); return lay; };
A.hud = hud;
})();
