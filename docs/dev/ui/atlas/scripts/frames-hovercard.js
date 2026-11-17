
(() => {
const $ = id => document.getElementById(id);
const A = window.__atlas, { svgEl } = A;
const OLIVE = "#8B964B", ORANGE = "#E38900", TAB_GREEN = "#5A652A", GLOW_MODAL = "#454E1B", HOT = "#EEF2A2";
let gid = 0;
const grad = (defs, stops, attrs) => {
  const id = `fm${++gid}`, g = svgEl("linearGradient", { id, gradientUnits: "userSpaceOnUse", ...attrs }, defs);
  stops.forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${id})`;
};
const pts = list => "M" + list.map(p => p.map(v => +v.toFixed(3)).join(" ")).join("L");
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
  document.fonts?.ready.then(draw);
  return draw;
}

/* Hover card: appears at once 6 dp from the pointer; near the trailing edge it moves to the leading side. */
const zone = $("hzone"), card = $("hcard"), kv = $("hcardKv");
if (zone) {
  let current = null;
  zone.querySelectorAll(".trow[data-card]").forEach(row => {
    row.addEventListener("pointerenter", () => {
      const [name, map, mode, players, ping] = row.dataset.card.split("|");
      kv.innerHTML = `<span>Server</span><b>${name}</b><span>Map</span><b>${map}</b><span>Mode</span><b>${mode}</b><span>Players</span><b>${players}</b><span>Ping</span><b>${ping}</b>`;
      card.classList.add("show"); current = row;
    });
    row.addEventListener("pointerleave", () => { if (current === row) { card.classList.remove("show"); current = null; } });
  });
  zone.addEventListener("pointermove", e => {
    if (!current) return;
    const z = zone.getBoundingClientRect(), x = e.clientX - z.left, y = e.clientY - z.top;
    const w = card.offsetWidth, h = card.offsetHeight;
    const left = x + 6 + w > z.width - 16 ? x - 6 - w : x + 6;
    card.style.left = Math.max(16, left) + "px";
    card.style.top = Math.min(Math.max(8, y + 6), z.height - h - 8) + "px";
  });
  /* Tooltip: 300 ms delay, 100 ms fade; 12 dp after and 15 dp below the pointer. */
  const info = $("infoBtn"), tip = $("tip"); let timer = null;
  const place = e => { const z = zone.getBoundingClientRect(); const x = e.clientX - z.left + 12, y = e.clientY - z.top + 15;
    tip.style.left = Math.min(x, z.width - tip.offsetWidth - 16) + "px"; tip.style.top = Math.min(y, z.height - tip.offsetHeight - 8) + "px"; };
  info.addEventListener("pointerenter", e => { place(e); clearTimeout(timer); timer = setTimeout(() => tip.classList.add("show"), 300); });
  info.addEventListener("pointermove", place);
  info.addEventListener("pointerleave", () => { clearTimeout(timer); tip.classList.remove("show"); });
  info.addEventListener("focus", () => { const r = info.getBoundingClientRect(); place({ clientX: r.right, clientY: r.bottom }); tip.classList.add("show"); });
  info.addEventListener("blur", () => tip.classList.remove("show"));
}

/* The stock modal's silhouette with its black frame (build_retained_screens.modal_silhouette and confirmation, user rules
   of 5 and 11 November 2026), at the dialog's laid-out width W and height H in dp: the art inset 7/512 of the width each
   side, 3.5625 dp above the raised sections and 5.34 dp below the foot; the leading tooth to texel 14 and the 17 dp slot
   to texel 388; the raised section's trailing corner cut 13 dp; a lug 4 dp out of the trailing edge 38 dp under the
   raised line for 56 dp; a 6 dp notch in the foot 58 dp from the trailing edge for 88 dp; a 38 dp lower-leading chamfer. */
function modalOutline(W, H, notch = true) {
  const inset = W * 7 / 512, raised = 3.5625, bottom = H - 5.34375, lead = W * 14 / 512, trail = W * 388 / 512, d = 17, right = W - inset;
  const lug = raised + 38, nt = right - 58, nl = nt - 88;
  const foot = notch ? [[nt, bottom], [nt - 6, bottom - 6], [nl + 6, bottom - 6], [nl, bottom]] : [];
  return { inset, raised, lead, trail, d, right, points: [[inset, raised], [lead, raised], [lead + d, raised + d], [trail - d, raised + d], [trail, raised],
    [right - 13, raised], [right, raised + 13], [right, lug], [right + 4, lug + 4], [right + 4, lug + 52], [right, lug + 56], [right, bottom],
    ...foot, [inset + 38, bottom], [inset, bottom - 38]] };
}
function modalFrame(host) {
  const title = host.querySelector(".mt");
  drawn(host, "fm-art", (s, W, H) => {
    const o = modalOutline(W, H, H >= 187 + 5.34 + 6), outline = pts(o.points) + "Z", defs = svgEl("defs", {}, s), rim = 3;
    // the energy (modal_energy): under the frame and over the glow, only while the contents show
    const en = svgEl("g", { class: "fm-energy", style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, s);
    svgEl("path", { class: "fm-halo", d: outline, fill: "none", stroke: HOT, "stroke-opacity": .3, "stroke-width": rim + 6, "stroke-linejoin": "miter" }, en);
    const cx = W / 2, cy = H / 2, reach = Math.hypot(cx, cy) + 12, tail = 2 * Math.PI * .1;
    const spoke = a => [cx + 1.06 * reach * Math.sin(a), cy - 1.06 * reach * Math.cos(a)];
    const mask = svgEl("mask", { id: `fm${++gid}`, maskUnits: "userSpaceOnUse", x: -50, y: -50, width: W + 100, height: H + 100 }, defs);
    const spin = svgEl("g", { class: "fm-spin", style: `transform-origin:${cx}px ${cy}px` }, mask);
    [0, Math.PI].forEach(at => svgEl("path", { d: pts([[cx, cy], spoke(at - tail), spoke(at - tail / 2), spoke(at)]) + "Z",
      fill: grad(defs, [[0, "#fff", 0], [.72, "#fff", .5], [1, "#fff", 1]], { x1: spoke(at - tail)[0], y1: spoke(at - tail)[1], x2: spoke(at)[0], y2: spoke(at)[1] }) }, spin));
    svgEl("path", { d: outline, fill: "none", stroke: HOT, "stroke-opacity": .9, "stroke-width": rim + 6, mask: `url(#${mask.id})` }, en);
    svgEl("rect", { class: "fm-wave", x: -16, y: 0, width: W + 32, height: 46, style: `--from:-46px;--to:${H}px`,
      fill: grad(defs, [[0, GLOW_MODAL, 0], [.6, GLOW_MODAL, .7], [1, GLOW_MODAL, 0]], { x1: 0, x2: 0, y1: 0, y2: 46 }) }, en);
    // the silhouette at 0.70 and its outline in black 3 dp, on top of the glow
    svgEl("path", { d: outline, fill: "#000", "fill-opacity": .7 }, s);
    svgEl("path", { d: outline, fill: "none", stroke: "#000", "stroke-width": rim, "stroke-linejoin": "miter" }, s);
    // the title's tab: from the frame's leading edge, black behind the title, stepping down 45 degrees to the slot's floor
    // 10 dp past the title's end (capped where the slot's trailing flank begins), with the corner marker before the title
    const foot = o.lead + o.d, left = o.inset - rim / 2, cap = o.trail - o.d, run = title ? title.offsetWidth : cap - foot;
    const end = Math.min(foot + run + 10, cap) + o.d, h = o.d + rim / 2, y = o.raised;
    svgEl("path", { d: pts([[left, y], [end - o.d, y], [end, y + o.d], [end, y + h], [left, y + h]]) + "Z", fill: "#000" }, s);
    svgEl("path", { d: pts([[left, y], [end - o.d, y], [end, y + o.d]]), fill: "none", stroke: "#000", "stroke-width": rim }, s);
    svgEl("path", { d: `M${foot - 14} ${y + (h - 6) / 2}h6v6z`, fill: ORANGE, "fill-opacity": .9 }, s);
  });
}
const md = { scrim: $("mdScrim"), glow: $("mdGlow"), frame: $("mdFrame"), acts: $("mdActs"), title: $("mdTitle"), body: $("mdBody") };
if (md.frame) modalFrame(md.frame);
A.modalFrame = modalFrame;

/* Modal choreography (section 8, modal.enter and modal.leave); the energy runs while the contents show. */
if (md.scrim) {
  let raf = null, live = null;
  const tracks = spec => {
    cancelAnimationFrame(raf);
    const t0 = performance.now(), from = {};
    Object.keys(spec).forEach(k => from[k] = +getComputedStyle(md[k]).opacity);
    const step = now => {
      const t = now - t0; let busy = false;
      for (const [k, [start, dur, to]] of Object.entries(spec)) {
        const p = dur ? Math.min(1, Math.max(0, (t - start) / dur)) : (t >= start ? 1 : 0);
        md[k].style.opacity = from[k] + (to - from[k]) * p; if (p < 1) busy = true;
      }
      if (busy) raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
  };
  const enter = () => { ["scrim", "glow", "frame", "acts", "title", "body"].forEach(k => md[k].style.opacity = 0);
    md.frame.classList.remove("fm-live"); clearTimeout(live); live = setTimeout(() => md.frame.classList.add("fm-live"), 200);
    tracks({ scrim: [0, 200, 1], glow: [0, 200, 1], frame: [0, 200, 1], title: [200, 0, 1], body: [200, 0, 1], acts: [200, 0, 1] }); };
  const leave = () => { clearTimeout(live); md.frame.classList.remove("fm-live");
    tracks({ title: [0, 0, 0], body: [0, 0, 0], acts: [0, 0, 0], scrim: [50, 250, 0], glow: [50, 250, 0], frame: [50, 200, 0] }); };
  $("mdEnter").addEventListener("click", enter);
  $("mdLeave").addEventListener("click", leave);
}

/* The DISPLAY page's plate dialog (retained_system.dialog and dialog_button): cut 12 dp at the top corners and the
   bottom-trailing one, #090C08 in a 1 dp olive 0.70 outline over a 4 dp black stroke; its actions cut 6 dp. */
const cutPlate = (W, H, c) => pts([[c, 0], [W - c, 0], [W, c], [W, H - c], [W - c, H], [0, H], [0, c]]) + "Z";
const dlg = $("fmDlg");
if (dlg) {
  drawn(dlg, "fm-art", (s, W, H) => {
    svgEl("path", { d: cutPlate(W, H, 12), fill: "none", stroke: "#000", "stroke-width": 4 }, s);
    svgEl("path", { d: cutPlate(W, H, 12), fill: "#090C08", stroke: OLIVE, "stroke-opacity": .7, "stroke-width": 1 }, s);
  });
  dlg.querySelectorAll(".fm-btn").forEach(b => drawn(b, "fm-bart", (s, W, H) => {
    svgEl("path", { d: cutPlate(W, H, 6), fill: "#090C08", "fill-opacity": .96, stroke: OLIVE, "stroke-opacity": .6, "stroke-width": 1 }, s);
    svgEl("path", { class: "fo", d: cutPlate(W, H, 6), fill: "none", stroke: ORANGE, "stroke-width": 2 }, s);
  }));
  // the countdown runs from 15 while the page is in view, and starts again
  const count = $("fmCount"); let n = 15;
  setInterval(() => { if (dlg.getBoundingClientRect().bottom > 0) { n = n > 1 ? n - 1 : 15; count.textContent = n; } }, 1000);
}

/* The tab strip (retained_mp_menus.tab and tab_strip): the active tab's silhouette and the broken baseline. */
const strip = $("fmStrip");
if (strip) {
  const tabs = [...strip.querySelectorAll(".fm-tab")], body = $("fmTabBody");
  const draw = drawn(strip, "fm-sart", (s, W) => {
    const defs = svgEl("defs", {}, s), h = 30, line = { stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5, fill: "none" };
    svgEl("rect", { x: 12, y: h, width: W - 24, height: h, fill: grad(defs, [[0, TAB_GREEN, .57], [1, TAB_GREEN, .02]], { x1: 0, x2: 0, y1: h, y2: 2 * h }) }, s);
    svgEl("path", { d: `M12 ${h - .75}H38M${W - 38} ${h - .75}H${W - 12}`, ...line }, s);
    tabs.forEach(t => {
      const a = t.offsetLeft, b = a + t.offsetWidth;
      if (!t.classList.contains("on")) { svgEl("path", { d: `M${a} ${h - .75}H${b}`, ...line }, s); return; }
      const edge = pts([[a, h], [a + 3, h - 3], [a + 3, .75], [b - 18, .75], [b, 18.75], [b, h]]);
      svgEl("path", { d: edge + "Z", fill: grad(defs, [[0, TAB_GREEN, 1], [1, TAB_GREEN, .6]], { x1: 0, x2: 0, y1: 0, y2: h }) }, s);
      svgEl("path", { d: edge, ...line }, s);
    });
  });
  const pick = t => { tabs.forEach(x => { x.classList.toggle("on", x === t); x.setAttribute("aria-selected", x === t); }); body.textContent = t.dataset.body; draw(); };
  tabs.forEach(t => t.addEventListener("click", () => pick(t)));
  const step = d => { const i = tabs.findIndex(t => t.classList.contains("on")); pick(tabs[(i + d + tabs.length) % tabs.length]); };
  strip.querySelector(".fm-key.pv").addEventListener("click", () => step(-1));
  strip.querySelector(".fm-key.nx").addEventListener("click", () => step(1));
  strip.addEventListener("keydown", e => { if (e.key === "ArrowLeft" || e.key === "ArrowRight") { e.preventDefault(); step(e.key === "ArrowLeft" ? -1 : 1); } });
}

/* The header band (retained_create_server.header_band). */
const band = $("fmBand");
if (band) drawn(band, "fm-hart", (s, W, H) => {
  const defs = svgEl("defs", {}, s), c = 13.5, g = svgEl("g", { opacity: .6 }, s), span = { x1: 0, x2: W, y1: 0, y2: 0 };
  const stops = [[0, 1], [.5, 1], [.75, .5], [.95, .1], [1, 0]];
  svgEl("path", { d: `M${c} 0H${W}V${H}H0V${c}Z`, fill: grad(defs, stops.map(([o, a]) => [o, OLIVE, .49 * a]), span) }, g);
  svgEl("path", { d: `M.75 ${H}V${c}L${c} .75H${W}`, fill: "none", stroke: grad(defs, stops.map(([o, a]) => [o, OLIVE, a]), span), "stroke-width": 1.5 }, g);
});
})();
