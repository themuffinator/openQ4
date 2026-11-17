
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_S = "Oxanium, Chakra Petch, sans-serif";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
let uid = 0; const nid = p => p + (++uid);

/* A tiny timeline: tracks of [element, attribute, from, to, start ms, duration ms]. */
function play(tracks, done) {
  const t0 = performance.now(), end = Math.max(...tracks.map(t => t[4] + t[5]));
  const step = now => {
    const t = now - t0;
    tracks.forEach(([el, attr, a, b, s, d]) => { const p = d ? Math.min(1, Math.max(0, (t - s) / d)) : (t >= s ? 1 : 0);
      if (t >= s || p > 0) el.setAttribute(attr, typeof a === "number" ? a + (b - a) * p : (p < 1 ? a : b)); });
    if (t < end) requestAnimationFrame(step); else done?.();
  };
  requestAnimationFrame(step);
}
const wrap = (text, perLine) => { const words = text.split(" "), lines = []; let line = "";
  words.forEach(w => { if ((line + " " + w).trim().length > perLine) { lines.push(line.trim()); line = w; } else line += " " + w; });
  if (line.trim()) lines.push(line.trim()); return lines; };
/* The objective plate: an open plate with its cut in the foot strip, fading toward the trailing side. With an
   underlay it carries its own black 0.50 backing inside the same outline and fade, and nothing else darkens. */
function objPlate(parent, x, y, w, bodyH, footH, color, fade, { underlay = false, mirror = false } = {}) {
  const g = svgEl("g", mirror ? { transform: `matrix(-1 0 0 1 ${2 * x + w} 0)` } : {}, parent), id = nid("of");
  const cut = footH * .9, outline = `M${x} ${y}H${x + w}V${y + bodyH + footH}H${x + cut}L${x} ${y + bodyH + footH - cut}Z`;
  if (underlay) {
    const ug = nid("ou"), ul = svgEl("linearGradient", { id: ug, gradientUnits: "userSpaceOnUse", x1: x, x2: x + w }, svgEl("defs", {}, g));
    [[0, .5], [fade[0], .5], [fade[1], .25], [fade[3], 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, ul));
    svgEl("path", { d: outline, fill: `url(#${ug})`, class: "underlay" }, g);
  }
  const lg = svgEl("linearGradient", { id, gradientUnits: "userSpaceOnUse", x1: x, x2: x + w }, svgEl("defs", {}, g));
  [[0, .49], [fade[0], .49], [fade[1], .245], [fade[2], .05], [fade[3], 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": color, "stop-opacity": a }, lg));
  svgEl("path", { d: outline, fill: `url(#${id})` }, g);
  const rg = nid("or"); const rl = svgEl("linearGradient", { id: rg, gradientUnits: "userSpaceOnUse", x1: x, x2: x + w }, svgEl("defs", {}, g));
  [[0, 1], [fade[0], 1], [fade[1], .5], [fade[2], .1], [fade[3], 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": color, "stop-opacity": a }, rl));
  svgEl("path", { d: `M${x + .5} ${y}V${y + bodyH + footH - cut}L${x + cut} ${y + bodyH + footH - .6}H${x + w}`, fill: "none", stroke: `url(#${rg})`, "stroke-width": 1.2 }, g);
  return g;
}
function shot(parent, x, y, seed) {       // placeholder for the bitmap screenshot: a camera frame with corner brackets
  const g = svgEl("g", {}, parent), id = nid("sh");
  const lg = svgEl("linearGradient", { id, x1: 0, y1: 0, x2: 1, y2: 1 }, svgEl("defs", {}, g));
  svgEl("stop", { offset: 0, "stop-color": ["#3b4432", "#44392a", "#2e3a40"][seed % 3] }, lg); svgEl("stop", { offset: 1, "stop-color": "#11140e" }, lg);
  svgEl("rect", { x, y, width: 134, height: 100, fill: `url(#${id})` }, g);
  for (let k = 0; k < 20; k++) svgEl("rect", { x, y: y + k * 5, width: 134, height: .6, fill: "#fff", "fill-opacity": .05 }, g);
  svgEl("path", { d: "M6 14V6H14M120 6H128V14M128 86V94H120M14 94H6V86", transform: `translate(${x} ${y})`, fill: "none", stroke: "#DDE8C8", "stroke-opacity": .6, "stroke-width": .8 }, g);
  svgEl("rect", { x, y, width: 134, height: 100, fill: "none", stroke: "#000", "stroke-width": 2 }, g);
  return g;
}
const shadowed = (parent, x, y, t, o) => { const g = svgEl("g", {}, parent);
  svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000", "fill-opacity": .9 }, g).textContent = t; svgEl("text", { ...o, x, y }, g).textContent = t; return g; };

/* Objectives display. */
const OBJ = [
  ["Reach the hangar control room", "Strogg forces hold the hangar. Fight through the perimeter and restore power to the control room so the dropship can land."],
  ["Destroy the three Strogg communication relays before the convoy leaves the staging area", "Each relay feeds the Strogg network. Use explosives to bring them down; the convoy cannot move until all three are silent."],
  ["Rendezvous with Rhino Squad", "Meet Rhino Squad at the medical bay. Keep the squad medic alive and escort the wounded to the evacuation point. Strogg reinforcements are expected from the east tunnels, so hold the corridor until the lift arrives."],
];
const pda = document.getElementById("pdaSvg");
if (pda) {
  let remastered = true, strogg = false, none = false, toggleMode = false;
  const defs = svgEl("defs", {}, pda);
  const bg = svgEl("linearGradient", { id: "pdaBar", gradientUnits: "userSpaceOnUse", x1: 0, x2: 380 }, defs);
  [[0, .8], [.5, .8], [.75, .4], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, bg));
  const root = svgEl("g", {}, pda);
  const DONE = ["Regroup with Rhino Squad", "Reach the motor pool", "Find a way through the tunnels", "Disable the tetranode"];
  let parts;
  /* The no-objectives screenshot (gfx/objectives/none): static behind the prohibition sign, as a vector stand-in. */
  function noneShot(parent, x, y) {
    const g = svgEl("g", {}, parent);
    svgEl("rect", { x, y, width: 134, height: 100, fill: "#1b1d1a", stroke: "#000", "stroke-width": 1 }, g);
    for (let k = 0; k < 50; k++) svgEl("rect", { x: x + 1, y: y + 1 + k * 2, width: 132, height: .7, fill: "#fff", "fill-opacity": .03 + (k * 37 % 7) * .012 }, g);
    svgEl("circle", { cx: x + 67, cy: y + 50, r: 30, fill: "none", stroke: "#fff", "stroke-opacity": .2, "stroke-width": 9 }, g);
    svgEl("path", { d: `M${x + 46} ${y + 71}L${x + 88} ${y + 29}`, stroke: "#fff", "stroke-opacity": .2, "stroke-width": 9 }, g);
    return g;
  }
  function build() {
    root.innerHTML = "";
    const lay = hud.fit(pda); hud.regionsLayer(root, lay);
    const g = hud.place(root, "cl", lay);
    const col = strogg ? "#FF9000" : "#B0CD6B", mark = strogg ? "#FF9900" : "#B0CD6B";
    const head = { "font-family": strogg ? F_S : F_M, "font-weight": strogg ? 600 : 400, "font-size": 8.4, "letter-spacing": -.4, fill: "#FF8000" };
    const serial = strogg ? "STROGG NET 45B-43556-BGX7J54" : "SMC MILCOMM 45B-43556-BGX7J54";
    const bar = svgEl("rect", { x: -40, y: 10, width: 420, height: 460, fill: "url(#pdaBar)" }, g);
    const frames = svgEl("g", { opacity: .4 }, g), text = svgEl("g", {}, g);
    /* Remastered (remastered_hud_objectives.py display()): a column centred on the view: the heading (16 u + 2), the
       entries three at a time (at least 132 u each plus 11), the empty plate, COMPLETED and the serial (12 + 12 u).
       An entry: the title on up to two lines of 308 u, the screenshot, and the description on 178 u without a limit.
       Stock: the newest three on a fixed 143 u pitch from 35 u, descriptions cut at seven lines. */
    const ents = none ? [] : OBJ.map(([title, desc]) => {
      const tl = remastered ? wrap(title, 52).slice(0, 2) : [title.length > 52 ? title.slice(0, 52) : title];
      const dl = remastered ? wrap(desc, 35) : wrap(desc, 35).slice(0, 7);
      const h = remastered ? Math.max(132, 4 + tl.length * 12 + 2 + Math.max(100, dl.length * 13.75) + 14) : 132;
      return { tl, dl, h };
    });
    const colH = 18 + (none ? 143 : ents.reduce((s, e) => s + e.h + 11, 0)) + (remastered && !none ? 14 : 0) + 24;
    const top = remastered ? (480 - colH) / 2 : 17;
    svgEl("text", { x: 9, y: top + 10, ...head }, text).textContent = ">>MISSION OBJECTIVES";
    let y0 = top + 18;
    if (none) {
      objPlate(frames, 6, y0, 600, 112, 20, col, [.29, .40, .51, .56]);
      noneShot(text, 28, y0 + 17);
      ["No current mission", "objectives."].forEach((t, k) => svgEl("text", { x: 170, y: y0 + 54 + k * 16, "font-family": F_L, "font-weight": 500, "font-size": 14.9, fill: "#D0DEB6" }, text).textContent = t);
      y0 += 143;
    }
    ents.forEach(({ tl, dl, h }, i) => {
      objPlate(frames, 6, y0, 600, h - 20, 20, col, [.29, .40, .51, .56]);
      svgEl("path", { d: `M16 ${y0 + 5}h8v8.5z`, fill: mark }, frames);
      tl.forEach((t, k) => svgEl("text", { x: 26, y: y0 + 14 + k * 12, "font-family": F_L, "font-weight": 500, "font-size": 10, fill: "#fff" }, text).textContent = t);
      const oy = (tl.length - 1) * 12;
      shot(text, 28, y0 + 18 + oy, i);
      dl.forEach((t, k) => svgEl("text", { x: 170, y: y0 + 30 + oy + k * 13.75, "font-family": F_L, "font-weight": 500, "font-size": 10, fill: "#D0DEB6" }, text).textContent = t);
      y0 += remastered ? h + 11 : 143;
    });
    if (remastered) {
      if (!none) {      // COMPLETED: out of view the section is its collapsed header, pointing on; scrolled to, it lists them
        const t = svgEl("text", { x: 24, y: y0 + 11, "font-family": F_L, "font-weight": 500, "font-size": 8.7, fill: "#fff", "fill-opacity": .4 }, text);
        t.textContent = `COMPLETED  ${DONE.length}`;
        const tw = t.getComputedTextLength ? t.getComputedTextLength() : 60;
        svgEl("path", { d: `M${30 + tw} ${y0 + 5}h7l-3.5 5z`, fill: "#fff", "fill-opacity": .4 }, text);
        y0 += 14;
        // the scrollbar at the plates' trailing end: four items, three in view
        svgEl("rect", { x: 614, y: 35, width: 3, height: 420, fill: col, "fill-opacity": .15 }, text);
        svgEl("rect", { x: 613, y: 35, width: 5, height: 315, fill: col, "fill-opacity": .55 }, text);
      }
      svgEl("text", { x: 26, y: y0 + 12 + 9, "font-family": strogg ? F_S : F_M, "font-size": 4.7, "letter-spacing": -.3, fill: "#D9E7BF", "fill-opacity": .25 }, text).textContent = serial;
    } else svgEl("text", { x: 26, y: 465, "font-family": strogg ? F_S : F_M, "font-size": 4.7, "letter-spacing": -.3, fill: "#D9E7BF", "fill-opacity": .25 }, text).textContent = serial;
    // Toggle Objectives (g_objectivesToggle): the HUD prompt names the control that closes the display
    if (toggleMode && !hud.classic) A.hudPrompt?.(hud.place(root, "br", lay), { x: 629, y: 476, items: [{ kbm: "TAB", pad: "BACK", touch: "scores", label: "Close" }] });
    parts = { bar, frames, text };
  }
  build(); hud.on(build);
  const enter = () => {
    build();
    if (reduce) return;
    const t0 = performance.now();           // the back bar slides about 220 u in and fades to 0.80 over 150 ms
    play([[parts.bar, "opacity", 0, 1, 0, 150], [parts.frames, "opacity", 0, .5, 0, 150], [parts.frames, "opacity", .5, .4, 150, 100], [parts.text, "opacity", 0, 1, 150, 100]]);
    const slide = now => { const p = Math.min(1, Math.max(0, now - t0) / 150); parts.bar.setAttribute("transform", `translate(${-220 * (1 - p)} 0)`); if (p < 1) requestAnimationFrame(slide); };
    requestAnimationFrame(slide);
  };
  document.getElementById("pdaShow")?.addEventListener("click", enter);
  const tog = (id, fn) => { const b = document.getElementById(id); b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); fn(on); build(); }, true); };
  tog("pdaRem", on => { remastered = on; }); tog("pdaStrogg", on => { strogg = on; }); tog("pdaNone", on => { none = on; }); tog("pdaToggle", on => { toggleMode = on; });
}

/* Notices over gameplay. Classic keeps the stock back bars on the 4:3 canvas. Remastered anchors the new-objective
   notice to the leading screen and the completion notice, mirrored, to the trailing screen under the radio chatter;
   neither draws a back bar, and each plate carries its own backing. */
const nt = document.getElementById("noticeSvg");
if (nt) {
  const ctl = document.getElementById("noticeCtl"); if (ctl) hud.controls(ctl);
  let nw, cp, fl, fade, state = { nw: false, cp: false };
  function build() {
    nt.innerHTML = "";
    const lay = hud.fit(nt), classic = hud.classic;
    hud.regionsLayer(nt, lay);
    const defs = svgEl("defs", {}, nt);
    A.crosshairAt(nt);
    A.radioChatter(hud.place(nt, "tr", lay));
    const PW = classic ? 279 : 200, FADE = classic ? [.46, .59, .73, .75] : [.66, .8, .93, 1];

    nw = { g: hud.place(nt, "tl", lay) };
    if (classic) {
      const nb = svgEl("linearGradient", { id: "ntNewBar", gradientUnits: "userSpaceOnUse", x1: -616, x2: 441 }, defs);
      [[0, .8], [.6623, .8], [.6818, .72], [.7384, .4], [.7947, .08], [.8165, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, nb));
      nw.bar = svgEl("rect", { x: -616, y: 32, width: 1057, height: 175, fill: "url(#ntNewBar)" }, nw.g);
    }
    nw.fr = svgEl("g", { opacity: .4 }, nw.g); nw.body = svgEl("g", {}, nw.fr);
    objPlate(nw.body, 6, 52, PW, 127, 17, "#B0CD6B", FADE, { underlay: !classic });
    svgEl("path", { d: "M16 59h8v8.5z", fill: "#B0CD6B" }, nw.fr);
    nw.tx = svgEl("g", {}, nw.g);
    const hs = classic ? svgEl("text", { x: 9, y: 47, "font-family": F_M, "font-size": 8.4, "letter-spacing": -.4, fill: "#FF8000" }, nw.tx)
                       : shadowed(nw.tx, 9, 47, ">>NEW OBJECTIVE", { "font-family": F_M, "font-size": 8.4, "letter-spacing": -.4, fill: "#FF8000" });
    if (classic) hs.textContent = ">>NEW OBJECTIVE";
    wrap(OBJ[1][0], 27).slice(0, 2).forEach((t, k) => svgEl("text", { x: 26, y: 68 + k * 13.75, "font-family": F_L, "font-weight": 500, "font-size": 10, fill: "#fff" }, nw.tx).textContent = t);
    nw.shot = shot(nw.g, 28, 84, 1);
    svgEl("text", { x: 26, y: 204, "font-family": F_M, "font-size": 6.1, "letter-spacing": -.3, fill: "#D9E7BF", "fill-opacity": .25 }, nw.tx).textContent = "SMC MILCOMM 45B-43556-BGX7J54";

    cp = { g: hud.place(nt, "tr", lay) };
    if (classic) {
      const cb = svgEl("linearGradient", { id: "ntDoneBar", gradientUnits: "userSpaceOnUse", x1: 239, x2: 1061 }, defs);
      [[0, 0], [.184, 0], [.26, .4], [.337, .8], [1, .8]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, cb));
      cp.bar = svgEl("rect", { x: 239, y: 53, width: 822, height: 66, fill: "url(#ntDoneBar)" }, cp.g);
    }
    cp.fr = svgEl("g", { opacity: .4 }, cp.g);
    const px = classic ? 412 : 634 - PW;
    objPlate(cp.fr, px, 75, PW, 22, 17, "#B0CD6B", FADE, { underlay: !classic, mirror: !classic });
    svgEl("path", { d: classic ? "M420 83h8v8.5z" : "M624 83h-8v8.5z", fill: "#B0CD6B" }, cp.fr);
    cp.tx = svgEl("g", {}, cp.g);
    const hx = classic ? 413 : 631, ta = classic ? "start" : "end";
    const hd = { "font-family": F_M, "font-size": 8.4, "letter-spacing": -.4, fill: "#FF8000", "text-anchor": ta };
    if (classic) svgEl("text", { x: hx, y: 69, ...hd }, cp.tx).textContent = ">>OBJECTIVE COMPLETE"; else shadowed(cp.tx, hx, 69, ">>OBJECTIVE COMPLETE", hd);
    wrap(OBJ[0][0], 35).slice(0, 2).forEach((t, k) => svgEl("text", { x: classic ? 431 : 612, y: 92 + k * 13.75, "text-anchor": ta, "font-family": F_L, "font-weight": 500, "font-size": 10, fill: "#fff" }, cp.tx).textContent = t);

    fl = svgEl("g", { opacity: 0 }, nt);
    fade = svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: "#000", opacity: 0 }, nt);
    svgEl("text", { x: 320, y: 232, "text-anchor": "middle", "font-family": F_M, "font-size": 16.8, "letter-spacing": -1, fill: "#FF0000" }, fl).textContent = "OBJECTIVE FAILED:";
    svgEl("text", { x: 320, y: 272, "text-anchor": "middle", "font-family": F_M, "font-size": 16.8, "letter-spacing": -1, "font-variant": "small-caps", fill: "#fff" }, fl).textContent = "Protect the convoy";
    nt.appendChild(fl);
    nw.g.setAttribute("opacity", state.nw ? 1 : 0); cp.g.setAttribute("opacity", state.cp ? 1 : 0);
  }
  build(); hud.on(build);
  const timers = new Map();
  const closeLater = (key, fn) => { clearTimeout(timers.get(key)); timers.set(key, setTimeout(fn, 5000)); };
  const grow = (el, cy) => {                    // the frame grows from a strip over 150 ms
    const t0 = performance.now();
    const step = now => { const p = Math.min(1, Math.max(0, now - t0) / 150), s = .12 + .88 * p;
      el.setAttribute("transform", `translate(0 ${cy * (1 - s)}) scale(1 ${s})`); if (p < 1) requestAnimationFrame(step); else el.removeAttribute("transform"); };
    requestAnimationFrame(step);
  };
  const enter = (n, key, cy) => {
    state[key] = true; n.g.setAttribute("opacity", 1);
    if (reduce) return;
    const tr = [[n.fr, "opacity", 0, .8, 0, 150], [n.fr, "opacity", .8, .4, 150, 100], [n.tx, "opacity", 0, 1, 150, 100]];
    if (n.bar) tr.push([n.bar, "opacity", 0, 1, 0, 150]);
    if (n.shot) tr.push([n.shot, "opacity", 0, 1, 150, 100]);
    play(tr); grow(n.body || n.fr, cy);
    closeLater(key, () => play([[n.fr, "opacity", .8, 0, 0, 100], [n.tx, "opacity", 1, 0, 0, 50], ...(n.shot ? [[n.shot, "opacity", 1, 0, 0, 20]] : []), ...(n.bar ? [[n.bar, "opacity", 1, 0, 0, 100]] : [])],
      () => { state[key] = false; n.g.setAttribute("opacity", 0); n.fr.setAttribute("opacity", .4); n.tx.setAttribute("opacity", 1); n.shot?.setAttribute("opacity", 1); n.bar?.setAttribute("opacity", 1); }));
  };
  const failIn = () => {
    fl.setAttribute("opacity", 1); fade.setAttribute("opacity", 0);
    play([[fade, "opacity", 0, .85, 0, 3000]], () => setTimeout(() => { fl.setAttribute("opacity", 0); fade.setAttribute("opacity", 0); }, 1200));
  };
  document.getElementById("ntNew")?.addEventListener("click", () => enter(nw, "nw", 115));
  document.getElementById("ntDone")?.addEventListener("click", () => enter(cp, "cp", 86));
  document.getElementById("ntFail")?.addEventListener("click", failIn);
  state.nw = state.cp = true; nw.g.setAttribute("opacity", 1); cp.g.setAttribute("opacity", 1);   // both shown at rest for the still view
}
A.objPlate = objPlate; A.play = play;
})();
