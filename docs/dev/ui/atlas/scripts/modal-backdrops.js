
(() => {
const A = window.__atlas, { svgEl } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif";

/* A copy of a drawn screen with its own ids, so its gradients and filters never collide with the original's. */
function cloneUnique(src, tag) {
  const c = src.cloneNode(true), map = new Map();
  c.removeAttribute("id");
  c.querySelectorAll("[id]").forEach(e => { const n = `${e.id}-${tag}`; map.set(e.id, n); e.id = n; });
  const fix = v => v.replace(/url\(#([^)]+)\)/g, (m, i) => map.has(i) ? `url(#${map.get(i)})` : m).replace(/^#(.+)$/, (m, i) => map.has(i) ? `#${map.get(i)}` : m);
  c.querySelectorAll("*").forEach(e => [...e.attributes].forEach(a => { if (a.value.includes("#")) { const v = fix(a.value); if (v !== a.value) e.setAttribute(a.name, v); } }));
  c.querySelectorAll("[tabindex],[role],[aria-label]").forEach(e => ["tabindex", "role", "aria-label"].forEach(k => e.removeAttribute(k)));
  c.setAttribute("preserveAspectRatio", "xMidYMid slice");
  c.setAttribute("aria-hidden", "true"); c.setAttribute("focusable", "false"); c.removeAttribute("style");
  return c;
}

/* The settings page in its page state (section 9): the bands moved to their page positions, the section
   navigation from 172 u, and rows on a 24 u pitch in the content column from 228 u. */
const TOP = [[-400, 89.7], [-297.9, 89.7], [-283.7, 103.8], [30.7, 103.8], [40.9, 94.7], [76.6, 94.7], [85.8, 103.8], [127.6, 103.8], [137.8, 94.7],
  [172.5, 94.7], [181.7, 103.8], [223.5, 103.8], [233.7, 94.7], [268.4, 94.7], [277.6, 103.8], [315.4, 103.8], [352.1, 68.5], [900, 68.5]];
const BTM = [[-399, 421.5], [-232.7, 421.5], [-222.5, 430.6], [-28.6, 430.6], [2.1, 400.4], [344.9, 400.4], [389.8, 443.7], [582.7, 443.7], [591.9, 434.6], [900, 434.6]];
const PAGES = {
  display: { section: 2, heading: "DISPLAY", rows: [["Resolution", "2560 × 1440"], ["Display mode", "Borderless"], ["Refresh rate", "144 Hz"],
    ["Vertical sync", "On"], ["Frame limit", "Off"], ["Brightness", 0.55], ["HUD scale", 0.4]] },
  controls: { section: 1, heading: "WEAPONS", rows: [["Attack", "Mouse 1"], ["Zoom", "Mouse 2"], ["Reload", "R"], ["Next weapon", "Wheel up"],
    ["Previous weapon", "Wheel down"], ["Weapon wheel", "Q"], ["Quick melee", "V"]] },
};
function settingsPage(kind) {
  const P = PAGES[kind], s = svgEl("svg", { viewBox: "-107 0 854 480", preserveAspectRatio: "xMidYMid slice", "aria-hidden": "true", focusable: "false" });
  const defs = svgEl("defs", {}, s), uid = `mu${kind}`;
  const lg = svgEl("linearGradient", { id: `${uid}l`, x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
  [[0, "#616F26", 0], [.5, "#616F26", .7], [1, "#616F26", 0]].forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, lg));
  const bk = svgEl("linearGradient", { id: `${uid}b`, x1: 0, y1: 0, x2: 1, y2: 0 }, defs);
  [[.0, 0], [.33, .6], [1, .6]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, bk));
  svgEl("feGaussianBlur", { stdDeviation: 3 }, svgEl("filter", { id: `${uid}r`, x: "-5%", y: "-50%", width: "110%", height: "200%" }, defs));
  svgEl("rect", { x: -107, y: 0, width: 854, height: 480, fill: "#0c0f09" }, s);
  svgEl("rect", { x: -107, y: 90, width: 854, height: 300, fill: `url(#${uid}l)`, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, s);
  svgEl("rect", { x: 0, y: 0, width: 747, height: 480, fill: `url(#${uid}b)` }, s);
  // section navigation
  ["GAME", "CONTROLS", "SYSTEM", "AUDIO", "ACCESSIBILITY"].forEach((t, i) => {
    const y = 172 + i * 30, on = i === P.section;
    svgEl("path", { d: `M14 ${y}H214L222 ${y + 8}V${y + 24}H14Z`, fill: on ? "#8B964B" : "#000", "fill-opacity": on ? .55 : .4 }, s);
    svgEl("path", { d: `M24 ${y + 9}l5 3.5-5 3.5z`, fill: on ? "#E38900" : "#909A49", "fill-opacity": on ? 1 : .4 }, s);
    const tx = svgEl("text", { x: 36, y: y + 16.5, "font-family": F_M, "font-size": 8.5, fill: on ? "#E38900" : "#fff", "fill-opacity": on ? 1 : .8 }, s); tx.textContent = t;
  });
  // content column: heading 14 u above the first row, rows on a 24 u pitch, labels at +31 u, values at +240 u
  const h = svgEl("text", { x: 259, y: 104, "font-family": F_M, "font-size": 8, fill: "#fff", "fill-opacity": .4, "letter-spacing": .5 }, s); h.textContent = P.heading;
  P.rows.forEach(([label, value], i) => {
    const y = 110 + i * 24;
    svgEl("path", { d: `M228 ${y}H616L624 ${y + 8}V${y + 22}H228Z`, fill: "#000", "fill-opacity": i === 1 ? .7 : .4 }, s);
    const l = svgEl("text", { x: 259, y: y + 15, "font-family": F_L, "font-size": 9.5, fill: "#fff", "fill-opacity": .8 }, s); l.textContent = label;
    if (typeof value === "number") {
      for (let k = 0; k < 16; k++) svgEl("rect", { x: 472 + k * 4.4, y: y + 15 - (3 + k * .45), width: 2, height: 3 + k * .45, fill: "#FFBE23", "fill-opacity": k / 16 < value ? .8 : .2 }, s);
      const n = svgEl("text", { x: 550, y: y + 15, "font-family": F_L, "font-size": 9.5, fill: "#FFBE23", "fill-opacity": .8 }, s); n.textContent = Math.round(value * 100);
    } else { const v = svgEl("text", { x: 468, y: y + 15, "font-family": F_L, "font-size": 9.5, fill: "#FFBE23", "fill-opacity": .8 }, s); v.textContent = value; }
  });
  // the bands in their page positions, with the rim light along their inner edges
  const top = TOP.map(([x, y]) => [x + 323, y - 63]), btm = BTM.map(([x, y]) => [x + 373, y + 35]);
  const line = pts => pts.map(p => p.join(" ")).join("L");
  svgEl("path", { d: `M${line(top)}`, fill: "none", stroke: "#626934", "stroke-width": 7, opacity: .7, filter: `url(#${uid}r)` }, s);
  svgEl("path", { d: `M${line(btm)}`, fill: "none", stroke: "#626934", "stroke-width": 7, opacity: .7, filter: `url(#${uid}r)` }, s);
  svgEl("path", { d: `M-400 -10L${line(top)}L1300 -10Z`, fill: "#000" }, s);
  svgEl("path", { d: `M-400 600L${line(btm)}L1300 600Z`, fill: "#000" }, s);
  const title = svgEl("text", { x: 39, y: 23, "font-family": F_M, "font-size": 12, fill: "#fff", "fill-opacity": .5 }, s);
  title.textContent = kind === "display" ? "SETTINGS - SYSTEM" : "SETTINGS - CONTROLS";
  svgEl("path", { d: "M532 441H634L641 448V471H532Z", fill: "#000", "fill-opacity": .5, stroke: "#8B964B", "stroke-opacity": .4 }, s);
  const back = svgEl("text", { x: 548, y: 460, "font-family": F_M, "font-size": 9, fill: "#fff", "fill-opacity": .8 }, s); back.textContent = "BACK";
  return s;
}

function underlay(stage, svg) {
  const u = document.createElement("div");
  u.className = "munder"; u.setAttribute("aria-hidden", "true"); u.inert = true;
  u.appendChild(svg); stage.prepend(u);
}
const home = document.getElementById("homeSvg"), quit = document.getElementById("mdemo");
if (home && quit) underlay(quit, cloneUnique(home, "mq"));
document.querySelectorAll(".mstage[data-under]").forEach(st => underlay(st, settingsPage(st.dataset.under)));
})();
