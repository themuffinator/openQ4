
(() => {
const { svgEl } = window.__atlas;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const FONT_M = "Michroma, Eurostile, sans-serif", FONT_C = "Chakra Petch, Arial Narrow, sans-serif";

/* On-screen keyboard: types into the field above it. */
const osk = document.querySelector(".osk");
if (osk) {
  const field = document.querySelector(".oskfield .field"), caret = field.querySelector(".caret");
  const rows = ["1234567890", "QWERTYUIOP", "ASDFGHJKLÉ", "ZXCVBNM,.-"];
  rows.forEach(r => { const row = document.createElement("div"); row.className = "r";
    [...r].forEach(ch => { const k = document.createElement("button"); k.className = "k" + (ch === "7" ? " f" : ""); k.textContent = ch; k.dataset.ch = ch; row.appendChild(k); });
    osk.appendChild(row); });
  const last = document.createElement("div"); last.className = "r";
  [["◀", "w2", "left"], ["Space", "w5", " "], ["Del", "w2", "del"], ["Done", "w2", "done"]].forEach(([t, w, a]) => {
    const k = document.createElement("button"); k.className = "k " + w; k.innerHTML = `<small>${t}</small>`; k.dataset.act = a; last.appendChild(k); });
  osk.appendChild(last);
  osk.addEventListener("click", e => {
    const k = e.target.closest(".k"); if (!k) return;
    osk.querySelectorAll(".k.f").forEach(x => x.classList.remove("f")); k.classList.add("f");
    const text = field.firstChild;
    if (k.dataset.ch) text.textContent = (text.textContent + k.dataset.ch.toLowerCase()).slice(0, 18);
    else if (k.dataset.act === " ") text.textContent = (text.textContent + " ").slice(0, 18);
    else if (k.dataset.act === "del") text.textContent = text.textContent.slice(0, -1);
    field.insertBefore(text, caret);
  });
}

/* Touch overlay in dp: landscape at the 1280x720 reference; portrait at 720x1280, where the thumb deck takes the
   bottom band and the readouts rise to the center screen's bottom edge, above it (section 13.6). */
const RAIL = "#B3D06E";
const SYM = {
  fire: (x, y, s) => `M${x} ${y - s * .26}V${y - s * .1}M${x} ${y + s * .1}V${y + s * .26}M${x - s * .26} ${y}H${x - s * .1}M${x + s * .1} ${y}H${x + s * .26}M${x + s * .18} ${y}A${s * .18} ${s * .18} 0 1 1 ${x + s * .18} ${y - .01}`,
  jump: (x, y) => `M${x - 12} ${y + 6}L${x} ${y - 6}L${x + 12} ${y + 6}`,
  crouch: (x, y) => `M${x - 12} ${y - 6}L${x} ${y + 6}L${x + 12} ${y - 6}`,
  weapon: (x, y) => `M${x - 13} ${y - 5}H${x + 9}L${x + 13} ${y - 1}H${x - 13}M${x - 13} ${y + 5}H${x - 3}`,
  reload: (x, y) => `M${x + 9} ${y - 5}A10 10 0 1 0 ${x + 10} ${y + 3}M${x + 9} ${y - 12}V${y - 5}H${x + 2}`,
  zoom: (x, y) => `M${x - 3} ${y - 3}m-8 0a8 8 0 1 0 16 0a8 8 0 1 0 -16 0M${x + 3} ${y + 3}L${x + 11} ${y + 11}`,
  pause: (x, y) => `M${x - 5} ${y - 9}V${y + 9}M${x + 5} ${y - 9}V${y + 9}`,
  score: (x, y) => `M${x - 10} ${y - 7}H${x + 10}M${x - 10} ${y}H${x + 10}M${x - 10} ${y + 7}H${x + 10}`,
  chat: (x, y) => `M${x - 11} ${y - 8}H${x + 11}V${y + 5}H${x - 3}L${x - 9} ${y + 10}V${y + 5}H${x - 11}Z`,
};
/* Placements: Fire first (the editor selects it). Sizes and spacing are the same in both orientations. */
const TOUCH = {
  land: { w: 1280, h: 720, stick: [250, 520], knob: [282, 494], splits: [[640, 110, 640, 690]], cross: [640, 360],
    buttons: [[1120, 590, 88, "fire", true], [1120, 470, 64, "weapon"], [1225, 500, 64, "jump"], [1225, 650, 64, "crouch"], [1010, 540, 56, "zoom"], [1010, 640, 56, "reload"],
      [48, 48, 48, "pause"], [560, 48, 48, "score"], [720, 48, 48, "chat"]],
    readouts: [[104, 60, "100"], [186, 60, "57", "#B3D06E"], [1236, 60, "86", "", "end"]],
    labels: [["MOVE", 250, 660], ["LOOK", 860, 300], ["FIRE", 1120, 668], ["WEAPON", 1120, 424], ["JUMP", 1225, 455], ["CROUCH", 1225, 700], ["ZOOM", 1010, 497], ["RELOAD", 1010, 690], ["PAUSE", 48, 94]],
    bar: [430, 120] },
  // the deck is the bottom band, 910-1280 dp; the move stick floats in its leading half
  port: { w: 720, h: 1280, stick: [180, 1096], knob: [212, 1070], splits: [[24, 910, 696, 910], [360, 934, 360, 1256]], cross: [360, 640],
    buttons: [[576, 1132, 88, "fire", true], [576, 1004, 64, "weapon"], [668, 1060, 64, "jump"], [668, 1188, 64, "crouch"], [468, 1060, 56, "zoom"], [468, 1172, 56, "reload"],
      [48, 72, 48, "pause"], [600, 72, 48, "score"], [672, 72, 48, "chat"]],
    readouts: [[40, 890, "100"], [122, 890, "57", "#B3D06E"], [680, 890, "86", "", "end"]],
    labels: [["MOVE", 180, 1240], ["LOOK", 540, 800], ["FIRE", 576, 1206], ["WEAPON", 576, 958], ["JUMP", 668, 1014], ["CROUCH", 668, 1236], ["ZOOM", 468, 1018], ["RELOAD", 468, 1216], ["PAUSE", 48, 118]],
    bar: [150, 150] },
};
const drawTouch = (touch, P) => {
  const ed = svgEl("g", { display: "none" }, touch);
  const label = (x, y, t) => { const g = svgEl("g", {}, ed);
    const w = t.length * 7.4 + 14;
    svgEl("rect", { x: x - w / 2, y: y - 10, width: w, height: 20, fill: "rgba(0,0,0,.55)", stroke: "rgba(255,255,255,.45)", "stroke-dasharray": "3 3" }, g);
    svgEl("text", { x, y: y + 4, "text-anchor": "middle", "font-family": FONT_M, "font-size": 9.5, fill: "rgba(255,255,255,.7)" }, g).textContent = t; };
  const button = (cx, cy, size, sym, pressed) => {
    const c = Math.round(size * .28), x = cx - size / 2, y = cy - size / 2;
    const g = svgEl("g", { opacity: pressed ? .8 : .35 }, touch);
    svgEl("path", { d: `M${x} ${y}H${x + size}V${y + size}H${x + c}L${x} ${y + size - c}Z`, fill: "rgba(0,0,0,.55)", stroke: pressed ? "#E38900" : RAIL, "stroke-width": 1.5 }, g);
    svgEl("path", { d: sym(cx, cy, size), fill: "none", stroke: pressed ? "#E38900" : "#fff", "stroke-width": 2.2, "stroke-linecap": "square" }, g);
    if (pressed) svgEl("path", { d: `M${x + 8} ${y + 8}h9v9z`, fill: "#E38900" }, g);
  };
  // move stick: origin where the thumb lands; outer ring marks the run threshold
  const [sx, sy] = P.stick, st = svgEl("g", { opacity: .35 }, touch);
  svgEl("circle", { cx: sx, cy: sy, r: 92, fill: "rgba(0,0,0,.35)", stroke: RAIL, "stroke-width": 1.5 }, st);
  svgEl("circle", { cx: sx, cy: sy, r: 124, fill: "none", stroke: RAIL, "stroke-width": 1, "stroke-dasharray": "2 6" }, st);
  svgEl("circle", { cx: P.knob[0], cy: P.knob[1], r: 36, fill: "rgba(0,0,0,.5)", stroke: "#fff", "stroke-width": 1.5 }, st);
  P.splits.forEach(([x1, y1, x2, y2]) => svgEl("line", { x1, y1, x2, y2, stroke: "rgba(255,255,255,.25)", "stroke-dasharray": "4 8" }, touch));
  // crosshair stays clear
  const [cx, cy] = P.cross;
  svgEl("path", { d: `M${cx} ${cy - 14}V${cy - 4}M${cx} ${cy + 4}V${cy + 14}M${cx - 14} ${cy}H${cx - 4}M${cx + 4} ${cy}H${cx + 14}`, stroke: "#fff", "stroke-opacity": .85, "stroke-width": 2 }, touch);
  P.buttons.forEach(([x, y, size, sym, pressed]) => button(x, y, size, SYM[sym], pressed));
  // Touch HUD preset: readouts moved clear of the controls
  const rd = svgEl("g", { "font-family": FONT_C, "font-weight": 600, fill: "#DDEBC3" }, touch);
  P.readouts.forEach(([x, y, t, fill, anchor]) => svgEl("text", { x, y, "font-size": 30, ...(fill ? { fill } : {}), ...(anchor ? { "text-anchor": anchor } : {}) }, rd).textContent = t);
  P.labels.forEach(([t, x, y]) => label(x, y, t));
  // layout editor: 24 dp snapping grid, the selected control with handles, presets
  const grid = svgEl("path", { d: "", stroke: "rgba(255,255,255,.07)", "stroke-width": 1 }, ed);
  let gd = ""; for (let x = 24; x < P.w; x += 24) gd += `M${x} 0V${P.h}`; for (let y = 24; y < P.h; y += 24) gd += `M0 ${y}H${P.w}`;
  grid.setAttribute("d", gd); ed.insertBefore(grid, ed.firstChild);
  const [fx, fy] = P.buttons[0];
  svgEl("rect", { x: fx - 52, y: fy - 52, width: 104, height: 104, fill: "none", stroke: "#E38900", "stroke-width": 1.5, "stroke-dasharray": "6 4" }, ed);
  [[fx - 52, fy - 52], [fx + 52, fy - 52], [fx - 52, fy + 52], [fx + 52, fy + 52]].forEach(([x, y]) => svgEl("rect", { x: x - 5, y: y - 5, width: 10, height: 10, fill: "#E38900" }, ed));
  svgEl("text", { x: fx, y: fy - 60, "text-anchor": "middle", "font-family": FONT_M, "font-size": 10, fill: "#E38900" }, ed).textContent = "100%";
  const bar = svgEl("g", {}, ed), [bx, by] = P.bar;
  svgEl("path", { d: `M${bx} ${by}H${bx + 420}V${by + 36}H${bx + 12}L${bx} ${by + 24}Z`, fill: "rgba(0,0,0,.7)", stroke: "#8B964B", "stroke-width": 1.5 }, bar);
  [["DEFAULT", 40, true], ["COMPACT", 155, false], ["LEFT-HANDED", 260, false], ["RESET", 380, false]].forEach(([t, dx, on]) =>
    svgEl("text", { x: bx + dx, y: by + 23, "text-anchor": "middle", "font-family": FONT_M, "font-size": 10, fill: on ? "#E38900" : "#fff", "fill-opacity": on ? 1 : .8 }, bar).textContent = t);
  return ed;
};
const eds = [["svg.touchsvg", TOUCH.land], ["svg.touchsvgp", TOUCH.port]].map(([sel, P]) => { const s = document.querySelector(sel); return s && drawTouch(s, P); }).filter(Boolean);
if (eds.length) {
  const box = document.getElementById("editBox");
  box?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = box.classList.toggle("on"); box.setAttribute("aria-pressed", on); eds.forEach(ed => ed.setAttribute("display", on ? "inline" : "none")); }, true);
}

})();
