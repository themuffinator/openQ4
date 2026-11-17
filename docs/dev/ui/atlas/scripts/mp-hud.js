
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const svg = document.getElementById("mpSvg");
if (!svg) return;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const MAR = "#6AA42B", STR = "#FF7B04", MAR_T = "#AAE355", STR_T = "#FF8E00", NOTE = "#FFFF8D", VALUE = "#FFBE23", ERR = "#E46D56", OK = "#B5C784";
const WEAP = { gauntlet: "#00D9FF", machinegun: "#FFFF00", shotgun: "#FF8000", hyperblaster: "#0073FF", grenadelauncher: "#338F12", nailgun: "#99CCCC", rocketlauncher: "#FF3300", railgun: "#00FF00", lightninggun: "#FFFFBA", darkmattergun: "#C400FF", napalmgun: "#FFBF40" };
const ABBR = { gauntlet: "GA", machinegun: "MG", shotgun: "SG", hyperblaster: "HB", grenadelauncher: "GL", nailgun: "NG", rocketlauncher: "RL", railgun: "RG", lightninggun: "LG", darkmattergun: "DM", napalmgun: "NP" };
const S = { mode: "dm", comp: false, specMode: "off", warm: false, count: false, last: false, sd: false, spec: false, dead: false, vote: false, voice: false, chat: false, teamChan: false, ctx: false, flagTaken: false, tick: 0 };
const LP = (size, weight = 500) => ({ "font-family": F_L, "font-weight": weight, "font-size": size });
const measure = (t, size, fam = F_L, w = 500) => A.measureText(t, size, fam, w);
const ell = (t, size, max, fam, w) => { if (measure(t, size, fam, w) <= max) return t; let s = t; while (s.length > 1 && measure(s + "…", size, fam, w) > max) s = s.slice(0, -1); return s + "…"; };
const shadowText = (parent, x, y, t, o) => A.shadowText(parent, x, y, t, o);
const plate = (parent, x, y, w, h, a = .5, col = "#000") => A.shearedPlate(parent, x, y, w, h, { fill: col, "fill-opacity": a });
const marker = (parent, x, y) => svgEl("path", { d: `M${x} ${y}h5v5z`, fill: "#E38900" }, parent);
const markPath = t => t === "m" ? "M12 1L22 7V17L12 23L2 17V7ZM12 6L7 16H17Z" : "M2 6L10 10L12 2L14 10L22 6L16 14L18 22L12 17L6 22L8 14Z";
const mark = (parent, x, y, team, s = 16) => { const g = svgEl("g", { transform: `translate(${x} ${y}) scale(${s / 24})` }, parent); svgEl("path", { d: markPath(team), fill: "#fff", "fill-rule": "evenodd" }, g); return g; };
const stopwatch = (parent, x, y, s = 1, col = NOTE) => { const g = svgEl("g", { transform: `translate(${x} ${y}) scale(${s})` }, parent);
  svgEl("circle", { cx: 7, cy: 8, r: 5.6, fill: "none", stroke: col, "stroke-width": 1.3 }, g);
  svgEl("path", { d: "M7 8l2.6 2.4M5 1h4M7 1v1.8M11.6 3.2l1.4-1.4", stroke: col, "stroke-width": 1.2, fill: "none", "stroke-linecap": "round" }, g); return g; };
const flagIcon = (parent, x, y, color, state, s = 1) => {        // state: base | taken | dropped
  const g = svgEl("g", { transform: `translate(${x} ${y}) scale(${s})` }, parent);
  const f = svgEl("g", { transform: "matrix(1 0 -0.22 1 1.8 0)" }, g);
  const art = svgEl("path", { d: "M3 1V17M3 2C7 0 10 4 15 2V10C10 12 7 8 3 10", fill: state === "dropped" ? "none" : color, "fill-opacity": state === "base" ? .45 : .9, stroke: color, "stroke-width": 1.3, "stroke-dasharray": state === "dropped" ? "2 1.5" : "none" }, f);
  if (state === "taken") { art.classList.add("flag-taken"); shadowText(g, 9.5, 9.5, "!", { ...LP(8.5, 700), fill: "#fff", "text-anchor": "middle" }); }
  if (state === "dropped") shadowText(g, 9.5, 9.5, "?", { ...LP(8.5, 700), fill: "#fff", "text-anchor": "middle" });
  return g;
};
const weaponIcon = (parent, key, x, y, h) => {           // vector weapon icon in the weapon color code (stand-in until the set loads)
  if (A.ICONS[key]) return A.icon(parent, key, { x, y, h, color: WEAP[key] || "#fff", opacity: .95 });
  const w = h * 1.5; svgEl("rect", { x, y, width: w, height: h, rx: 1, fill: WEAP[key] || "#fff", "fill-opacity": .9 }, parent);
  svgEl("text", { x: x + w / 2, y: y + h * .72, "text-anchor": "middle", "font-family": F_M, "font-size": h * .55, fill: "#10140a" }, parent).textContent = ABBR[key] || "?"; return null;
};

/* ---------- Classic: the stock construction on the 4:3 canvas (section 14.9 table) ---------- */
function slab(parent, x, y, w, h, stops, cutTop, cutBottom, color = "#000") {
  const id = "sl" + Math.random().toString(36).slice(2, 8), cid = id + "c";
  const d = svgEl("defs", {}, parent), lg = svgEl("linearGradient", { id, gradientUnits: "userSpaceOnUse", x1: x, x2: x + w }, d);
  stops.forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": color, "stop-opacity": a }, lg));
  svgEl("path", { d: `M${x} ${y}H${x + w * cutTop}L${x + w * cutBottom} ${y + h}H${x}Z` }, svgEl("clipPath", { id: cid }, d));
  return svgEl("rect", { x, y, width: w, height: h, fill: `url(#${id})`, "clip-path": `url(#${cid})` }, parent);
}
function classicTop(g) {
  const team = ["tdm", "ctf", "dz", "ca"].includes(S.mode);
  if (S.mode === "dm") {
    [["1.", "Sarge_v", 24, 35], ["3.", "You", 17, 64]].forEach(([rank, name, score, y]) => {
      slab(g, 0, y, 242, 28, [[0, 1], [.12, 1], [.25, .84], [.44, .5], [.62, .22], [.7, .14]], .73, .70);
      slab(g, 0, y + 4.4, 242, 17.5, [[0, .2], [.16, .2], [.66, .052], [.7, .04]], .70, .68, NOTE);
      shadowText(g, 23, y + 18, rank, { ...LP(10.6), fill: "#fff", "text-anchor": "end" });
      shadowText(g, 26, y + 18, name, { ...LP(10.6), fill: "#fff" });
      shadowText(g, 163, y + 18, String(score), { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
    });
  }
  if (team) {
    [[MAR, "m", S.mode === "dz" ? 47 : 3, -3], [STR, "s", S.mode === "dz" ? 38 : 2, 41]].forEach(([col, t, score, y], i) => {
      slab(g, 1, y, 102, 58, [[0, .87], [.34, .5], [.81, .15], [.88, .12]], .88, .79);
      slab(g, 1, y + 11.8, 101, 34.4, [[0, .46], [.76, .12], [.85, .1]], .85, .77, col);
      mark(g, 6, y + 17, t, 24);
      const hot = S.mode === "dz" && S.tick > 0 && i === 0;
      svgEl("text", { x: 36, y: y + 37, ...LP(18), fill: hot ? `rgb(${255 * (1 - S.tick)},255,${255 * (1 - S.tick)})` : "#fff" }, g).textContent = score;
    });
    if (S.mode === "ctf") { classicFlag(g, 86, 11, MAR, false); classicFlag(g, 86, 55, STR, S.flagTaken); }
  }
  if (S.mode === "tourney") {
    svgEl("rect", { x: 0, y: 0, width: 640, height: 33, fill: "#000", "fill-opacity": .6 }, g);
    TOURNEY.forEach(([a, sa, b, sb], i) => { const x = i * 80;
      if (i === 1) svgEl("rect", { x: x + 1, y: 1, width: 77, height: 14, fill: "#5EB987", "fill-opacity": .4 }, g);
      if (i === 3) svgEl("rect", { x: x + 1, y: 1, width: 77, height: 28, fill: "#3E57B7", "fill-opacity": .4 }, g);
      [[a, sa, 11], [b, sb, 24]].forEach(([n, s, yy]) => { shadowText(g, x + 2, yy, n, { ...LP(10.6), fill: "#fff", "fill-opacity": i === 3 && n === "Ash" ? .5 : 1 });
        svgEl("text", { x: x + 77, y: yy, ...LP(10.6), fill: "#FF8000", "text-anchor": "end" }, g).textContent = s; });
      if (i) svgEl("path", { d: `M${x - 1.5} 2h3M${x} 2v28M${x - 1.5} 30h3`, stroke: "#fff", "stroke-opacity": .5 }, g); });
    svgEl("path", { d: "M0 35H434L416 54H0Z", fill: "#000", "fill-opacity": .6 }, g);
    shadowText(g, 4, 48, "Arena 2; Quarter Finals", { ...LP(10.6), fill: "#fff" });
  }
  const ty = S.mode === "dm" ? 92 : S.mode === "tourney" ? 57 : 94;
  svgEl("circle", { cx: 14, cy: ty + 11, r: 7.5, fill: "none", stroke: NOTE, "stroke-width": 1.6 }, g);
  svgEl("path", { d: `M14 ${ty + 11}l3.5 3.5M11 ${ty + 1.5}h6`, stroke: NOTE, "stroke-width": 1.4, fill: "none" }, g);
  shadowText(g, 23, ty + 16, S.warm || S.count ? (S.mode === "tourney" ? "Pre-game 7" : "Warmup 7") : (S.sd ? "∞" : S.last ? "0:42" : "7:12"), { ...LP(12), fill: "#fff" });
}
function classicFlag(g, x, y, color, taken) {
  const f = svgEl("g", { transform: `translate(${x} ${y})` }, g), s = svgEl("g", { transform: "matrix(1 0 -0.22 1 3.08 0)" }, f);
  const art = svgEl("path", { d: "M22 2V27M22 3C16 1 12 6 5 3V15C12 18 16 13 22 15", fill: color, "fill-opacity": .8, stroke: color, "stroke-opacity": .8, "stroke-width": 1.4 }, s);
  if (taken) { art.classList.add("flag-taken"); shadowText(f, 12, 17, "!", { ...LP(16, 700), fill: "#fff", "text-anchor": "middle" }); }
}

/* ---------- Remastered top-leading group: standing or team panels, then the timer ---------- */
const TOURNEY = [["Kaz", 4, "Orin", 2], ["You", 3, "Tal", 1], ["Vex", 0, "Nyx", 0], ["Rho", 5, "Ash", 3], ["Quillon_the_Relentless", 1, "Moss", 1], ["Reed", 2, "Juno", 0], ["Pike", 0, "- BYE -", 0], ["Brand", 3, "Cole", 4]];
function standing(g) {
  const tied = false, rows = [["1ST", "Sarge_v", 24, false, "/30"], ["3RD", "You", 19, true]];
  rows.forEach(([rank, name, score, me, limit], i) => {
    const y = 36 + i * 21;
    plate(g, 6, y, 150, 18);
    if (me) { svgEl("rect", { x: 8.5, y: y + 2, width: 145, height: 14, rx: .8, fill: NOTE, "fill-opacity": .16, transform: `matrix(1 0 -0.22 1 ${0.22 * (y + 9)} 0)` }, g);
      [y + 2, y + 15].forEach(ry => svgEl("rect", { x: 8.5, y: ry, width: 145, height: 1, fill: NOTE, "fill-opacity": .32, transform: `matrix(1 0 -0.22 1 ${0.22 * (y + 9)} 0)` }, g)); marker(g, 0, y + 6); }
    svgEl("text", { x: 34, y: y + 12.6, "text-anchor": "end", ...LP(8.4, 700), "letter-spacing": .3, fill: i === 0 ? VALUE : "#fff" }, g).textContent = rank;
    svgEl("text", { x: 38, y: y + 12.8, ...LP(10.7), fill: me ? "#fff" : "#E4EBD2" }, g).textContent = ell(name, 10.7, 76);
    svgEl("text", { x: limit ? 137 : 150, y: y + 13.4, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 13, fill: me ? "#fff" : NOTE }, g).textContent = score;
    if (limit) svgEl("text", { x: 139, y: y + 13, "font-family": F_C, "font-weight": 600, "font-size": 8, fill: "#fff", "fill-opacity": .45 }, g).textContent = limit;
    if (me) { const d = score - 24; svgEl("text", { x: 162, y: y + 13, "font-family": F_C, "font-weight": 600, "font-size": 10, fill: d >= 0 ? OK : ERR }, g).textContent = d >= 0 ? `+${d}` : `−${-d}`; }
  });
  return 36 + rows.length * 21;
}
function teamPanels(g) {
  const dz = S.mode === "dz", ctf = S.mode === "ctf", ca = S.mode === "ca";
  const T = [["m", "MARINES", MAR, MAR_T, dz ? 47 : ca ? 5 : ctf ? 3 : 41, true], ["s", "STROGG", STR, STR_T, dz ? 38 : ca ? 4 : ctf ? 2 : 37, false]];
  const limit = dz ? "/120" : ca ? "/10" : ctf ? "/5" : "/60";
  T.forEach(([t, name, col, textCol, score, me], i) => {
    const y = 36 + i * 25;
    plate(g, 6, y, 150, 22);
    A.shearedPlate(g, 6, y, 26, 22, { fill: col, "fill-opacity": .8 });
    mark(g, 11, y + 3, t, 16);
    if (me) marker(g, 0, y + 8);
    svgEl("text", { x: 38, y: y + 9.4, ...LP(7, 700), "letter-spacing": .6, fill: textCol }, g).textContent = name;
    if (dz && S.tick > 0 && i === 0) svgEl("text", { x: 38, y: y + 19.6, ...LP(7, 600), fill: "#fff", "fill-opacity": .55 }, g).textContent = "+1 · HOLDING";
    const hot = dz && i === 0 && S.tick > 0;
    const sc = svgEl("text", { x: 118, y: y + 16.8, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 17, fill: hot ? `rgb(${Math.round(255 - (255 - 0xAA) * S.tick)},${Math.round(255 - (255 - 0xE3) * S.tick)},${Math.round(255 - (255 - 0x55) * S.tick)})` : "#fff" }, g); sc.textContent = score;
    svgEl("text", { x: 120, y: y + 16.4, "font-family": F_C, "font-weight": 600, "font-size": 8, fill: "#fff", "fill-opacity": .45 }, g).textContent = limit;
    if (ctf) flagIcon(g, 140, y + 2.5, col, i === 1 && S.flagTaken ? "taken" : "base", 1);
    if (ctf && i === 1 && S.flagTaken) { svgEl("path", { d: `M162 ${y + 6}h4v10h-4z`, fill: "none" }, g); shadowText(g, 162, y + 14, "Vex", { ...LP(9, 600), fill: MAR_T }); }
    if (ca) { for (let k = 0; k < 5; k++) svgEl("rect", { x: 162 + k * 6, y: y + 8, width: 4, height: 7, rx: .6, fill: textCol, "fill-opacity": k < (i ? 2 : 3) ? .9 : .2, transform: `matrix(1 0 -0.22 1 ${0.22 * (y + 11.5)} 0)` }, g); }
  });
  let y = 36 + 2 * 25;
  if (dz) { plate(g, 6, y, 106, 14, .5); svgEl("text", { x: 12, y: y + 10, ...LP(7.4, 700), "letter-spacing": .6, fill: "#fff", "fill-opacity": .7 }, g).textContent = "ZONE";
    const cz = S.tick > 0 ? ["MARINES", MAR] : ["CONTESTED", VALUE];
    svgEl("rect", { x: 38, y: y + 3, width: 7, height: 8, fill: cz[1] }, g); svgEl("text", { x: 49, y: y + 10, ...LP(7.4, 700), "letter-spacing": .5, fill: cz[1] }, g).textContent = cz[0]; y += 17; }
  return y;
}
function timerPlate(g, y) {
  const round = S.mode === "ca" || S.mode === "tourney";
  let label = "", time = "7:12", col = "#fff", pulse = false, sym = "watch";
  if (S.warm) { label = "WARM-UP"; time = "READY 3/8"; sym = "watch"; }
  else if (S.count) { label = "STARTS IN"; time = "0:07"; col = VALUE; }
  else if (S.sd) { label = "SUDDEN DEATH"; time = "∞"; col = ERR; }
  else if (S.last) { time = "0:08"; col = ERR; pulse = true; }
  const w = S.warm ? 112 : label ? 106 : 70;
  plate(g, 6, y, w, 16);
  stopwatch(g, 10, y + 1.5, .95, NOTE);
  let x = 27;
  if (label) { svgEl("text", { x, y: y + 11.2, ...LP(7.2, 700), "letter-spacing": .5, fill: "#fff", "fill-opacity": .7 }, g).textContent = label; x += measure(label, 7.2, F_L, 700) + 4 + label.length * .5; }
  const t = svgEl("text", { x, y: y + 12.4, "font-family": F_C, "font-weight": 600, "font-size": S.warm ? 9 : 12.5, fill: col }, g); t.textContent = time; if (pulse) t.classList.add("last-ten");
  y += 18;
  if (round && !S.warm && !S.count) {
    plate(g, 6, y, 106, 14);
    svgEl("text", { x: 12, y: y + 10, ...LP(7.4, 700), "letter-spacing": .5, fill: "#fff", "fill-opacity": .7 }, g).textContent = S.mode === "tourney" ? "ROUND 2 · ARENA 2" : "ROUND 3";
    if (S.mode === "ca") svgEl("text", { x: 104, y: y + 10.6, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 10, fill: VALUE }, g).textContent = "1:12";
    y += 16;
  }
  return y;
}
/* Tourney: the bracket strip across the center screen, restyled; your row aqua with the marker, finished arenas checked. */
function tourneyStrip(g) {
  const cellW = 78;
  TOURNEY.forEach(([a, sa, b, sb], i) => {
    const x = 2 + i * 80, done = i === 3;
    plate(g, x, 2, cellW, 30, .55);
    [[a, sa, 13, i === 1 && a === "You"], [b, sb, 27, false]].forEach(([n, s, yy, me]) => {
      if (me) { svgEl("rect", { x: x + 1.5, y: yy - 11, width: cellW - 3, height: 13, fill: "#5EB987", "fill-opacity": .4, transform: `matrix(1 0 -0.22 1 ${0.22 * (yy - 4.5)} 0)` }, g); marker(g, x - 5, yy - 8); }
      const lose = done && n === "Ash", win = done && n === "Rho";
      svgEl("text", { x: x + 4, y: yy, ...LP(9.3), fill: "#fff", "fill-opacity": lose ? .5 : 1 }, g).textContent = ell(n, 9.3, cellW - 22);
      svgEl("text", { x: x + cellW - 3, y: yy, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 10, fill: done ? "#fff" : "#FF8000", "fill-opacity": lose ? .5 : 1 }, g).textContent = s;
      if (win) svgEl("path", { d: `M${x + cellW - 20} ${yy - 5}l2.5 2.5 5-5`, fill: "none", stroke: OK, "stroke-width": 1.4 }, g);
    });
  });
}
function tourneyBar(g) {
  svgEl("path", { d: "M0 35H434L416 54H0Z", fill: "#000", "fill-opacity": .6 }, g);
  shadowText(g, 8, 48, "Arena 2 · Quarter-finals · You vs. Tal", { ...LP(10.6), fill: "#fff" });
  return 57;
}

/* ---------- kill feed, top-trailing ---------- */
let feed = [["Vex", "railgun", "Kaz"], ["Orin", "rocketlauncher", "Tal"], ["You", "nailgun", "Quillon_the_Relentless"], ["Kaz", "skull", "Kaz"]];
let feedT = performance.now(), feedLayer = null, lay = null;
function drawFeed() {
  const g = feedLayer; if (!g) return; g.innerHTML = "";
  const team = ["tdm", "ctf", "dz", "ca"].includes(S.mode), n = S.mode === "tourney" ? 2 : 4, classic = hud.classic;
  const side = name => ["Vex", "Tal", "Nyx", "Quillon_the_Relentless"].includes(name) ? "s" : "m";
  const col = name => !team ? NOTE : side(name) === "s" ? STR_T : MAR_T;
  feed.slice(0, n).forEach(([a, w, v], i) => {
    const y = (S.mode === "tourney" ? (classic ? 54 : 70) : 24) + i * 15, line = svgEl("g", { "data-line": i }, g);
    const me = a === "You" || v === "You", tk = team && w !== "skull" && a !== v && side(a) === side(v);
    const vName = classic ? v : ell(v, 12, 84), aName = classic ? a : ell(a, 12, 84);
    const vw = measure(vName, 12), aw = measure(aName, 12), iw = w === "skull" ? 12 : 17;
    const tagW = tk && !classic ? 30 : 0;
    const total = (w === "skull" && a === v ? 0 : aw + 5) + iw + 5 + vw + tagW;
    if (!classic) plate(line, 633 - total - 6, y - 11.5, total + 10, 15, me ? .3 : .35, me ? "#8B964B" : "#000");
    if (!classic && me) marker(line, 633 - total - 13, y - 8);
    const tv = shadowText(line, 633, y, vName, { ...LP(12), fill: me && !classic && v === "You" ? "#fff" : col(v), "text-anchor": "end" });
    let x = 633 - vw - 5 - tagW;
    if (tagW) A.hudTag(line, 633 - vw - tagW + 1, y - 9, "TEAM", col(v));
    if (w === "skull") { svgEl("path", { d: `M${x - 6} ${y - 10}a5.5 5.5 0 0 1 5.5 5.5c0 1.8-.9 2.8-1.8 3.6v1.9h-7.4v-1.9c-.9-.8-1.8-1.8-1.8-3.6a5.5 5.5 0 0 1 5.5-5.5zM${x - 8.2} ${y - 5}h1.6M${x - 5.4} ${y - 5}h1.6`, fill: "#fff", stroke: "#10140a", "stroke-width": .7 }, line); x -= iw; }
    else { weaponIcon(line, w, x - iw, y - 10, 11); x -= iw; }
    if (!(w === "skull" && a === v)) shadowText(line, x - 5, y, aName, { ...LP(12), fill: me && !classic && a === "You" ? "#fff" : col(a), "text-anchor": "end" });
  });
}
setInterval(() => {                                   // one line leaves every 3.1 s after holding 2000 ms and fading 1000 ms
  const t = performance.now() - feedT, top = feedLayer?.querySelector('[data-line="0"]');
  if (!feed.length) { feedT = performance.now(); return; }
  if (top && t > 2000 && !(hud.classic && S.mode === "tourney")) top.setAttribute("opacity", Math.max(0, 1 - (t - 2000) / 1000));
  if (t >= 3100) { feed.shift(); feedT = performance.now(); drawFeed(); }
}, 50);

/* ---------- center column: frag notice, main notice, spectator lines, awards; stacked, never overlapping ---------- */
function centerColumn(g) {
  const items = [];
  if (S.notice) items.push(["frag", "You fragged Vex", 17.3]);
  if (S.notice && S.mode === "dm") items.push(["main", "3rd place with 19", 15.4]);
  if (S.sd) items.push(["main", "Sudden Death!", 15.4]);
  else if (S.warm && !S.spec && S.mode !== "tourney" && !S.notice) items.push(["main", "Warmup", 15.4]);
  if (S.warm && S.mode !== "tourney") items.push(["spec1", "WAITING ON ONE MORE PLAYER", 12.5]);     // spectators get the follow card instead
  if (S.award) items.push(["award"]);
  let y = 81;
  items.forEach(([kind, text, size]) => {
    if (kind === "award") { [304, 336].forEach(x => { svgEl("circle", { cx: x, cy: y + 16, r: 14, fill: "#C9A227", stroke: "#6d5510", "stroke-width": 2 }, g);
      svgEl("path", { d: `M${x} ${y + 6}l3 7h7l-5.5 4 2 7-6.5-4-6.5 4 2-7-5.5-4h7z`, fill: "#fff3c4" }, g); }); y += 36; return; }
    const w = measure(text, size) + 16, h = size * 1.35;
    plate(g, 320 - w / 2, y, w, h, .35);
    const t = shadowText(g, 320, y + size * 1.02, "", { ...LP(size), fill: kind === "spec0" || kind === "spec1" ? "#FFFF8C" : NOTE, "text-anchor": "middle" });
    t.querySelectorAll("text").forEach((el, k) => { if (text.startsWith("3rd")) { const a = svgEl("tspan", { fill: k ? VALUE : "#000" }, el); a.textContent = "3rd"; svgEl("tspan", {}, el).textContent = text.slice(3); } else el.textContent = text; });
    if (kind.startsWith("spec")) t.classList.add("breathe");
    y += h + 3;
  });
}

/* ---------- powerups and the carried flag, bottom-trailing ---------- */
function powerups(g) {
  const items = [["quaddamage", "#00B4FF", 22], ...(S.flagTaken && S.mode === "ctf" ? [["flag", STR, null]] : [])];
  items.forEach(([key, col, secs], i) => {
    const cx = 612, cy = 400 - i * 34;
    svgEl("circle", { cx, cy, r: 13, fill: "#000", "fill-opacity": .5 }, g);
    if (secs) { const frac = secs / 30, a = 2 * Math.PI * frac; svgEl("path", { d: `M${cx} ${cy - 13}A13 13 0 ${frac > .5 ? 1 : 0} 1 ${cx + 13 * Math.sin(a)} ${cy - 13 * Math.cos(a)}`, fill: "none", stroke: col, "stroke-width": 2 }, g);
      svgEl("text", { x: cx - 18, y: cy + 4, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 11, fill: "#fff" }, g).textContent = secs; }
    if (key === "flag") flagIcon(g, cx - 8, cy - 9, col, "base", 1);
    else if (A.ICONS[key]) A.icon(g, key, { x: cx, y: cy - 10, h: 20, anchor: "middle", color: col, glow: true });
    else { svgEl("path", { d: `M${cx} ${cy - 8}l7 4v8l-7 4-7-4v-8z`, fill: col, "fill-opacity": .85 }, g); }
  });
}

/* ---------- Competitive (section 14.9): the match bar at the top center, team status, network quality ---------- */
/* name, weapon, health, armor, you, dead */
const ROSTER = {
  m: [["You", "nailgun", 100, 50, true, false], ["Orin", "railgun", 84, 100, false, false], ["Kaz", "rocketlauncher", 32, 0, false, false],
    ["Bolt", "shotgun", 0, 0, false, true], ["Pike", "machinegun", 125, 25, false, false]],
  s: [["Vex", "railgun", 76, 100, false, false], ["Nyx", "lightninggun", 100, 45, false, false], ["Tal", "rocketlauncher", 18, 0, false, false],
    ["Juno", "hyperblaster", 0, 0, false, true], ["Ash", "nailgun", 140, 80, false, false]],
};
const FOLLOWED = "Vex";
const isTeamMode = () => ["tdm", "ctf", "dz", "ca"].includes(S.mode);
const glyphChip = (g, x, y, key) => {                   // a key or button glyph in the prompt-bar construction
  const w = Math.max(14, key.length * 4.6 + 6);
  svgEl("path", { d: `M${x} ${y}h${w}v10h${-w + 3}l-3-3z`, fill: "#fff", "fill-opacity": .85 }, g);
  svgEl("text", { x: x + w / 2, y: y + 7.3, "text-anchor": "middle", "font-family": F_P, "font-size": 6, fill: "#10140a" }, g).textContent = key;
  return w;
};
const prompts = (g, items, cx, y) => {                  // glyph chips with labels, centered on cx
  const widths = items.map(([k, l]) => Math.max(14, k.length * 4.6 + 6) + 4 + measure(l, 8.4, F_L, 600));
  let x = cx - (widths.reduce((a, b) => a + b, 0) + 14 * (items.length - 1)) / 2;
  items.forEach(([k, l], i) => { const w = glyphChip(g, x, y, k);
    svgEl("text", { x: x + w + 4, y: y + 8.2, ...LP(8.4, 600), fill: "#fff", "fill-opacity": .85 }, g).textContent = l; x += widths[i] + 14; });
};
function netQuality(g, cx, y) {                          // ping beside four rising bars, colored by the worse of ping and loss
  const ping = 38, bars = 4;
  svgEl("text", { x: cx - 2, y: y + 7, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 8, fill: "#fff", "fill-opacity": .75 }, g).textContent = `${ping} ms`;
  for (let k = 0; k < 4; k++) svgEl("rect", { x: cx + 2 + k * 3.4, y: y + 7 - (2 + k * 1.6), width: 2.2, height: 2 + k * 1.6, fill: k < bars ? OK : "#fff", "fill-opacity": k < bars ? .9 : .2 }, g);
}
function matchBar(g) {
  const team = isTeamMode(), ctf = S.mode === "ctf", dz = S.mode === "dz", ca = S.mode === "ca", tour = S.mode === "tourney", spec = S.specMode !== "off";
  let L, R, sub = "";
  if (team) {
    const sc = dz ? [47, 38] : ca ? [5, 4] : ctf ? [3, 2] : [41, 37];
    L = { name: "MARINES", score: sc[0], col: MAR, text: MAR_T, t: "m" }; R = { name: "STROGG", score: sc[1], col: STR, text: STR_T, t: "s" };
    sub = dz ? "TO 120" : ca ? "ROUND 3" : ctf ? "TO 5" : "TO 60";
  } else if (tour) { L = { name: spec ? FOLLOWED : "You", score: 2, rank: "R2" }; R = { name: "Tal", score: 1, rank: "R2" }; sub = "ARENA 2"; }
  else { L = { name: spec ? FOLLOWED : "You", score: spec ? 21 : 19, rank: spec ? "2ND" : "3RD" }; R = { name: "Sarge_v", score: 24, rank: "1ST" }; sub = "TO 30"; }
  let time = "7:12", tcol = "#fff";
  if (S.warm) { time = "WARM-UP"; sub = "READY 3/8"; } else if (S.count) { time = "0:07"; tcol = VALUE; } else if (S.sd) { time = "SUDDEN"; sub = "DEATH"; tcol = ERR; } else if (S.last) { time = "0:08"; tcol = ERR; }
  const y = 6, H = 26, BW = 122, TW = 60, x0 = 320 - TW / 2 - 2 - BW, x1 = 320 + TW / 2 + 2;
  [[L, x0, 1], [R, x1, -1]].forEach(([side, x, dir]) => {
    plate(g, x, y, BW, H, .55);
    const ex = dir > 0 ? x : x + BW - 25;                // the outer end carries the team color and mark, or the rank
    A.shearedPlate(g, ex, y, 25, H, { fill: side.col || "#8B964B", "fill-opacity": side.col ? .85 : .55 });
    if (side.t) mark(g, ex + 4.5, y + 5, side.t, 16);
    else svgEl("text", { x: ex + 12.5, y: y + 16.5, "text-anchor": "middle", ...LP(7.6, 700), "letter-spacing": .3, fill: side.rank === "1ST" ? VALUE : "#fff" }, g).textContent = side.rank;
    const me = !spec && !team && side.name === "You";
    svgEl("text", { x: dir > 0 ? x + 31 : x + BW - 31, y: y + 16.8, "text-anchor": dir > 0 ? "start" : "end", ...LP(side.t ? 7.4 : 10.6, 700), "letter-spacing": side.t ? .7 : .1,
      fill: side.text || (me ? "#fff" : "#E4EBD2") }, g).textContent = side.t ? side.name : ell(side.name, 10.6, 52);
    if (me) marker(g, dir > 0 ? x - 6 : x + BW + 1, y + 10);
    svgEl("text", { x: dir > 0 ? x + BW - 7 : x + 7, y: y + 20.6, "text-anchor": dir > 0 ? "end" : "start", "font-family": F_C, "font-weight": 600, "font-size": 21, fill: "#fff" }, g).textContent = side.score;
  });
  plate(g, 320 - TW / 2, y, TW, H, .72);
  const tt = svgEl("text", { x: 320, y: y + (sub ? 14.6 : 17.6), "text-anchor": "middle", "font-family": F_C, "font-weight": 600, "font-size": S.warm || S.sd ? 9.6 : 15.5, fill: tcol }, g);
  tt.textContent = time; if (S.last) tt.classList.add("last-ten");
  if (sub) svgEl("text", { x: 320, y: y + 22.6, "text-anchor": "middle", ...LP(5.9, 700), "letter-spacing": .55, fill: "#fff", "fill-opacity": .55 }, g).textContent = sub;
  // under the bar: each side's state at its inner end, the zone under the timer, the network readout trailing
  const yb = y + H + 2;
  if (ctf) [[x0 + BW - 64, MAR, "base", ""], [x1 + 6, STR, S.flagTaken ? "taken" : "base", S.flagTaken ? "Vex" : ""]].forEach(([x, col, st, who]) => {
    plate(g, x, yb, 58, 13, .5); flagIcon(g, x + 3, yb - 1.5, col, st, .72);
    svgEl("text", { x: x + 17, y: yb + 9.4, ...LP(6.8, 700), "letter-spacing": .4, fill: "#fff", "fill-opacity": .8 }, g).textContent = st === "taken" ? `TAKEN · ${who}` : "AT BASE"; });
  if (ca) [[x0 + BW - 46, MAR_T, 3], [x1 + 6, STR_T, 2]].forEach(([x, col, alive]) => { plate(g, x, yb, 40, 11, .5);
    for (let k = 0; k < 5; k++) svgEl("rect", { x: x + 5 + k * 6.6, y: yb + 2.5, width: 4.4, height: 6, rx: .6, fill: col, "fill-opacity": k < alive ? .9 : .2, transform: `matrix(1 0 -0.22 1 ${0.22 * (yb + 5.5)} 0)` }, g); });
  if (dz) { plate(g, 320 - 34, yb, 68, 12, .5); svgEl("rect", { x: 320 - 28, y: yb + 3, width: 7, height: 6, fill: S.tick > 0 ? MAR : VALUE }, g);
    svgEl("text", { x: 320 - 17, y: yb + 9, ...LP(6.6, 700), "letter-spacing": .5, fill: S.tick > 0 ? MAR_T : VALUE }, g).textContent = S.tick > 0 ? "MARINES" : "CONTESTED"; }
  netQuality(g, x1 + BW + 40, y + 4);
  return yb + 14;
}
/* Team status: one plate per player, weapon, name, health and armor; the dead dimmed under a skull. */
function teamList(g, t, x, y, trailing) {
  const col = t === "m" ? MAR_T : STR_T, W = 138, follow = S.specMode === "follow";
  svgEl("text", { x: trailing ? x + W : x, y: y - 3, "text-anchor": trailing ? "end" : "start", ...LP(6.8, 700), "letter-spacing": .7, fill: col }, g).textContent = t === "m" ? "MARINES" : "STROGG";
  ROSTER[t].forEach(([name0, weap, hp, ar, me0, dead], i) => {
    const name = S.spec && me0 ? "Rho" : name0, me = me0 && !S.spec;            // a spectator is on neither list
    const ry = y + i * 16.5, lit = follow && name === FOLLOWED;
    plate(g, x, ry, W, 14.5, me ? .3 : .45, me ? "#8B964B" : "#000");
    if (lit) { svgEl("rect", { x: x + 1.5, y: ry + 1.5, width: W - 3, height: 11.5, fill: col, "fill-opacity": .22, transform: `matrix(1 0 -0.22 1 ${0.22 * (ry + 7.25)} 0)` }, g); marker(g, trailing ? x + W + 1 : x - 6, ry + 4.5); }
    if (me) marker(g, x - 6, ry + 4.5);
    const iconX = trailing ? x + W - 21 : x + 4;
    if (dead) svgEl("text", { x: iconX + 8, y: ry + 10.6, "text-anchor": "middle", ...LP(8, 700), fill: "#fff", "fill-opacity": .4 }, g).textContent = "✕";
    else weaponIcon(g, weap, iconX, ry + 2.5, 9.5);
    const nx = trailing ? x + W - 26 : x + 26;
    svgEl("text", { x: nx, y: ry + 10.6, "text-anchor": trailing ? "end" : "start", ...LP(9, 600), fill: lit || me ? "#fff" : col, "fill-opacity": dead ? .4 : 1 }, g).textContent = ell(name, 9, 46);
    if (!dead) {
      const hx = trailing ? x + 34 : x + W - 34, ax = trailing ? x + 10 : x + W - 10;
      svgEl("text", { x: hx, y: ry + 11, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 10.6, fill: hp <= 25 ? ERR : hp > 100 ? VALUE : "#fff" }, g).textContent = hp;
      svgEl("text", { x: ax, y: ry + 11, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 9, fill: "#fff", "fill-opacity": ar ? .6 : .25 }, g).textContent = ar;
    }
  });
}
/* ---------- Spectator follow cam (section 14.9): a follow card above the status bar and its prompts ---------- */
function followCard(g) {
  const free = S.specMode === "free", w = 236, x = 320 - w / 2, y = free ? 404 : 360;
  plate(g, x, y, w, 30, .58);
  if (!free && isTeamMode()) A.shearedPlate(g, x, y, 7, 30, { fill: STR, "fill-opacity": .9 });   // the followed player's team color
  svgEl("text", { x: x + 15, y: y + 10.2, ...LP(6.4, 700), "letter-spacing": .9, fill: "#fff", "fill-opacity": .58 }, g).textContent = free ? "SPECTATING" : "FOLLOWING";
  svgEl("text", { x: x + 15, y: y + 24.5, ...LP(13, 700), fill: "#fff" }, g).textContent = free ? "Free camera" : FOLLOWED;
  if (!free) {
    const rx = x + w - 10;
    svgEl("text", { x: rx, y: y + 24, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 15, fill: "#fff" }, g).textContent = "21";
    svgEl("text", { x: rx - 22, y: y + 23.4, "text-anchor": "end", ...LP(8.4, 700), "letter-spacing": .3, fill: VALUE }, g).textContent = "2ND";
    [[x - 9, -1], [x + w + 9, 1]].forEach(([cx, d]) => svgEl("path", { d: `M${cx - 3 * d} ${y + 9}l${6 * d} 6l${-6 * d} 6`, fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.6, "stroke-linejoin": "round" }, g));
  }
  prompts(g, free ? [["M1", "Follow a player"], ["TAB", "Scores"]] : [["M1", "Next"], ["M2", "Previous"], ["SPACE", "Free camera"]], 320, y + 35);
}
/* Free camera: name tags over the players in view, the team color at the leading end; Competitive adds health. */
function nameTags(g) {
  [[214, 236, "Nyx", "s", 100], [452, 214, "Orin", "m", 84], [338, 196, "Ash", "s", 140]].forEach(([cx, cy, n, t, hp]) => {
    const w = measure(n, 9, F_L, 600) + 18, x = cx - w / 2;
    plate(g, x, cy, w, 13, .5); A.shearedPlate(g, x, cy, 4, 13, { fill: t === "m" ? MAR : STR, "fill-opacity": .9 });
    svgEl("text", { x: x + 10, y: cy + 9.6, ...LP(9, 600), fill: "#fff" }, g).textContent = n;
    if (S.comp) { svgEl("rect", { x: x + 4, y: cy + 15, width: w - 8, height: 2, fill: "#fff", "fill-opacity": .2 }, g);
      svgEl("rect", { x: x + 4, y: cy + 15, width: (w - 8) * Math.min(1, hp / 100), height: 2, fill: hp > 100 ? VALUE : "#fff", "fill-opacity": .85 }, g); }
    svgEl("path", { d: `M${cx - 3} ${cy + 13}l3 4 3-4z`, fill: "#000", "fill-opacity": .5 }, g);
  });
}

/* ---------- Objective points of interest (section 14.9, Appendix C 1.28; ObjectiveMarkers.cpp) ----------
   CTF bases (a pennant: solid at home, hollow with an upward cue when carried, lower with a downward cue when
   dropped) and Dead Zone volumes (open corner brackets around an empty, filled or split core, a padlock while the
   artifact is required), each in a 45-degree diamond at black 0.48 with a 0.65 outline in the owner's color, the
   straight-line distance below. Off screen, the marker stops inside the safe frame (38 u from the side edges, 80 u
   from the top and bottom, times the HUD scale) at 0.78 with an outward chevron. Drawn in view units by the game for
   Classic and Remastered alike; hidden while dead, zoomed, spectating free or on the scoreboard. */
const POI_COL = { m: "#AAE355", s: "#FF8F00", n: "#DEEBC4" };
function poiMarker(g, x, y, s, { kind, team, flag = "base", owner = "n", locked = false, dist, dir = null }) {
  const col = POI_COL[kind === "base" ? team : owner === "split" ? "n" : owner], a = dir ? .78 : 1;
  const m = svgEl("g", { opacity: a }, g), r = 13 * s;
  svgEl("path", { d: `M${x} ${y - r}L${x + r} ${y}L${x} ${y + r}L${x - r} ${y}Z`, fill: "#000", "fill-opacity": .48, stroke: col, "stroke-opacity": .65, "stroke-width": .8 * s }, m);
  const line = (pts, w = 1.2) => svgEl("path", { d: "M" + pts.map(([px, py]) => `${(x + px * s).toFixed(2)} ${(y + py * s).toFixed(2)}`).join("L"), fill: "none", stroke: col, "stroke-width": w * s, "stroke-linecap": "butt" }, m);
  if (kind === "base") {
    const top = flag === "dropped" ? -3 : -6;
    line([[-4, 6], [-4, -6]]);
    const pen = `M${x - 3 * s} ${y + top * s}H${x + 6 * s}L${x + 3 * s} ${y + (top + 4) * s}H${x - 3 * s}Z`;
    svgEl("path", flag === "base" ? { d: pen, fill: col } : { d: pen, fill: "none", stroke: col, "stroke-width": .9 * s }, m);
    line([[-6, 6], [6, 6]]);
    if (flag !== "base") { const tip = flag === "dropped" ? 5 : 1; line([[1, 3], [3, tip], [5, 3]], 1); }
  } else {
    [-1, 1].forEach(sx => [-1, 1].forEach(sy => line([[sx * 5, sy * 2.5], [sx * 5, sy * 5], [sx * 2.5, sy * 5]])));
    if (owner === "split") { svgEl("path", { d: `M${x - 4 * s} ${y}L${x - s} ${y - 3 * s}V${y + 3 * s}Z`, fill: POI_COL.m }, m); svgEl("path", { d: `M${x + 4 * s} ${y}L${x + s} ${y + 3 * s}V${y - 3 * s}Z`, fill: POI_COL.s }, m); }
    else if (owner !== "n") svgEl("path", { d: `M${x} ${y - 3 * s}L${x + 3 * s} ${y}L${x} ${y + 3 * s}L${x - 3 * s} ${y}Z`, fill: col }, m);
    if (locked) { const lx = x + 8 * s, ly = y + 8 * s;
      svgEl("rect", { x: lx - 4 * s, y: ly - 4 * s, width: 8 * s, height: 8 * s, fill: "#000", "fill-opacity": .72 }, m);
      svgEl("path", { d: `M${lx - 2.5 * s} ${ly}h${5 * s}v${3 * s}h${-5 * s}ZM${lx - 1.5 * s} ${ly}v${-2.5 * s}h${3 * s}v${2.5 * s}`, fill: "none", stroke: "#DEEBC4", "stroke-width": .9 * s }, m); }
  }
  if (dir) { const [dx, dy] = dir, nx = -dy, ny = dx, tip = [x + dx * 21 * s, y + dy * 21 * s], tail = [x + dx * 16 * s, y + dy * 16 * s];
    svgEl("path", { d: `M${tail[0] + nx * 3 * s} ${tail[1] + ny * 3 * s}L${tip[0]} ${tip[1]}L${tail[0] - nx * 3 * s} ${tail[1] - ny * 3 * s}`, fill: "none", stroke: col, "stroke-width": 1.4 * s }, m); }
  if (dist != null) shadowText(m, x, y + 27 * s, `${dist} m`, { ...LP(8.6 * s), fill: "#E6EDD9", "fill-opacity": .85, "text-anchor": "middle" });
}
function objectiveMarkers(g, lay) {
  if (!["ctf", "dz"].includes(S.mode) || S.dead || S.specMode === "free") return;
  const s = hud.scale, right = lay.x0 + lay.W - 38 * s;      // the safe frame's trailing side
  if (S.mode === "ctf") {
    poiMarker(g, 452, 262, s, { kind: "base", team: "m", dist: 23 });
    poiMarker(g, right, 300, s, { kind: "base", team: "s", flag: S.flagTaken ? "carried" : "base", dist: 129, dir: [1, 0] });
  } else {
    poiMarker(g, 214, 250, s, { kind: "zone", owner: S.tick > 0 ? "m" : "split", locked: S.tick <= 0, dist: 41 });
  }
}

/* ---------- build ---------- */
function build() {
  svg.innerHTML = "";
  lay = hud.fit(svg);
  const classic = hud.classic, team = ["tdm", "ctf", "dz", "ca"].includes(S.mode);
  hud.regionsLayer(svg, lay);
  if (!S.dead && S.specMode !== "free") A.crosshairAt(svg);
  objectiveMarkers(svgEl("g", {}, svg), lay);
  // status bar, centered: a following spectator sees the followed player's; free-flying spectators none; Competitive
  // enlarges the readouts by 15% about the bottom edge
  if (S.specMode !== "free") {
    const sb = hud.place(svg, "bc", lay, S.comp && !classic ? " translate(320 480) scale(1.15) translate(-320 -480)" : ""), gx = classic ? [13, 190, 326] : [101, 278, 414];
    const ammo = A.marineGauge(sb, gx[0], "ammo", { mp: true, color: "#99CCCC" }), hp = A.marineGauge(sb, gx[1], "health"), ar = A.marineGauge(sb, gx[2], "armor");
    ammo.weapon({ key: "nailgun", clip: 50, loaded: 36, total: 86, max: 200 }); ar.set(57, .57);
    if (S.dead && S.specMode === "off") {            // dead: health reads 0 on the low-health paint (the armor gauge leaves at 0)
      hp.set(0, 0); ar.root.remove();
      [hp.fill, hp.trackFill, hp.bevel].forEach(el => el.setAttribute(el === hp.bevel ? "stroke" : "fill", "#FF4C00"));
      hp.trackFill.setAttribute("fill-opacity", .35); hp.ekg?.setColor("#4C0D00"); hp.rim.setAttribute("opacity", 1);
    } else hp.set(100, .8);
  }
  // top-leading group
  const tl = hud.place(svg, "tl", lay);
  if (classic) classicTop(tl);
  else if (S.comp) {                                 // the match bar at the top center; team status at the leading edge
    matchBar(hud.place(svg, "tc", lay));
    if (team) { teamList(hud.place(svg, "cl", lay), "m", 8, 150, false); if (S.spec) teamList(hud.place(svg, "cr", lay), "s", 494, 150, true); }
  }
  else {
    let y = 57;
    if (S.mode === "tourney") { tourneyStrip(hud.place(svg, "tc", lay)); y = tourneyBar(tl); }
    else if (team) y = teamPanels(tl) + 2;
    else y = standing(tl) + 2;
    timerPlate(tl, y + 2);
  }
  if (classic && S.mode === "tourney") {/* drawn in classicTop */}
  // kill feed (not for free spectators in the stock; Remastered shows it to everyone)
  feedLayer = svgEl("g", {}, hud.place(svg, "tr", lay)); if (!(classic && S.spec)) drawFeed();
  if (S.ctx && !classic) {                           // managed-match context: a card under the kill feed, collapsed while live
    const g = hud.place(svg, "tr", lay), y = S.mode === "tourney" ? 106 : 90;
    svgEl("path", { d: `M470 ${y}H634V${y + 34}H478L470 ${y + 26}Z`, fill: "#000", "fill-opacity": .72, stroke: "#E38900", "stroke-opacity": .55, "stroke-width": .8 }, g);
    svgEl("text", { x: 478, y: y + 12, ...LP(8.4, 700), "letter-spacing": .4, fill: "#FFF0C2" }, g).textContent = "MAP 2 OF 3 · LIVE";
    svgEl("text", { x: 478, y: y + 24, ...LP(7.4), fill: "#C8D6B0" }, g).textContent = "Series 1–0 · Timeouts 1 left";
    svgEl("text", { x: 478, y: y + 31, ...LP(6), fill: "#fff", "fill-opacity": .4 }, g).textContent = "Scoreboard shows the full match record";
  }
  // center column
  if (!classic) centerColumn(hud.place(svg, "tc", lay));
  else if (S.notice) { const c = svgEl("g", {}, svg); shadowText(c, 320, 104, "You fragged Vex", { ...LP(17.3), fill: NOTE, "text-anchor": "middle" }); }
  if (classic && S.spec) {                           // the stock spectator lines, breathing (mphud Spectate0/1)
    const c = svgEl("g", { class: "breathe" }, svg), free = S.specMode === "free";
    shadowText(c, 320, 139, free ? "SPECTATING" : "SPECTATING - FOLLOWING Vex", { ...LP(12.7), fill: "#FFFF8C", "text-anchor": "middle", "letter-spacing": -.3 });
    shadowText(c, 320, 156, free ? "Press Fire to enter follow mode" : "Press Fire to cycle players, Jump to exit follow mode", { ...LP(10.4), fill: "#FFFF8C", "text-anchor": "middle", "letter-spacing": -.3 });
  }
  if (S.spec && !classic) { followCard(hud.place(svg, "bc", lay)); if (S.specMode === "free") nameTags(svgEl("g", {}, svg)); }
  // chat and voice, leading side
  const chatLines = CHAT.map(c => ({ ...c }));
  const chatG = A.mpChat(svg, lay, { lines: chatLines, team: "m", input: S.chat, channel: S.teamChan ? "team" : "all", typed: S.chat ? TYPED : "", classic, teamGame: team,
    clear: (S.comp && !classic ? 51 * 1.15 : 51) + 8 });   // 8 u above the gauges' top, which Competitive's 15% lifts
  if (S.voice) A.mpVoice?.(svg, lay, { classic, top: chatG.topY, talkers: [{ name: "Kaz", team: ["tdm", "ctf", "dz", "ca"].includes(S.mode) ? "m" : null }, { self: true }] });
  // powerups, bottom-trailing
  if (S.specMode !== "free") powerups(hud.place(svg, "br", lay));
  // vote, leading edge; Competitive moves it to the top-leading corner the standing left, clear of the team status
  if (S.vote) {
    const g = S.comp && !classic ? svgEl("g", { transform: "translate(0 -114)" }, hud.place(svg, "tl", lay)) : hud.place(svg, "cl", lay);
    if (classic) { shadowText(g, 4, 157, "Vote Called:", { ...LP(9.6), fill: NOTE }); shadowText(g, 4, 171, "F1 (YES)  F2 (NO)", { ...LP(9.6), fill: NOTE }); ["MAP q4dm7", "FRAG LIMIT 30"].forEach((t, i) => shadowText(g, 4, 185 + i * 14, t, { ...LP(9.6), fill: "#fff" })); }
    else {
      plate(g, 6, 150, 150, 74, .5);
      svgEl("text", { x: 12, y: 162, ...LP(7.4, 700), "letter-spacing": .6, fill: VALUE }, g).textContent = "VOTE · Kaz";
      ["Map: q4dm7", "Frag limit: 30"].forEach((t, i) => svgEl("text", { x: 12, y: 176 + i * 12, ...LP(9.6), fill: "#fff" }, g).textContent = t);
      [["F1", "YES", OK, 5, 12], ["F2", "NO", ERR, 2, 84]].forEach(([k, lab, c, n, x]) => {
        svgEl("path", { d: `M${x} 199h14v10h-11l-3-3z`, fill: "#fff", "fill-opacity": .8 }, g);
        svgEl("text", { x: x + 7, y: 206.5, "text-anchor": "middle", "font-family": F_P, "font-size": 6, fill: "#10140a" }, g).textContent = k;
        svgEl("text", { x: x + 18, y: 207, ...LP(8, 700), fill: c }, g).textContent = lab;
        for (let q = 0; q < 8; q++) svgEl("rect", { x: x + 18 + q * 6, y: 211, width: 4, height: 4, fill: c, "fill-opacity": q < n ? .9 : .18 }, g);
      });
      for (let k = 0; k < 20; k++) svgEl("rect", { x: 12 + k * 7, y: 219 - (1 + k * .15), width: 1.4, height: 1 + k * .15, fill: VALUE, "fill-opacity": k < 13 ? .8 : .15 }, g);
    }
  }
  // respawn prompt while dead
  if (S.dead && !classic) {
    const g = hud.place(svg, "cc", lay);
    const w = 150; plate(g, 320 - w / 2, 318, w, 20, .45);
    svgEl("path", { d: "M268 322h12v10h-9l-3-3z", fill: "#fff", "fill-opacity": .85 }, g);
    svgEl("text", { x: 274, y: 330, "text-anchor": "middle", "font-family": F_P, "font-size": 6, fill: "#10140a" }, g).textContent = "M1";
    svgEl("text", { x: 285, y: 331.5, ...LP(11, 600), fill: "#fff" }, g).textContent = "Respawn";
    svgEl("text", { x: 368, y: 332, "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 11, fill: VALUE }, g).textContent = "2";
  }
  // aim text under the crosshair
  if (!S.dead && !S.spec) { const aim = svgEl("g", {}, svg);
    shadowText(aim, 320, 272, "Vex", { ...LP(10.6), fill: "#fff", "fill-opacity": .8, "text-anchor": "middle" });
    if (team && !classic) shadowText(aim, 320, 285, "100 · 50", { "font-family": F_C, "font-weight": 600, "font-size": 9.6, fill: "#fff", "fill-opacity": .8, "text-anchor": "middle" }); }
}
const CHAT = [
  { name: "Kaz", text: "rail on the bridge" },
  { name: "Orin", team: "m", text: "quad in 20, I'll take the lower route" },
  { name: "server", server: true, text: "Kaz called a vote: map q4dm7" },
  { name: "You", me: true, text: "gg" },
];
const TYPED = "hold the mid and watch the lower door";
let chatK = 0; const MORE = [{ name: "Vex", team: "s", text: "they're stacking red armor, push now" }, { name: "Tal", text: "nice shot" }, { name: "Nyx", team: "s", text: "flag is on the bridge" }];
document.getElementById("mpChatLine")?.addEventListener("click", () => { CHAT.push(MORE[chatK++ % MORE.length]); if (CHAT.length > 12) CHAT.shift(); build(); });
const KILLS = [["You", "railgun", "Nyx"], ["Tal", "shotgun", "You"], ["Orin", "grenadelauncher", "Vex"], ["Kaz", "hyperblaster", "Orin"], ["Vex", "lightninggun", "Tal"]];
let k = 0;
document.getElementById("mpKill")?.addEventListener("click", () => {
  if (feed.length >= (S.mode === "tourney" ? 2 : 4)) { feed.shift(); feedT = performance.now(); }
  feed.push(KILLS[k++ % KILLS.length]); S.notice = true; build();
  clearTimeout(S.noticeT); S.noticeT = setTimeout(() => { S.notice = false; build(); }, 2500);
});
document.getElementById("mpAward")?.addEventListener("click", () => { S.award = true; build(); clearTimeout(S.awardT); S.awardT = setTimeout(() => { S.award = false; build(); }, 3000); });
document.getElementById("mpFlag")?.addEventListener("click", () => { S.flagTaken = !S.flagTaken; if (S.mode !== "ctf") setMode("ctf"); else build(); });
document.getElementById("mpTick")?.addEventListener("click", () => {
  if (S.mode !== "dz") setMode("dz");
  const t0 = performance.now();                       // the score jumps to the team color and relaxes back to white
  const step = now => { const p = Math.max(0, now - t0); S.tick = reduce ? (p < 1000 ? 1 : 0) : Math.pow(.95, p / 16.7); build(); if (S.tick > .02) requestAnimationFrame(step); else { S.tick = 0; build(); } };
  requestAnimationFrame(step);
});
function setMode(m) { S.mode = m; document.querySelectorAll("#mp [data-mode]").forEach(b => b.classList.toggle("is-current", b.dataset.mode === m)); build(); }
document.querySelectorAll("#mp [data-mode]").forEach(b => b.addEventListener("click", () => setMode(b.dataset.mode)));
const tog = (id, key) => { const b = document.getElementById(id); b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); S[key] = on; build(); }, true); };
[["mpWarm", "warm"], ["mpCount", "count"], ["mpLast", "last"], ["mpSD", "sd"], ["mpSpec", "spec"], ["mpDead", "dead"], ["mpVote", "vote"], ["mpVoice", "voice"], ["mpChat", "chat"], ["mpTeamChan", "teamChan"], ["mpCtx", "ctx"]].forEach(([i, key]) => tog(i, key));
const ctl = document.getElementById("mpLayoutCtl"); if (ctl) hud.controls(ctl);
/* The preset and the spectator view. Classic is the page-wide toggle; Competitive is this sheet's preset. */
const syncPreset = () => document.querySelectorAll("#mpPreset .seg").forEach(b => b.setAttribute("aria-pressed", b.dataset.p === (hud.classic ? "classic" : S.comp ? "comp" : "rem")));
document.getElementById("mpPreset")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; const p = b.dataset.p;
  S.comp = p === "comp"; if (hud.classic !== (p === "classic")) hud.set("classic", p === "classic"); else build(); syncPreset(); });
document.getElementById("mpSpecMode")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  S.specMode = b.dataset.s; S.spec = S.specMode !== "off"; document.querySelectorAll("#mpSpecMode .seg").forEach(x => x.setAttribute("aria-pressed", x === b)); build(); });
build(); hud.on(build); hud.on(syncPreset);
})();
