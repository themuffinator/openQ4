
(() => {
const A = window.__atlas, { svgEl } = A;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_P = "IBM Plex Mono, monospace";
const NOTE = "#FFFF8D", MAR_T = "#AAE355", STR_T = "#FF8E00";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
/* Measure text with a hidden SVG so plates fit their lines. */
let meter;
const measure = (text, size, family = F_L, weight = 500) => {
  if (!meter) { meter = svgEl("svg", { width: 0, height: 0, style: "position:absolute;visibility:hidden" }); document.body.appendChild(meter); }
  const t = svgEl("text", { "font-family": family, "font-size": size, "font-weight": weight }, meter); t.textContent = text;
  const w = t.getComputedTextLength(); t.remove(); return w;
};
const sheared = (parent, x, y, w, h, attrs) => svgEl("rect", { x, y, width: w, height: h, rx: 1, transform: `matrix(1 0 -0.22 1 ${0.22 * (y + h / 2)} 0)`, ...attrs }, parent);
const shadowText = (parent, x, y, t, o) => { const g = svgEl("g", {}, parent);
  svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000", "fill-opacity": .9 }, g).textContent = t; svgEl("text", { ...o, x, y }, g).textContent = t; return g; };
/* A tag in the header-band construction: a small plate with a cut lower-leading corner. */
const tag = (parent, x, y, text, color, dark = "#10140A") => {
  const w = measure(text, 7.2, F_L, 700) + 8;
  svgEl("path", { d: `M${x} ${y}H${x + w}V${y + 10}H${x + 3}L${x} ${y + 7}Z`, fill: color, "fill-opacity": .85 }, parent);
  svgEl("text", { x: x + w / 2, y: y + 7.8, "text-anchor": "middle", "font-family": F_L, "font-weight": 700, "font-size": 7.2, "letter-spacing": .4, fill: dark }, parent).textContent = text;
  return w;
};
const wrapPx = (text, size, width) => { const words = text.split(" "), out = []; let line = "";
  words.forEach(w => { const test = line ? line + " " + w : w; if (measure(test, size) > width && line) { out.push(line); line = w; } else line = test; }); if (line) out.push(line); return out; };

/* Remastered chat box (section 14.9): a leading-edge box 200-300 u wide that always clears the status bar vertically.
   Its bottom sits 8 u above the gauges; where the leading screen is too narrow for its width (below 16:9) it is 200 u
   wide and rises clear of the weapon strip too. Lines: channel tag, name, message; each on its own plate as wide as
   its text. The input opens at the box's bottom and pushes the lines up, never down toward the status bar. */
A.mpChat = (svg, lay, { lines, team, input, channel = "all", typed = "", limit = 128, classic = false, clear = 59, teamGame = true }) => {
  const g = svgEl("g", {}, svg);
  if (classic) {                                    // stock: four lines at 8,302 on a 15 u pitch, 549 u wide, text only
    lines.slice(-4).forEach((l, i) => shadowText(g, 8, 314 + i * 15, `${l.name}${l.team ? ` (${l.team === "m" ? "Marine" : "Strogg"})` : ""}: ${l.text}`,
      { "font-family": F_L, "font-weight": 500, "font-size": 12, fill: l.team ? (l.team === "m" ? MAR_T : STR_T) : NOTE }));
    if (input) {
      const pg = svgEl("linearGradient", { id: "chatPlateC", gradientUnits: "userSpaceOnUse", x1: 37, x2: 374 }, svgEl("defs", {}, g));
      [[0, .8], [.44, .8], [.62, .4], [.88, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, pg));
      svgEl("path", { d: "M37 380.5H374V400.2H48L37 389Z", fill: "url(#chatPlateC)" }, g);
      svgEl("path", { d: "M37.5 380.5V389L48 399.7H220", fill: "none", stroke: "#CCCC51", "stroke-opacity": .8, "stroke-width": 1 }, g);
      shadowText(g, 6, 394, "SEND", { "font-family": F_M, "font-size": 10.1, "letter-spacing": -1, fill: "#fff", "fill-opacity": .6 });
      svgEl("text", { x: 66, y: 395, "font-family": F_L, "font-weight": 500, "font-size": 12, fill: NOTE }, g).textContent = typed + "▏";
    }
    return g;
  }
  const s = lay.s, sbLeft = 320 - 219 * s;               // the status bar's leading edge in view units
  const room = (sbLeft - 8 * s - (lay.L + 8 * s)) / s;   // stock units beside the status bar
  const corner = room >= 180, W = corner ? Math.min(300, room) : 200;
  // the box always clears the status bar vertically: its bottom sits 8 u above the gauges' top edge (y 429 on the
  // canvas, 51 u above the bottom edge); a narrow leading screen also lifts it clear of the weapon strip
  const boxY = corner ? lay.B - clear * s : lay.B - 150 * s;       // narrow views, portrait included, follow the bottom edge
  const box = svgEl("g", { transform: `translate(${lay.L + 8 * s} ${boxY}) scale(${s})` }, g);
  // content is authored upward from y 0 = the bottom of the box
  const SIZE = 10.7, PITCH = 13.3;
  let rows = [];
  const shown = input ? lines.slice(-8) : lines.slice(-4);
  shown.forEach(l => {
    const col = l.server ? "#fff" : l.team ? (l.team === "m" ? MAR_T : STR_T) : NOTE;
    const nameCol = l.me ? "#fff" : l.team ? col : "#fff";
    const lead = (l.team ? 30 : 0) + (l.server ? 14 : 0);
    const first = `${l.name}: `, avail = W - 8 - lead;
    const parts = wrapPx(first + l.text, SIZE, avail);
    parts.forEach((p, k) => rows.push({ l, p, k, col, nameCol, lead, first }));
  });
  while (rows.length > 8) { const first = rows[0].l; rows = rows.filter(r => r.l !== first); }   // whole messages only
  const inputH = input ? 34 : 0;
  g.topY = boxY + (-inputH - rows.length * PITCH - (input ? 4 : 0) - 4) * s;   // the top of the chat, for rows stacked above it
  rows.forEach((r, i) => {
    const y = -inputH - (rows.length - i) * PITCH - (input ? 4 : 0);
    const line = svgEl("g", { opacity: r.l.alpha ?? 1 }, box);
    const w = measure(r.p, SIZE) + r.lead + 8;
    sheared(line, 0, y + 1.5, w, PITCH - 2, { fill: r.l.me ? "#8B964B" : "#000", "fill-opacity": r.l.me ? .3 : .35 });
    let x = 4;
    if (r.k === 0 && r.l.team) x += tag(line, x, y + 3, "TEAM", r.col) + 4;
    else if (r.l.team) x += 30;
    if (r.k === 0 && r.l.server) { svgEl("circle", { cx: x + 4.5, cy: y + 7.8, r: 4, fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1 }, line);
      svgEl("path", { d: `M${x + 4.5} ${y + 6.8}v3.4M${x + 4.5} ${y + 5.2}v.3`, stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.1 }, line); x += 12; }
    else if (r.l.server) x += 12;
    const t = svgEl("text", { x, y: y + 10.2, "font-family": F_L, "font-weight": 500, "font-size": SIZE, fill: r.col, "fill-opacity": r.l.server ? .7 : 1 }, line);
    if (r.k === 0 && !r.l.server) { const a = svgEl("tspan", { fill: r.nameCol }, t); a.textContent = r.first.trimEnd() + " "; const b = svgEl("tspan", {}, t); b.textContent = r.p.slice(r.first.length); }
    else t.textContent = r.p;
    if (r.l.me && r.k === 0) svgEl("path", { d: `M${-6} ${y + 3}h5v5z`, fill: "#E38900" }, line);
  });
  if (input) {                                       // the stock chat plate, recessed, with the channel tag in place of SEND
    const y = -18, pw = W, gid = "chatPlateR" + Math.random().toString(36).slice(2, 6);
    const pg = svgEl("linearGradient", { id: gid, gradientUnits: "userSpaceOnUse", x1: 0, x2: pw }, svgEl("defs", {}, box));
    [[0, .8], [.7, .8], [1, .55]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, pg));
    svgEl("path", { d: `M0 ${y}H${pw}V${y + 18}H9L0 ${y + 9}Z`, fill: `url(#${gid})` }, box);
    svgEl("path", { d: `M.5 ${y}V${y + 9}L9 ${y + 17.5}H${pw * .7}`, fill: "none", stroke: "#CCCC51", "stroke-opacity": .8, "stroke-width": 1 }, box);
    const col = channel === "team" ? (team === "s" ? STR_T : MAR_T) : NOTE;
    const tw = tag(box, 6, y + 4, channel === "team" ? "TEAM" : "ALL", col);
    // the entry scrolls sideways so the caret stays in view; the leading text is clipped under a fade
    const ex = 12 + tw, avail = pw - ex - (typed.length >= limit * .8 ? 22 : 8), tw2 = measure(typed, SIZE), shift = Math.min(0, avail - tw2 - 3);
    const cid = "chatEntry" + Math.random().toString(36).slice(2, 6);
    svgEl("rect", { x: ex, y: y, width: avail, height: 18 }, svgEl("clipPath", { id: cid }, box));
    const eg = svgEl("g", { "clip-path": `url(#${cid})` }, box), inner = svgEl("g", { transform: `translate(${shift} 0)` }, eg);
    const tx = svgEl("text", { x: ex, y: y + 12.6, "font-family": F_L, "font-weight": 500, "font-size": SIZE, fill: NOTE }, inner);
    tx.textContent = typed;
    if (shift < 0) svgEl("path", { d: `M${ex + 1} ${y + 9}l3 -3v6z`, fill: NOTE, "fill-opacity": .7 }, box);   // more text before the view
    const caret = svgEl("rect", { x: ex + tw2 + 1, y: y + 4, width: 1.2, height: 10, fill: NOTE }, inner);
    if (!reduce) svgEl("animate", { attributeName: "opacity", values: "1;1;0;0", keyTimes: "0;.5;.5;1", dur: "1s", repeatCount: "indefinite" }, caret);
    if (typed.length >= limit * .8) svgEl("text", { x: pw - 5, y: y + 12.4, "text-anchor": "end", "font-family": F_P, "font-size": 7.5, fill: typed.length >= limit ? "#E46D56" : "#fff", "fill-opacity": .6 }, box).textContent = `${limit - typed.length}`;
    // prompts with the active device's glyphs
    let px = 0; const py = y - 6;
    [["Enter", "Send"], ...(teamGame ? [["Tab", "Channel"]] : []), ["Esc", "Cancel"]].forEach(([k, label]) => {
      const kw = measure(k, 6.6, F_P) + 6;
      svgEl("path", { d: `M${px} ${py - 7}H${px + kw}V${py + 2}H${px + 2.5}L${px} ${py - .5}Z`, fill: "#fff", "fill-opacity": .78 }, box);
      svgEl("text", { x: px + kw / 2, y: py, "text-anchor": "middle", "font-family": F_P, "font-size": 6.6, fill: "#10140A" }, box).textContent = k;
      const lw = measure(label, 7.6);
      shadowText(box, px + kw + 3, py + .5, label, { "font-family": F_L, "font-weight": 500, "font-size": 7.6, fill: "#fff", "fill-opacity": .8 });
      px += kw + lw + 12;
    });
  }
  return g;
};
/* Voice indicator (section 14.9). Stock: a bar fading from the leading edge (aud_wavbg, black 0.42 to 0.07 with a hard
   slanted end at 87%), a 10x12 additive #FFFF8D trace (the transmission waveform, canned height) and the name; you at
   4,263 ("Transmitting..."), one talker at 4,286. Remastered: one plate per talker, stacked above the chat on the
   leading side, the trace following the voice level in the talker's team color, you marked with the microphone. */
A.mpVoice = (svg, lay, { talkers, classic, top }) => {
  const g = svgEl("g", {}, svg);
  if (classic) {
    talkers.filter(t => t.self).slice(0, 1).concat(talkers.filter(t => !t.self).slice(0, 1)).forEach(t => {
      const y = t.self ? 263 : 286, gid = "audBg" + Math.random().toString(36).slice(2, 6);
      const lg = svgEl("linearGradient", { id: gid, gradientUnits: "userSpaceOnUse", x1: 4, x2: 180 }, svgEl("defs", {}, g));
      [[0, .42], [.87, .07]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#000", "stop-opacity": a }, lg));
      svgEl("path", { d: `M4 ${y + 1.1}H162L158 ${y + 16.9}H4Z`, fill: `url(#${gid})` }, g);
      A.waveform?.(g, { rect: [8, y + 3, 10, 12], clip: `M8 ${y + 3}h10v12h-10Z`, color: NOTE, mode: "table" });
      shadowText(g, 21, y + 13, t.self ? "Transmitting..." : t.name, { "font-family": F_L, "font-weight": 500, "font-size": 9.6, fill: "#fff" });
    });
    return g;
  }
  const s = lay.s, x = lay.L + 8 * s;
  talkers.forEach((t, i) => {
    const y = top - (talkers.length - i) * 17 * s, row = svgEl("g", { transform: `translate(${x} ${y}) scale(${s})` }, g);
    const col = t.team === "m" ? MAR_T : t.team === "s" ? STR_T : NOTE, label = t.self ? "YOU" : t.name;
    const w = 44 + measure(label, 9.6) + 10;
    sheared(row, 0, 0, w, 15, { fill: t.self ? "#8B964B" : "#000", "fill-opacity": t.self ? .3 : .5 });
    A.waveform?.(row, { rect: [-2, 1.5, 48, 12], clip: "M4 1.5H30V13.5H4Z", color: col, mode: "voice", source: t.self ? "voice:self" : "voice:" + t.name });
    if (t.self) {        // microphone: you are transmitting
      svgEl("path", { d: "M36 3.2a2 2 0 0 1 4 0v4a2 2 0 0 1-4 0zM34.6 6.6a3.4 3.4 0 0 0 6.8 0M38 10v2.4", fill: "none", stroke: "#fff", "stroke-width": 1.1, "stroke-linecap": "round" }, row);
    } else svgEl("path", { d: "M34.5 5.5h2l3-2.6v9.2l-3-2.6h-2z", fill: col }, row);
    svgEl("text", { x: 45, y: 11, "font-family": F_L, "font-weight": 500, "font-size": 9.6, fill: t.self ? "#fff" : col }, row).textContent = label;
  });
  return g;
};
A.measureText = measure; A.hudTag = tag; A.shadowText = shadowText; A.shearedPlate = sheared;
})();
