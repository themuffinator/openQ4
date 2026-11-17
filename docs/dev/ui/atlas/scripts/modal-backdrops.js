
(() => {
const A = window.__atlas, { svgEl } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif";
const OLIVE = "#8B964B", MARKER = "#909A49", ORANGE = "#E38900", VALUE = "#FFBE23", RAIL_DARK = "#CCCC51", TAB_GREEN = "#5A652A";
const MARINE = 0.69;                     // Michroma stands in for Marine at this fraction of its size
let gid = 0;
const uid = p => `${p}${++gid}`;
const grad = (defs, stops, attrs = {}) => {
  const id = uid("pg"), g = svgEl("linearGradient", { id, gradientUnits: "userSpaceOnUse", ...attrs }, defs);
  stops.forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${id})`;
};
const pts = list => list.map(p => p.join(" ")).join("L");
/* Draw `fn(svg, W, H)` into a fresh svg.<cls> at the host's laid-out size, again whenever the host resizes. */
function drawn(host, cls, fn) {
  const draw = () => {
    const W = host.offsetWidth, H = host.offsetHeight;
    if (!W || !H) return;
    host.querySelector(`:scope > svg.${cls}`)?.remove();
    const s = svgEl("svg", { class: cls, viewBox: `0 0 ${W} ${H}`, "aria-hidden": "true" });
    fn(s, W, H);
    host.prepend(s);
  };
  draw(); new ResizeObserver(draw).observe(host);
}

/* ---------------------------------------------------------------- plates (build_retained_screens.py, retained_saves.py,
   retained_difficulty.py): every number is the generator's, in dp on a 45 dp target. */
const PLATES = {
  // navigation_plate: b1_dark from past the view's edge; its fade runs over 413 u from the canvas edge (`lead` dp in)
  nav: { band: [4.2, 40.8], cut: 0, dark: true, lead: 40, span: 619.5, fill: [[0, 1], [.44, 1], [.62, .5], [.88, 0]],
         rails: "foot", mark: [52.5, 17.7, 9], focus: "nav" },
  // section_plate: b2_dark at 208 u, its band 13-37.4 u of 52 u centred on the 30 u target
  sec: { band: [4.2, 40.8], cut: 17.1, dark: true, fill: [[0, 1], [.57, 1], [.72, .5], [.94, 0]], mark: [16.125, 17.325, 8.25], focus: [.72] },
  // action_plate: light, 35 in 45 dp
  act: { band: [5, 40], cut: 20, fill: [[0, .49], [.33, .49], [1, 0]], rail: [[0, 1], [.33, 1], [1, 0]], mark: [14, 14, 8], focus: "act" },
  // primary_plate at 30 u (APPLY CHANGES): band rows 4-27 of 32, cut 13/32, marker (9, 8, 14) u
  prim: { band: [5.625, 37.97], cut: 18.28, fill: [[0, .49], [.31, .49], [.5, .37], [.75, .17], [.94, .055], [.953, 0]],
          rail: [[0, 1], [.31, 1], [.953, 0]], mark: [16.84, 17.25, 10.5], focus: [.6] },
};
function plate(el) {
  const P = PLATES[el.dataset.pg];
  if (!P) return;
  drawn(el, "pg-art", (s, W) => {
    const defs = svgEl("defs", {}, s), [t, b] = P.band, c = P.cut, x0 = P.lead || 0, x1 = P.span ? x0 + P.span : W;
    const span = { x1: x0, x2: x1, y1: 0, y2: 0 };
    const p = svgEl("g", { class: "pg-p" }, s);
    const fillPaint = P.dark ? grad(defs, P.fill.map(([o, a]) => [o, "#000", a]), span) : grad(defs, P.fill.map(([o, a]) => [o, OLIVE, a]), span);
    const railPaint = P.dark ? grad(defs, P.fill.map(([o, a]) => [o, RAIL_DARK, a]), span) : grad(defs, P.rail.map(([o, a]) => [o, OLIVE, a]), span);
    if (P.dark && x0) svgEl("rect", { x: -400, y: t, width: x0 + 400, height: b - t, fill: "#000" }, p);   // the part past the canvas edge
    svgEl("path", { d: c ? `M${x0} ${t}H${W}V${b}H${x0 + c}L${x0} ${b - c}Z` : `M${x0} ${t}H${W}V${b}H${x0}Z`, fill: fillPaint }, p);
    const rail = P.rails === "foot" ? `M-400 ${b - .75}H${W}` : `M.75 ${t}V${b - c}L${c} ${b - .75}H${W}`;
    svgEl("path", { d: rail, fill: "none", stroke: railPaint, "stroke-width": 1.5 }, p);
    if (P.rails === "foot") svgEl("path", { d: `M-400 ${b - .75}H${x0}`, stroke: RAIL_DARK, "stroke-width": 1.5 }, p);
    // focus: the inset rail, and the navigation plate's focus light
    const f = svgEl("g", { class: "pg-f" }, s), o = { fill: "none", stroke: ORANGE, "stroke-width": 1.2 };
    if (P.focus === "nav") {
      svgEl("path", { d: `M-400 ${b - 3.2}H${x0 + 348}`, ...o }, f);
      const light = grad(defs, [[0, "#616F26", 0], [.5, "#616F26", .5], [1, "#616F26", 0]], { x1: x0 + 20, x2: x0 + 70, y1: 0, y2: 0 });
      svgEl("rect", { x: x0 + 20, y: t - 2, width: 50, height: b - t + 4, fill: light, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, f);
    } else if (P.focus === "act") svgEl("path", { d: `M3.5 ${t + 3}V${b - c + 1.5}L${c + 1.5} ${b - 3.5}H${W - 20}`, ...o }, f);
    else svgEl("path", { d: `M${c + 2} ${b - 3.2}H${W * P.focus[0]}`, ...o }, f);
    const [mx, my, ms] = P.mark, tri = `M${x0 + mx} ${my}h${ms}v${ms}z`;
    svgEl("path", { class: "pg-mr", d: tri, fill: MARKER }, s);
    svgEl("path", { class: "pg-mh", d: tri, fill: ORANGE }, s);
  });
}
document.querySelectorAll(".pg[data-pg]").forEach(plate);

/* ---------------------------------------------------------------- settings rows (retained_mp_menus.value_row and
   retained_settings_rows): a 36 dp closed light plate with a 6 dp upper-leading cut, its wash and orange focus rail. */
const ROW_H = 36, TRACK_H = 26;
const vxOf = el => parseFloat(getComputedStyle(el).getPropertyValue("--vx")) || 160;
function sliderTicks(min, max, step) {
  const steps = Math.max(1, Math.round((max - min) / step));
  if (steps <= 40) return steps;
  for (const g of [2, 4, 5, 10, 20, 25, 40, 50, 100, 200, 250, 400, 500, 1000]) if (steps % g === 0 && steps / g <= 40) return steps / g;
  return 20;
}
function rowChrome(s, W, H, plateW = W) {
  const defs = svgEl("defs", {}, s), shape = `M6 1H${plateW}V${H - 1}H0V7Z`;
  svgEl("path", { d: shape, fill: OLIVE, "fill-opacity": .12, stroke: OLIVE, "stroke-opacity": .32, "stroke-width": 1 }, s);
  svgEl("path", { class: "sr-ws", d: shape, fill: grad(defs, [[0, OLIVE, .45], [1, OLIVE, .1]], { x1: 0, x2: plateW, y1: 0, y2: 0 }) }, s);
  svgEl("path", { class: "sr-rl", d: `M0 7L6 1V${H - 1}H0Z`, fill: ORANGE }, s);
  return defs;
}
function settingsRow(el) {
  const kind = el.dataset.sr, vx = () => vxOf(el);
  let state = { on: el.dataset.on === "1" };
  if (kind === "slider" || kind === "entry") {
    const [min, max, step, value, decimals] = el.dataset.range.split("|").map(Number);
    Object.assign(state, { min, max, step, value, decimals: decimals ?? (String(step).split(".")[1] || "").length });
  }
  const fmt = v => v.toFixed(Math.min(state.decimals ?? 0, 3));
  let num = null, field = null;
  if (kind === "slider") { num = document.createElement("span"); num.className = "sr-n"; el.append(num); }
  if (kind === "entry") {
    field = document.createElement("span"); field.className = "vl-field"; field.style.cssText = "position:absolute;right:0;top:7px;width:66px";
    el.append(field);
  }
  const draw = (s, W, H) => {
    const x = vx(), valueW = W - x - 12;
    if (kind === "entry") {
      const sliderW = W - 66 - 6, trackW = sliderW - x - 12 - 56 + 48;
      rowChrome(s, sliderW, H);
      ramp(s, x, trackW);
      field.textContent = fmt(state.value);
      return;
    }
    rowChrome(s, W, H);
    if (kind === "choice") svgEl("path", { d: `M${W - 24} 15l6 6 6-6`, fill: "none", stroke: VALUE, "stroke-width": 1.5 }, s);
    if (kind === "toggle") {
      svgEl("rect", { x: x + .75, y: 11.75, width: 12.5, height: 12.5, fill: "none", stroke: VALUE, "stroke-width": 1.5 }, s);
      if (state.on) svgEl("rect", { x: x + 3.5, y: 14.5, width: 7, height: 7, fill: VALUE }, s);
    }
    if (kind === "slider") {
      const trackW = valueW - 56;
      ramp(s, x, trackW);
      num.style.left = `${x + trackW + 8}px`; num.textContent = fmt(state.value);
    }
    s.dataset.track = kind === "slider" || kind === "entry" ? "1" : "";
  };
  // the tick track: a line per stop from 6 to 16 dp, a 4 dp fill bar to the 4x16 dp thumb, centred 13 dp down
  const ramp = (s, x, trackW) => {
    const n = sliderTicks(state.min, state.max, state.step), f = (state.value - state.min) / (state.max - state.min);
    const g = svgEl("g", { transform: `translate(${x} 0)` }, s);
    for (let i = 0; i <= n; i++) { const tx = trackW * i / n, h = 3 + 5 * i / n;
      svgEl("path", { d: `M${tx} ${13 - h}V${13 + h}`, stroke: "#fff", "stroke-opacity": .45, "stroke-width": 1 }, g); }
    svgEl("rect", { x: 0, y: 11, width: Math.max(0, f * trackW), height: 4, fill: VALUE, "fill-opacity": .8 }, g);
    svgEl("rect", { x: Math.min(trackW - 4, Math.max(0, f * trackW - 2)), y: 5, width: 4, height: 16, fill: VALUE }, g);
    el._track = { x, w: trackW };
  };
  const redraw = () => { el.querySelector(":scope > svg.sr-art")?.remove(); const W = el.offsetWidth; if (!W) return;
    const s = svgEl("svg", { class: "sr-art", viewBox: `0 0 ${W} ${ROW_H}`, "aria-hidden": "true" }); draw(s, W, ROW_H); el.prepend(s); };
  redraw(); new ResizeObserver(redraw).observe(el);
  el.setAttribute("role", { toggle: "checkbox", slider: "slider", entry: "slider", choice: "button" }[kind] || "button");
  if (kind === "toggle") {
    el.setAttribute("aria-checked", state.on);
    const flip = () => { state.on = !state.on; el.setAttribute("aria-checked", state.on); redraw(); };
    el.addEventListener("click", flip);
    el.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); flip(); } });
  }
  if (kind === "slider" || kind === "entry") {
    const set = v => { const k = Math.round((Math.min(state.max, Math.max(state.min, v)) - state.min) / state.step);
      state.value = +(state.min + k * state.step).toFixed(6); el.setAttribute("aria-valuenow", state.value); redraw(); };
    const fromX = cx => { const r = el.getBoundingClientRect(), t = el._track; set(state.min + (cx - r.left - t.x) / t.w * (state.max - state.min)); };
    el.addEventListener("pointerdown", e => { const t = el._track, r = el.getBoundingClientRect();
      if (!t || e.clientX - r.left < t.x - 6) return; el.setPointerCapture(e.pointerId); fromX(e.clientX); });
    el.addEventListener("pointermove", e => { if (el.hasPointerCapture(e.pointerId)) fromX(e.clientX); });
    el.addEventListener("keydown", e => { const d = { ArrowRight: 1, ArrowLeft: -1 }[e.key]; if (d) { e.preventDefault(); set(state.value + d * state.step); } });
    el.setAttribute("aria-valuemin", state.min); el.setAttribute("aria-valuemax", state.max); el.setAttribute("aria-valuenow", state.value);
  }
  if (kind === "choice" && el.dataset.options) choiceList(el);
}

/* A choice's list (choice_popup, placed by the runtime under its row, as wide as the row): black 0.96 in a #B2CD43 0.20
   hairline over a 4 dp black frame, 22 dp rows, the chosen option olive 0.35 and the highlight orange 0.30. */
function choiceList(row) {
  const pop = document.createElement("div"), vp = document.createElement("div"), options = row.dataset.options.split("|");
  pop.className = "sr-pop"; pop.setAttribute("role", "listbox"); vp.className = "sr-vp";
  const shown = Math.min(8, options.length);
  vp.style.height = `${shown * 22}px`;
  let pick = +(row.dataset.pick || 0);
  options.forEach((text, i) => { const o = document.createElement("div"); o.className = "sr-o" + (i === pick ? " sel hi" : ""); o.textContent = text;
    o.setAttribute("role", "option"); vp.append(o); });
  pop.append(vp); row.append(pop);
  // eight rows, then the list scrolls: an 8 dp track 12 dp from the end, the trough white 0.12, the thumb olive 0.90
  let thumb = null;
  const place = () => { if (!thumb) return; const H = shown * 22, full = options.length * 22, th = Math.max(24, H * H / full);
    thumb.setAttribute("height", th); thumb.setAttribute("y", 2 + (H - th) * vp.scrollTop / Math.max(1, full - H)); };
  drawn(pop, "sr-art", (s, W, H) => {
    svgEl("rect", { x: 0, y: 0, width: W, height: H, fill: "none", stroke: "#000", "stroke-width": 4 }, s);
    svgEl("rect", { x: 0, y: 0, width: W, height: H, fill: "#000", "fill-opacity": .96, stroke: "#B2CD43", "stroke-opacity": .2, "stroke-width": 1 }, s);
    thumb = null;
    if (options.length > 8) {
      svgEl("rect", { x: W - 9, y: 2, width: 2, height: H - 4, fill: "#fff", "fill-opacity": .12 }, s);
      thumb = svgEl("rect", { x: W - 11, y: 2, width: 6, height: 24, fill: OLIVE, "fill-opacity": .9 }, s); place();
    }
  });
  vp.addEventListener("scroll", place);
  const open = on => { pop.classList.toggle("open", on); row.setAttribute("aria-expanded", on);
    if (on) { vp.scrollTop = Math.max(0, (pick - shown + 2) * 22); place(); } };
  row.setAttribute("aria-haspopup", "listbox");
  open(row.dataset.open === "1");
  row.addEventListener("click", e => { if (pop.contains(e.target)) return; e.stopPropagation(); open(!pop.classList.contains("open")); });
  vp.querySelectorAll(".sr-o").forEach((o, i) => {
    o.addEventListener("pointerenter", () => vp.querySelectorAll(".sr-o").forEach(x => x.classList.toggle("hi", x === o)));
    o.addEventListener("click", e => { e.stopPropagation(); pick = i;
      vp.querySelectorAll(".sr-o").forEach(x => x.classList.toggle("sel", x === o)); row.querySelector(".sr-v").textContent = o.textContent; open(false); });
  });
  document.addEventListener("click", e => { if (!row.contains(e.target)) open(false); });
  row.addEventListener("keydown", e => { if (e.key === "Escape") open(false); if (e.key === "Enter") open(!pop.classList.contains("open")); });
}
document.querySelectorAll(".sr[data-sr]").forEach(settingsRow);

/* The server browser's filters (retained_join_game: spinner_square, source_tag, filter_tag). */
document.querySelectorAll(".vl-spin").forEach(sp => {
  const vals = sp.dataset.values.split("|"), out = sp.querySelector("output"); let i = 0;
  sp.querySelector(".pv").addEventListener("click", () => { i = (i + vals.length - 1) % vals.length; out.textContent = vals[i]; });
  sp.querySelector(".nx").addEventListener("click", () => { i = (i + 1) % vals.length; out.textContent = vals[i]; });
});
document.querySelectorAll(".vl-tag").forEach(tag => {
  drawn(tag, "tagart", (s, W, H) => {
    const c = 7.5, d = `M${c} 0H${W}V${H}H0V${c}Z`;
    svgEl("path", { class: "ol", d, fill: "none", stroke: OLIVE, "stroke-width": 1 }, s);
    const lit = svgEl("g", { class: "lit", opacity: .6 }, s);
    svgEl("path", { d, fill: ORANGE, "fill-opacity": .35, stroke: ORANGE, "stroke-width": 1 }, lit);
    if (tag.dataset.tag === "filter") svgEl("path", { class: "x", d: `M${W - 16} ${H / 2 - 4}l8 8M${W - 8} ${H / 2 - 4}l-8 8`, stroke: VALUE, "stroke-width": 1.5 }, s);
  });
  tag.addEventListener("click", () => {
    if (tag.dataset.tag === "src") tag.parentElement.querySelectorAll(".vl-tag").forEach(t => t.classList.toggle("on", t === tag));
    else tag.classList.toggle("on");
  });
});

/* List rows (retained_saves.save_row, retained_join_game.server_row): the first click selects and focuses a row. */
document.querySelectorAll(".ls-list").forEach(list => list.querySelectorAll(".ls-row").forEach(row => row.addEventListener("click", () =>
  list.querySelectorAll(".ls-row").forEach(r => { r.classList.toggle("sel", r === row); r.classList.toggle("f", r === row); r.classList.remove("hov"); }))));

/* ---------------------------------------------------------------- modal backdrops (section 6): the screen beneath a
   modal, drawn under its soft focus. A copy of a drawn screen gets its own ids, so its gradients never collide. */
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

/* A settings category as the retained sub-page (retained_settings_rows.settings_subpage, retained_subpage): the top band
   one notch on, SETTINGS as the crumb and the category's title at the step's foot, the tab strip across the content's
   top (STRIP_TOP 46 u), the heading line, two columns of 287 u on the 24 u pitch from 87 u, the backing, and the prompt
   bar in the bottom band. DISPLAY keeps the draft's status and APPLY CHANGES under its columns. Drawn in dp
   (1.5 dp a stock unit) on a 16:9 view of the 960 dp canvas. */
const U = 1.5;
const TOP = [[-297.9, 89.7], [-283.7, 103.8], [30.7, 103.8], [40.9, 94.7], [76.6, 94.7], [85.8, 103.8], [127.6, 103.8], [137.8, 94.7],
  [172.5, 94.7], [181.7, 103.8], [223.5, 103.8], [233.7, 94.7], [268.4, 94.7], [277.6, 103.8], [315.4, 103.8], [352.1, 68.5]];
const BTM = [[-232.7, 421.5], [-222.5, 430.6], [-28.6, 430.6], [2.1, 400.4], [344.9, 400.4], [389.8, 443.7], [582.7, 443.7], [591.9, 434.6]];
const PAGES = {
  display: { title: "DISPLAY", tabs: ["GRAPHICS", "SCREEN", "IMAGE QUALITY"], vx: 181, footer: true,
    heads: ["GRAPHICS", "INTERFACE"],
    columns: [[["Performance Preset", "choice", "Enhanced"], ["Auto-Detect", "button"], ["Rendering Backend", "choice", "OpenGL"],
               ["Rendering Device", "choice", "Automatic (high performance)"], ["Renderer Fallback", "choice", "Auto"]],
              [["UI Scale", "choice", "100%"], ["Text Size", "choice", "100%"], ["UI Aspect", "toggle", true], ["Reset UI Sizes", "button"]]] },
  controls: { title: "CONTROLS", tabs: ["MOVEMENT", "WEAPONS", "ATTACK/LOOK", "OTHER", "MOUSE", "CONTROLLER"], active: 1, vx: 160,
    note: "Select an action, then press its new key. Escape cancels; Delete clears.",
    columns: [[["Attack", "keys", ["MOUSE1"]], ["Zoom", "keys", ["MOUSE2"]], ["Reload", "keys", ["R"]], ["Next Weapon", "keys", ["MWHEELUP"]],
               ["Previous Weapon", "keys", ["MWHEELDOWN"]]],
              [["Weapon Wheel", "keys", ["Q"]], ["Quick Melee", "keys", ["V"]], ["Last Weapon", "keys", ["TAB"]], ["Flashlight", "keys", ["F"]]]] },
};
function settingsPage(kind) {
  const P = PAGES[kind], s = svgEl("svg", { viewBox: "-160 0 1280 720", preserveAspectRatio: "xMidYMid slice", "aria-hidden": "true", focusable: "false" });
  const defs = svgEl("defs", {}, s), D = (stops, a) => grad(defs, stops, a);
  svgEl("rect", { x: -160, y: 0, width: 1280, height: 720, fill: "#000" }, s);
  // the lit field: vignette, the additive light band, the reticle grid
  svgEl("rect", { x: -160, y: 0, width: 1280, height: 720, fill: D([[.1, "#616F26", 0], [.5, "#616F26", .7], [.9, "#616F26", 0]], { x1: 0, x2: 0, y1: 0, y2: 720 }),
    style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, s);
  svgEl("rect", { x: -160, y: 0, width: 1280, height: 720, fill: D([[0, "#000", .97], [.3, "#000", 0], [.7, "#000", 0], [1, "#000", .97]], { x1: 0, x2: 0, y1: 0, y2: 720 }) }, s);
  let grid = "";
  for (let cx = -8; cx < 29; cx++) for (let r = 0; r < 16; r++) { const x = U * (15 + 30 * cx), y = U * (15 + 30 * r); grid += `M${x - 9.75} ${y}h19.5M${x} ${y - 9.75}v19.5`; }
  svgEl("path", { d: grid, stroke: "#fff", "stroke-opacity": .04, "stroke-width": 1 }, s);
  // the backing (settings_backing): black 0.60 from 4 u, full from 40 u to 636 u, gone by 660 u
  svgEl("rect", { x: -160, y: 0, width: 1280, height: 720, fill: D([[0, "#000", 0], [36 / 656, "#000", .6], [632 / 656, "#000", .6], [1, "#000", 0]], { x1: 6, x2: 990, y1: 0, y2: 0 }) }, s);
  const L = U * 44, CW = U * 587, colW = U * 287, gap = U * 13;
  // the tab strip (retained_mp_menus.tab_strip): keycaps 12 dp in, the tabs sharing the rest, the active one risen
  const stripTop = U * 46, rise = 30, base = stripTop + rise, inner = 12 + 20 + 6;
  svgEl("rect", { x: L + 12, y: base, width: CW - 24, height: rise, fill: D([[0, TAB_GREEN, .57], [1, TAB_GREEN, .02]], { x1: 0, x2: 0, y1: base, y2: base + rise }) }, s);
  const font = 14 * MARINE, widths = P.tabs.map(t => t.length * font * .98 + 26), spare = Math.max(0, CW - 2 * inner - widths.reduce((a, b) => a + b, 0)) / P.tabs.length;
  let tx = L + inner; const active = P.active || 0;
  P.tabs.forEach((t, i) => {
    const w = widths[i] + spare;
    if (i === active) {
      const edge = `M${tx} ${base}L${tx + 3} ${base - 3}V${stripTop}H${tx + w - 18}L${tx + w} ${stripTop + 18}V${base}`;
      svgEl("path", { d: edge + "Z", fill: D([[0, TAB_GREEN, 1], [1, TAB_GREEN, .6]], { x1: 0, x2: 0, y1: stripTop, y2: base }) }, s);
      svgEl("path", { d: edge, fill: "none", stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 }, s);
    } else svgEl("path", { d: `M${tx} ${base - .75}H${tx + w}`, stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 }, s);
    const lab = svgEl("text", { x: tx + 8 + (w - 26) / 2, y: base - 11, "text-anchor": "middle", "font-family": F_M, "font-size": font, fill: "#fff", "fill-opacity": i === active ? 1 : .55 }, s);
    lab.textContent = t; tx += w;
  });
  [[L + 12, L + inner], [L + CW - inner, L + CW - 12]].forEach(([a, b]) => svgEl("path", { d: `M${a} ${base - .75}H${b}`, stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 }, s));
  [["Q", L + 12], ["E", L + CW - 32]].forEach(([k, x]) => {
    svgEl("path", { d: `M${x} ${stripTop + 4}h20v22h-14l-6-6z`, fill: "#fff", "fill-opacity": .85 }, s);
    svgEl("text", { x: x + 10, y: stripTop + 19.5, "text-anchor": "middle", "font-family": F_L, "font-weight": 600, "font-size": 13, fill: "#0A0D09" }, s).textContent = k;
  });
  // the heading line: the columns' headings, or the page's note, 21 dp in
  const headY = U * 70 + U * 8 + 5;
  if (P.note) svgEl("text", { x: L + 21, y: headY, "font-family": F_L, "font-weight": 500, "font-size": 14, fill: "#fff", "fill-opacity": .4 }, s).textContent = P.note;
  (P.heads || []).forEach((h, i) => svgEl("text", { x: L + i * (colW + gap) + 21, y: headY, "font-family": F_L, "font-weight": 600, "font-size": 14, fill: "#fff", "fill-opacity": .4 }, s).textContent = h);
  // the rows
  P.columns.forEach((col, side) => col.forEach(([label, k, value], i) => {
    const x = L + side * (colW + gap), y = U * 87 + i * U * 24, g = svgEl("g", { transform: `translate(${x} ${y})` }, s);
    const focused = side === 0 && i === 0, vx = P.vx;
    svgEl("path", { d: `M6 1H${colW}V35H0V7Z`, fill: OLIVE, "fill-opacity": .12, stroke: OLIVE, "stroke-opacity": .32 }, g);
    if (focused) { svgEl("path", { d: `M6 1H${colW}V35H0V7Z`, fill: D([[0, OLIVE, .45], [1, OLIVE, .1]], { x1: x, x2: x + colW, y1: 0, y2: 0 }) }, g);
      svgEl("path", { d: "M0 7L6 1V35H0Z", fill: ORANGE }, g); }
    svgEl("text", { x: 21, y: 23, "font-family": F_L, "font-weight": 500, "font-size": 15, fill: focused ? ORANGE : "#fff", "fill-opacity": focused ? 1 : .85 }, g).textContent = label;
    if (k === "choice") { svgEl("text", { x: vx, y: 23, "font-family": F_L, "font-weight": 500, "font-size": 14, fill: VALUE }, g).textContent = value;
      svgEl("path", { d: `M${colW - 24} 15l6 6 6-6`, fill: "none", stroke: VALUE, "stroke-width": 1.5 }, g); }
    if (k === "toggle") { svgEl("rect", { x: vx + .75, y: 11.75, width: 12.5, height: 12.5, fill: "none", stroke: VALUE, "stroke-width": 1.5 }, g);
      if (value) svgEl("rect", { x: vx + 3.5, y: 14.5, width: 7, height: 7, fill: VALUE }, g); }
    if (k === "keys") { let kx = vx; value.forEach(key => { const w = key.length * 7.2 + 14;
      svgEl("path", { d: `M${kx} 8h${w}v20h${-w + 5.5}l-5.5-5.5z`, fill: "#fff", "fill-opacity": .85 }, g);
      svgEl("text", { x: kx + w / 2, y: 22, "text-anchor": "middle", "font-family": F_L, "font-weight": 600, "font-size": 11, fill: "#0A0D09" }, g).textContent = key; kx += w + 4; }); }
  }));
  // DISPLAY's footer: the draft's status, and APPLY CHANGES (unavailable while the draft holds no changes)
  if (P.footer) {
    svgEl("text", { x: L + 21, y: U * 396, "font-family": F_L, "font-weight": 500, "font-size": 15, fill: "#fff", "fill-opacity": .85 }, s).textContent = "Ready";
    const ax = L + colW + gap + 20, ay = U * 384, aw = 380, g = svgEl("g", { transform: `translate(${ax} ${ay})`, opacity: .4 }, s);
    svgEl("path", { d: `M0 5.6H${aw}V38H18.3L0 19.7Z`, fill: D([[0, OLIVE, .49], [.31, OLIVE, .49], [.953, OLIVE, 0]], { x1: ax, x2: ax + aw, y1: 0, y2: 0 }) }, g);
    svgEl("path", { d: `M.75 5.6V19.7L18.3 37.2H${aw}`, fill: "none", stroke: D([[0, OLIVE, 1], [.31, OLIVE, 1], [.953, OLIVE, 0]], { x1: ax, x2: ax + aw, y1: 0, y2: 0 }), "stroke-width": 1.5 }, g);
    svgEl("path", { d: "M16.8 17.2h10.5v10.5z", fill: MARKER }, g);
    svgEl("text", { x: 42, y: 27, "font-family": F_M, "font-size": 13.8, fill: "#fff", "fill-opacity": .8 }, g).textContent = "APPLY CHANGES";
  }
  // the framing bands, docked for a sub-page, with the rim light along their inner edges
  const top = [[-400, U * (89.7 - 63)], ...TOP.map(([x, y]) => [U * (x + 420), U * (y - 63)]), [1500, U * (68.5 - 63)]];
  const btm = [[-400, U * (421.5 + 35)], ...BTM.map(([x, y]) => [U * (x + 373), U * (y + 35)]), [1500, U * (434.6 + 35)]];
  const rim = svgEl("filter", { id: uid("rim"), x: "-5%", y: "-50%", width: "110%", height: "200%" }, defs);
  svgEl("feGaussianBlur", { stdDeviation: 9 }, rim);
  [top, btm].forEach(edge => svgEl("path", { d: `M${pts(edge)}`, fill: "none", stroke: "#626934", "stroke-width": 22, opacity: .75, filter: `url(#${rim.id})` }, s));
  svgEl("path", { d: `M-400 -10L${pts(top)}L1500 -10Z`, fill: "#000" }, s);
  svgEl("path", { d: `M-400 900L${pts(btm)}L1500 900Z`, fill: "#000" }, s);
  // the crumb and the title, and the prompt bar
  svgEl("text", { x: U * 39, y: U * 12 + 13, "font-family": F_M, "font-size": 18 * MARINE * .8, fill: "#fff", "fill-opacity": .4, "letter-spacing": 1.2 }, s).textContent = "SETTINGS";
  svgEl("text", { x: U * 136, y: U * 19 + 19, "font-family": F_M, "font-size": 18 * MARINE, fill: "#fff", "fill-opacity": .5, "letter-spacing": 1.5 }, s).textContent = P.title;
  [["ENTER", "SELECT", 656], ["ESCAPE", "BACK", 806]].forEach(([k, v, x]) => {
    const w = k.length * 8.4 + 12;
    svgEl("path", { d: `M${x} 673h${w}v22h${-w + 6}l-6-6z`, fill: "#fff", "fill-opacity": .85 }, s);
    svgEl("text", { x: x + w / 2, y: 689, "text-anchor": "middle", "font-family": F_L, "font-weight": 600, "font-size": 13, fill: "#0A0D09" }, s).textContent = k;
    svgEl("text", { x: x + w + 7, y: 688.5, "font-family": F_M, "font-size": 14 * MARINE, fill: "#fff", "fill-opacity": .8, "letter-spacing": 1 }, s).textContent = v;
  });
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
A.settingsPage = settingsPage;
})();
