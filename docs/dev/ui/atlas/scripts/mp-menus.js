
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const svg = document.getElementById("mmSvg");
if (!svg) return;
/* The retained multiplayer cards (section 14.18; section 14.14 for the match summary), drawn from their generators:
   tools/ui/retained_mp_menus.py (Welcome, Escape), retained_mp_admin.py (Admin), retained_mp_summary.py (summary) and
   retained_mp_buy.py (buy). Everything inside the card is authored in dp, as the generators are, and the card is placed
   in stock units (1.5 dp a unit in landscape). Marine is Michroma, Lowpixel Archivo, ProFont IBM Plex Mono. */
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const RAIL_CARD = "#A0A040", OLIVE = "#8B964B", MARKER = "#909A49", ORANGE = "#E38900", VALUE = "#FFBE23", TABFILL = "#5A652A";
const ERR = "#E46D56", NOTE = "#FFFF8D", HEADC = "#8B964B", DARK = "#0A0D09", CHAT_RAIL = "#CCCC51";
const BAND = ["#6AA42B", "#FF7B04", "#999999", "#8B964B"];          // BAND_TINTS: Marine, Strogg, spectators, deathmatch olive
const TEAM_TEXT = ["#AAE355", "#FF8E00"];
let uid = 0; const id = p => `mm${p}${++uid}`;
const measure = (t, size, fam = F_L, w = 500) => A.measureText ? A.measureText(t, size, fam, w) : t.length * size * .55;
/* Michroma stands in for Marine: drawn at 0.78 of the size with 0.04 em tracking, its capitals match Marine's height
   and advance in the engine captures; lowercase is set in small capitals, as Marine draws it. */
const MF = .78, MLS = .04;
const mw = (t, s) => measure(t.toUpperCase(), s * MF * (/[a-z]/.test(t) ? .94 : 1), F_M, 400) + t.length * s * MF * MLS;

/* Text: `top` and `h` give the line box; the baseline sits in its middle. */
function T(g, x, top, h, s, o = {}) {
  const { f = "l", size = 14, fill = "#fff", op = 1, anchor = "start", w } = o;
  const fam = f === "m" ? F_M : f === "p" ? F_P : F_L;
  const fs = f === "m" ? size * MF : size;
  const attrs = { x, y: top + h / 2 + fs * .36, "font-family": fam, "font-weight": f === "m" ? 400 : 500, "font-size": fs, fill, "fill-opacity": op, "text-anchor": anchor };
  if (f === "m" && /[a-z]/.test(s)) attrs["font-variant"] = "small-caps";
  if (f === "m") attrs["letter-spacing"] = (fs * MLS).toFixed(2);
  if (o.ls) attrs["letter-spacing"] = o.ls;
  const e = svgEl("text", attrs, g); e.textContent = s;
  if (w) { const room = w, have = f === "m" ? mw(s, size) : measure(s, size); if (have > room) e.setAttribute("textLength", room), e.setAttribute("lengthAdjust", "spacingAndGlyphs"); }
  return e;
}
const P = (g, d, o = {}) => svgEl("path", { d, ...o }, g);
function grad(stops, { x1 = 0, x2 = 1, y1 = 0, y2 = 0 } = {}) {
  const gid = id("g"), lg = svgEl("linearGradient", { id: gid, x1, x2, y1, y2 }, ui.defs);
  stops.forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, lg));
  return `url(#${gid})`;
}

/* ---------- State ---------- */
const VARIANTS = {
  welcome: { w: 873, tabs: ["JOIN", "SERVER", "PLAYERS", "SETTINGS"] },
  escape: { w: 907, tabs: ["TEAM", "PLAYERS", "VOTE", "MATCH", "SETTINGS", "VOICE", "SERVER", "ADMIN"] },
  summary: { w: 907, tabs: ["SUMMARY", "STATISTICS", "SCOREBOARD"] },
  buy: { w: 907, tabs: ["WEAPONS", "MODS", "ARMOR & AMMO", "TEAM"] },
};
const H = 477, HEAD = 40, TAB_TOP = 52, RISE = 30, PAGE_TOP = 98, INSET = 24, PROMPT_H = 22, PROMPT_Y = 477 - 16 - 22, PAGE_H = 325;
let variant = "welcome", mode = "team", dev = "kbm", soft = true;
let fresh = true, tab = 0, focus = 0, matchSec = 0, adminSec = 0, admin = false, closing = false, closed = false, modal = null, typing = false, pick = "Vex";
const status = document.getElementById("mmStatus");
const say = t => { if (status) status.textContent = t; };
const MAP = { team: "Heartless", dm: "The Fragging Yard", tourney: "The Fragging Yard 1v1" };
const MODE = { team: "Capture the Flag", dm: "Deathmatch", tourney: "Tourney" };

/* Rosters [name, score, ping, flags]; in Welcome you are still spectating, elsewhere you play for the Marines. */
function roster() {
  const me = variant !== "welcome";
  const m = [["Kaz", 18, 38], ["Orin", 15, 51], ["Bolt", 9, 0, "bot"], ["Pike", 7, 88, "dead"], ["Reed", 4, 140]];
  if (me) m.unshift(["You", 19, 24, "me"]); else m.push(["Sarge", 12, 61]);
  const s = [["Vex", 24, 31], ["Nyx", 19, 47], ["Tal", 12, 66], ["Juno", 10, 95], ["Ash", 6, 120]];
  const spec = me ? [["Quill", 0, 44]] : [["You", 0, 24, "me"], ["Quill", 0, 44]];
  if (mode === "team") return [["MARINES", 0, "3", m], ["STROGG", 1, "2", s], ["SPECTATORS", 2, "", spec]];
  const all = [...m, ...s].sort((a, b) => b[1] - a[1]);
  return [["PLAYERS", 3, "", all], ["SPECTATORS", 2, "", spec]];
}

/* ---------- Glyphs: keycaps (section 13.4) or the controller's buttons (section 13.5) ---------- */
const PAD = { ESCAPE: "B", ENTER: "A", Q: "LB", E: "RB" };
function cap(g, x, top, key, { fixed = false, glyphs = true } = {}) {     // returns the width
  const pad = glyphs && dev === "pad" && PAD[key];
  if (pad) {
    if (pad.length === 1) { svgEl("circle", { cx: x + 11, cy: top + 11, r: 10.5, fill: "#fff", "fill-opacity": .85 }, g); T(g, x + 11, top, 22, pad, { size: 14, fill: DARK, anchor: "middle", op: 1 }); return 22; }
    const w = fixed ? 20 : 22; svgEl("rect", { x, y: top + 4, width: w, height: 14, rx: 3, fill: "#fff", "fill-opacity": .85 }, g);
    T(g, x + w / 2, top + 4, 14, pad, { size: 9.5, fill: DARK, anchor: "middle" }); return w;
  }
  const w = fixed ? 20 : 14 + measure(key, 13);
  P(g, `M${x} ${top}H${x + w}V${top + 22}H${x + 6}L${x} ${top + 16}Z`, { fill: "#fff", "fill-opacity": .85 });
  T(g, x + w / 2, top, 22, key, { size: 13, fill: DARK, anchor: "middle" });
  return w;
}
function lock(g, x, top, col = ERR) {                    // the padlock after an unavailable label: 12 x 14 dp
  P(g, `M${x + 3} ${top + 7}V${top + 3.5}L${x + 4.5} ${top + 1.5}H${x + 7.5}L${x + 9} ${top + 3.5}V${top + 7}`, { fill: "none", stroke: col, "stroke-width": 1.5 });
  P(g, `M${x + 1} ${top + 6.5}H${x + 11}V${top + 13.5}H${x + 1}Z`, { fill: col });
}
const tri = (g, x, y, s, fill, op = 1) => P(g, `M${x} ${y}H${x + s}V${y + s}Z`, { fill, "fill-opacity": op });   // the ◥ marker
function teamBand(g, x, y, w, h, tint, cut = 8) {        // header band in a team's color (section 6)
  P(g, `M${x + cut} ${y}H${x + w}V${y + h}H${x}V${y + cut}Z`, { fill: grad([[0, tint, .49], [.5, tint, .49], [.75, tint, .25], [.95, tint, .05], [1, tint, 0]]) });
  P(g, `M${x + .75} ${y + h}V${y + cut}L${x + cut} ${y + .75}H${x + w * .6}`, { fill: "none", stroke: grad([[0, tint, 1], [.7, tint, 1], [1, tint, 0]]), "stroke-width": 1.5 });
}
const mark = (g, team, x, y, s) => A.icon(g, team === 0 ? "marinelogo" : "strogglogo", { x, y, h: s, color: "#fff" });
const heading = (g, x, top, s, w) => T(g, x, top, 20, s, { size: 14, fill: HEADC, w });

/* ---------- Focus ---------- */
let focusables = [];
function control(update, act, root) {
  const idx = focusables.length, c = { update, act, root };
  focusables.push(c);
  if (root) { root.style.cursor = "pointer"; root.addEventListener("pointerenter", () => setFocus(idx)); root.addEventListener("click", e => { e.stopPropagation(); setFocus(idx); activate(); }); }
  update(false); return c;
}
function setFocus(i) { if (!focusables.length) return; ui.tabLabel?.setAttribute("fill", "#fff"); focus = Math.max(0, Math.min(focusables.length - 1, i)); focusables.forEach((c, k) => c.update(k === focus)); }
function activate() { focusables[focus]?.act?.(); }
function shake(root, reason) {
  say(`Unavailable: ${reason}`);
  if (reduce || !root) return;
  const t0 = performance.now(), step = n => { const q = Math.min(1, (n - t0) / 300), k = [0, -6, 6, -4, 3, 0], i = Math.min(4, Math.floor(q * 5)), f = q * 5 - i;
    root.setAttribute("transform", `translate(${k[i] + (k[i + 1] - k[i]) * f} 0)`); if (q < 1) requestAnimationFrame(step); };
  requestAnimationFrame(step);
}

/* An in-game plate (section 14.18, b6_light): 45 dp, band 5-40 with a 20 dp lower-leading cut, 0.62 for the primary
   action and 0.28 for the others, rising to 1.00 with the orange marker and inset rail. Marine 20. An action plate
   adds the lock and its reason in the error colour (or its detail at 0.70) under the label; unavailable it shows at 0.60. */
function plate(g, x, y, w, label, { primary, sub, reason, act, keycap, h = 45, size = 20 } = {}) {
  const off = reason != null, root = svgEl("g", {}, g), body = svgEl("g", { opacity: off ? .6 : 1 }, root);
  const rest = primary ? .62 : .28, bt = h === 45 ? 5 : 3, bb = h === 45 ? 40 : h - 3, cut = h === 45 ? 20 : 12;
  const pl = svgEl("g", {}, body);
  P(pl, `M${x} ${y + bt}H${x + w}V${y + bb}H${x + cut}L${x} ${y + bb - cut}Z`, { fill: grad([[0, OLIVE, .49], [.33, OLIVE, .49], [1, OLIVE, 0]]) });
  P(pl, `M${x + .75} ${y + bt}V${y + bb - cut}L${x + cut} ${y + bb - .75}H${x + w}`, { fill: "none", stroke: grad([[0, OLIVE, 1], [.33, OLIVE, 1], [1, OLIVE, 0]]), "stroke-width": 1.5 });
  const inset = P(body, `M${x + 3.5} ${y + bt + 3}V${y + bb - cut + 1.5}L${x + cut + 1.5} ${y + bb - 3.5}H${x + w - 20}`, { fill: "none", stroke: ORANGE, "stroke-width": 1.2 });
  const ms = h === 45 ? 8 : 7, mx = h === 45 ? 14 : 10, my = h === 45 ? 14 : 9;
  const mk = tri(body, x + mx, y + my, ms, MARKER), mh = tri(body, x + mx, y + my, ms, ORANGE);
  const lx = x + (h === 45 ? 26 : 60), lab = T(body, lx, y, h, label, { f: "m", size, op: .8 });
  let after = lx + mw(label, size) + 8;
  if (keycap) after += cap(body, after, y + (h - 22) / 2, keycap, { glyphs: false }) + 8;
  if (off) lock(body, after, y + (h - 14) / 2);
  if (off) T(root, x + 26, y + 40, 18, reason, { size: 14, fill: ERR });
  else if (sub) T(root, x + 26, y + 40, 18, sub, { size: 14, op: .7 });
  return control(on => {
    pl.setAttribute("opacity", on ? 1 : rest); inset.setAttribute("opacity", on ? 1 : 0);
    mk.setAttribute("opacity", on ? 0 : rest); mh.setAttribute("opacity", on ? 1 : 0);
    lab.setAttribute("fill", on ? ORANGE : "#fff"); lab.setAttribute("fill-opacity", on ? 1 : .8);
  }, () => off ? shake(root, reason) : act?.(), root);
}

/* A setting row (section 7): a light plate (olive 0.12 inside an olive 0.32 line, 6 dp cut), the label 21 dp in, the
   value at the value column in the value colour; focus brings the orange rail and the wash. */
function row(g, x, y, w, label, vx, kind, value, { h = 26, act, locked, reason } = {}) {
  const root = svgEl("g", { opacity: locked ? .38 : reason ? .6 : 1 }, g);
  P(root, `M${x + 6} ${y + 1}H${x + w}V${y + h - 1}H${x}V${y + 7}Z`, { fill: OLIVE, "fill-opacity": .12, stroke: OLIVE, "stroke-opacity": .32 });
  const wash = P(root, `M${x + 6} ${y + 1}H${x + w}V${y + h - 1}H${x}V${y + 7}Z`, { fill: grad([[0, OLIVE, .45], [1, OLIVE, .1]]) });
  const rail = P(root, `M${x} ${y + 7}L${x + 6} ${y + 1}V${y + h - 1}H${x}Z`, { fill: ORANGE });
  let lab = null;
  if (label) { lab = T(root, x + 21, y, h, label, { size: label.length > 16 ? 14 : 15, op: .85, w: vx - 29 }); if (locked || reason) lock(root, x + 25 + Math.min(vx - 37, measure(label, label.length > 16 ? 14 : 15)), y + (h - 14) / 2); }
  const vxa = x + vx;
  if (kind === "choice") { T(root, vxa, y, h, value, { size: 14, fill: VALUE, w: w - vx - 40 }); P(root, `M${x + w - 24} ${y + 10}l6 6 6-6`, { fill: "none", stroke: VALUE, "stroke-width": 1.5 }); }
  else if (kind === "toggle") { P(root, `M${vxa + .75} ${y + 6.75}h12.5v12.5h-12.5Z`, { fill: "none", stroke: VALUE, "stroke-width": 1.5 }); if (value) P(root, `M${vxa + 3.5} ${y + 9.5}h7v7h-7Z`, { fill: VALUE }); }
  else if (kind === "slider") {
    const [v, lo, hi, step, txt] = value, tw = w - vx - 12 - 56, n = Math.min(40, Math.round((hi - lo) / step)), f = (v - lo) / (hi - lo);
    for (let i = 0; i <= n; i++) { const tx = vxa + tw * i / n, e = 3 + 5 * i / n; P(root, `M${tx} ${y + 13 - e}V${y + 13 + e}`, { stroke: "#fff", "stroke-opacity": .45 }); }
    svgEl("rect", { x: vxa, y: y + 11, width: tw * f, height: 4, fill: VALUE, "fill-opacity": .8 }, root);
    svgEl("rect", { x: vxa + tw * f - 2, y: y + 5, width: 4, height: 16, fill: VALUE }, root);
    T(root, vxa + tw + 56, y, h, txt, { size: 15, fill: VALUE, anchor: "end" });
  } else if (kind === "field") {
    P(root, `M${vxa} ${y + 3}H${x + w - 6}V${y + h - 3}H${vxa}Z`, { fill: "#000", "fill-opacity": .45, stroke: OLIVE, "stroke-opacity": .5 });
    T(root, vxa + 6, y, h, value, { size: 15, op: .9, f: value.startsWith("•") ? "p" : "l" });
  }
  if (reason) T(root, vxa, y, h, reason, { size: 14, fill: ERR, w: w - vx - 8 });
  return control(on => { wash.setAttribute("opacity", on ? 1 : 0); rail.setAttribute("opacity", on ? 1 : 0); lab?.setAttribute("fill", on ? ORANGE : "#fff"); lab?.setAttribute("fill-opacity", on ? 1 : .85); },
    () => locked || reason ? shake(root, reason || "locked") : act?.(), root);
}

/* ---------- Player lists (section 14.18 with the scoreboard's rows, section 14.14) ---------- */
function speaker(g, x, y, muted) {
  P(g, `M${x + 1} ${y + 5}h3l4-3.5v11L${x + 4} ${y + 9}h-3z`, { fill: "#fff", "fill-opacity": .7 });
  P(g, `M${x + 10} ${y + 4.5}q2 2.5 0 5`, { fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.2 });
  if (muted) P(g, `M${x + 1.5} ${y + 12.5}L${x + 12.5} ${y + 1.5}`, { stroke: ERR, "stroke-width": 1.6 });
}
function friend(g, x, y, on) {
  const s = svgEl("g", { opacity: on ? 1 : .125 }, g);
  svgEl("circle", { cx: x + 7, cy: y + 4.5, r: 3, fill: "#fff" }, s); P(s, `M${x + 1.5} ${y + 13}a5.5 5 0 0 1 11 0z`, { fill: "#fff" });
}
function skull(g, x, y) { P(g, `M${x + 4} ${y}a4 4 0 0 1 4 4c0 1.4-.7 2-1.3 2.7v1.3H${x + 1.3}V${y + 6.7}C${x + .7} ${y + 6} ${x} ${y + 5.4} ${x} ${y + 4}a4 4 0 0 1 4-4z`, { fill: "#fff" }); }
function botTag(g, x, y) { P(g, `M${x} ${y}h28v15h-24l-4-4z`, { fill: OLIVE, "fill-opacity": .85 }); T(g, x + 14, y, 15, "BOT", { size: 10.8, fill: DARK, anchor: "middle", ls: .6 }); }
const barsLevel = p => p <= 60 ? 4 : p <= 120 ? 3 : p <= 200 ? 2 : 1;
function bars(g, x, y, ping) {                                    // network quality (section 13.13): 22 x 16 dp
  const n = barsLevel(ping), c = n >= 3 ? "#B5C784" : n === 2 ? "#E3AD36" : ERR;
  for (let b = 0; b < 4; b++) { const bh = 4.5 + b * 3.3; svgEl("rect", { x: x + 2 + b * 5, y: y + 15 - bh, width: 3.3, height: bh, fill: c, "fill-opacity": b < n ? 1 : .2 }, g); }
}
/* The lists: headings once (Lowpixel 11 at 0.40 over a white 0.10 rule), then each list's band in its team's color with
   its title and score over 24 dp rows; a scrolling viewport under the headings with the authored bar in a 36 dp gutter.
   `select`: rows are controls (Players, STATISTICS); the chosen player's row is lit in olive. */
function playerLists(g, W, h, { select } = {}) {
  const w = W - 36, sw = Math.max(measure("SCORE", 11), measure("-999", 14)) + 2, pw = Math.max(measure("PING", 11), measure("999", 14)) + 2;
  const nameL = 12 + 18 + 20 + 18 + 2, scoreR = w - 4 - pw - 8 - 28;
  const hd = { size: 11, op: .4 };
  T(g, nameL, 0, 16, "NAME", hd); T(g, scoreR, 0, 16, "SCORE", { ...hd, anchor: "end" }); T(g, w - 4, 0, 16, "PING", { ...hd, anchor: "end" });
  P(g, `M0 15.5H${w}`, { stroke: "#fff", "stroke-opacity": .1 });
  const clip = id("clip"); svgEl("rect", { x: -2, y: 16, width: W + 4, height: h - 16 }, svgEl("clipPath", { id: clip }, ui.defs));
  const view = svgEl("g", { "clip-path": `url(#${clip})` }, g);
  let y = 16;
  roster().forEach(([title, tint, score, rows], k) => {
    if (!rows.length) return;
    if (k) y += 6;
    teamBand(view, 0, y, w, 22, BAND[tint], 6);
    if (tint < 2) mark(view, tint, 8, y + 4, 14);
    T(view, 28, y, 22, title, { f: "m", size: 15, op: .95 });
    if (score) T(view, scoreR, y, 22, score, { size: 16, fill: VALUE, anchor: "end" });
    y += 22;
    rows.forEach(([n, sc, ping, flag]) => {
      const me = flag === "me", dead = flag === "dead", ry = y, rg = svgEl("g", {}, view);
      svgEl("rect", { x: 0, y: ry + 1, width: w, height: 22, fill: "#fff", "fill-opacity": me ? .29 : .08 }, rg);
      const chosen = select && n === pick ? svgEl("rect", { x: 0, y: ry, width: w, height: 24, fill: grad([[0, OLIVE, .6], [.4, OLIVE, .45], [1, OLIVE, 0]]) }, rg) : null;
      const fl = select ? P(rg, `M.75 ${ry + 1}V${ry + 22.5}H${w}`, { fill: "none", stroke: ORANGE, "stroke-width": 1.2 }) : null;
      const c = svgEl("g", { opacity: dead ? .5 : 1, "pointer-events": "none" }, rg);
      if (me) tri(c, 2, ry + 8, 8, ORANGE);
      speaker(c, 2 + 12, ry + 5, n === "Reed"); friend(c, 2 + 30, ry + 5, n === "Kaz");
      if (dead) skull(c, 2 + 52, ry + 8);
      const nm = T(c, nameL, ry, 24, n, { size: 14, op: .85 });
      if (flag === "bot") botTag(c, scoreR - sw - 40, ry + 4.5);    // the tag ends the growing name cell
      if (tint !== 2) T(c, scoreR, ry, 24, String(sc), { size: 14, op: .85, anchor: "end" });
      if (flag !== "bot") { bars(c, scoreR + 6, ry + 4, ping); T(c, w - 4, ry, 24, String(ping), { size: 14, op: .6, anchor: "end" }); }
      if (select && n === pick) ui.pickFocus = focusables.length;
      if (select) control(on => { fl.setAttribute("opacity", on ? 1 : 0); nm.setAttribute("fill", on ? ORANGE : "#fff"); nm.setAttribute("fill-opacity", on ? 1 : .85); },
        () => { pick = n; build(false); say(`${n}'s statistics.`); }, rg);
      y += 24;
    });
  });
  scrollbar(g, w, h, y);
}
function scrollbar(g, w, h, y) {                 // the shared authored bar in the 36 dp gutter; its thumb once the rows overflow
  svgEl("rect", { x: w + 15, y: 16, width: 4, height: h - 16, fill: OLIVE, "fill-opacity": .22 }, g);
  if (y > h) svgEl("rect", { x: w + 14, y: 16, width: 6, height: (h - 16) * (h - 16) / (y - 16), fill: ORANGE }, g);
}

/* The award medals stay bitmaps in the game (section 6); these are vector stand-ins in the medals' colors. */
const MEDALS = [["excellent", "#E8C33A"], ["impressive", "#3FC6E0"], ["humiliation", "#D8A23A"], ["combo_kill", "#E05A3F"], ["rampage", "#E07A2A"], ["capture", "#C9C9C9"], ["assist", "#7FB24A"], ["defense", "#5A8FE0"]];
function medal(g, x, y, c, on) {
  const m = svgEl("g", { opacity: on ? 1 : .35 }, g);
  svgEl("circle", { cx: x + 11, cy: y + 11, r: 10, fill: "none", stroke: c, "stroke-width": 1.6 }, m);
  P(m, `M${x + 11} ${y + 4}l2 4.5 5 .5-3.8 3.3 1.2 4.9-4.4-2.6-4.4 2.6 1.2-4.9-3.8-3.3 5-.5z`, { fill: c });
}
const WEAP = [["item_machinegun", "#FFFF00"], ["item_shotgun", "#FF8000"], ["item_hyperblaster", "#0073FF"], ["item_grenade", "#338F12"], ["item_nailgun", "#99CCCC"],
  ["item_rocket", "#FF3300"], ["item_railgun", "#00FF00"], ["item_lightning", "#FFFFBA"], ["item_darkmatter", "#C400FF"], ["item_fire", "#FFBF40"]];
/* The selected player's statistics (section 14.18): the band and name, kills/deaths/score, accuracy for ten weapons in
   five columns, the award medals (the flag awards only in flag modes), then Mute and Friend or the match awards. */
function playerStats(g, x, w, { plates = true, awards } = {}) {
  const me = pick === "You", team = mode === "team" ? (["Vex", "Nyx", "Tal", "Juno", "Ash"].includes(pick) ? 1 : ["Quill"].includes(pick) ? 2 : 0) : 3;
  teamBand(g, x, 0, w, 26, BAND[team]);
  T(g, x + 18, 0, 26, pick, { f: "m", size: 18, op: .95 });
  [["KILLS", 24], ["DEATHS", 5], ["SCORE", 24]].forEach(([k, v], i) => { const cx = x + i * w / 3; T(g, cx, 32, 14, k, { size: 12, fill: HEADC }); T(g, cx, 46, 26, String(v), { size: 22, fill: VALUE }); });
  T(g, x, 78, 14, "ACCURACY", { size: 12, fill: HEADC });
  const acc = ["32%", "41%", "-", "18%", "-", "28%", "61%", "35%", "-", "-"];
  WEAP.forEach(([icon, c], i) => { const cx = x + (i % 5) * w / 5, cy = 94 + 22 * Math.floor(i / 5), quiet = acc[i] === "-";
    A.icon(g, icon, { x: cx, y: cy + 2, h: 18, color: c, opacity: quiet ? .35 : 1 }); T(g, cx + 24, cy, 22, acc[i], { size: 15, op: .85 }); });
  T(g, x, 144, 14, "AWARDS", { size: 12, fill: HEADC });
  const shown = mode === "team" ? MEDALS : MEDALS.slice(0, 5), counts = [3, 2, 0, 1, 0, 1, 2, 0], px = w / 8;
  shown.forEach(([n, c], i) => { medal(g, x + i * px, 161, c, counts[i] > 0); T(g, x + i * px + 25, 160, 24, String(counts[i]), { size: 15, fill: VALUE }); });
  if (plates) {
    plate(g, x, 192, w, "MUTE PLAYER", { reason: me ? "You can't mute yourself." : null, act: () => say(`${pick} is muted.`) });
    plate(g, x, 254, w, "ADD FRIEND", { reason: me ? "You can't add yourself as a friend." : null, act: () => say(`${pick} is on your friends list.`) });
  }
  if (awards) awards.forEach((a, i) => {                 // the end-of-match awards (summary STATISTICS)
    const ay = 196 + i * 34;
    P(g, `M${x} ${ay}H${x + w}V${ay + 28}H${x + 10}L${x} ${ay + 18}Z`, { fill: grad([[0, VALUE, .18], [.5, VALUE, .08], [1, VALUE, 0]]) });
    P(g, `M${x + .75} ${ay}V${ay + 18}L${x + 10} ${ay + 27.25}H${x + w * .6}`, { fill: "none", stroke: grad([[0, VALUE, .9], [1, VALUE, 0]]), "stroke-width": 1.5 });
    tri(g, x + 12, ay + 10, 8, VALUE); T(g, x + 28, ay, 28, a, { f: "m", size: 17, op: .92 });
  });
}

/* ---------- Pages ---------- */
function serverPage(g, W) {
  T(g, 0, 0, 28, "openQ4 Community", { f: "m", size: 20, op: .9 });
  T(g, 0, 30, 20, "ADDRESS", { size: 14, fill: HEADC }); T(g, 124, 30, 20, "203.0.113.24:28004", { f: "p", size: 15, op: .75 });
  ["Welcome to the openQ4 community server. Play fair, call your", "flag runs, have fun. Match nights: Fridays 20:00 UTC."].forEach((l, i) => T(g, 0, 56 + i * 20, 20, l, { size: 17, fill: NOTE }));
  const rules = [["MODE", MODE[mode]], ["MAP", MAP[mode]], ["LIMITS", mode === "team" ? "Capture Limit 5 · Time Limit 15" : mode === "dm" ? "Frag Limit 30 · Time Limit 15" : "Frag Limit 15 · Rounds 4"], ["PLAYERS", "11/16"],
    ["FRIENDLY FIRE", mode === "team" ? "OFF" : "-"], ["TEAM BALANCE", mode === "team" ? "ON" : "-"], ["NEXT MAP", mode === "team" ? "Over the Edge" : "Speed Trap"]];
  const split = W * .55, lw = [Math.max(...rules.slice(0, 4).map(r => measure(r[0], 14))) + 6, Math.max(...rules.slice(4).map(r => measure(r[0], 14))) + 6];
  rules.forEach(([k, v], i) => { const side = i < 4 ? 0 : 1, x = side ? split : 0, top = 124 + 24 * (i % 4);
    T(g, x, top, 22, k, { size: 14, fill: HEADC }); T(g, x + lw[side] + 6, top, 22, v, { size: 15, op: .85 }); });
  heading(g, 0, 228, "MAP ROTATION");
  const rot = mode === "team" ? ["Heartless", "Over the Edge", "Death Before Dishonor", "Lost Fleet"] : mode === "dm" ? ["The Fragging Yard", "Speed Trap", "Over the Edge", "Campgrounds Redux", "Bloodworks"] : ["The Fragging Yard 1v1", "Speed Trap 1v1"];
  rot.forEach((m, i) => T(g, (i % 4) * W / 4, 250 + 19 * Math.floor(i / 4), 19, m, { size: 15, fill: i === 0 ? ORANGE : "#fff", op: i === 0 ? 1 : .7, w: W / 4 - 8 }));
  ui.focusTab = true;
}
function settingsPlates(g, top, w) {
  ["CONTROLS", "GAME OPTIONS", "SYSTEM"].forEach((l, i) => plate(g, 0, top + 50 * i, w, l, { act: () => say(`Leaves for the main menu's ${l.toLowerCase()} page.`) }));
}
const RAILS = ["#FF0000", "#FF8000", "#FFFF00", "#00FF00", "#00FFFF", "#0000FF", "#FF0080"];
let rail = 3;
function railRow(g, y, w, vx) {
  const root = svgEl("g", {}, g);
  P(root, `M6 ${y + 1}H${w}V${y + 25}H0V${y + 7}Z`, { fill: OLIVE, "fill-opacity": .12, stroke: OLIVE, "stroke-opacity": .32 });
  T(root, 21, y, 26, "Rail Color", { size: 15, op: .85 });
  RAILS.forEach((c, i) => { const sx = vx + 20 * i, s = svgEl("g", {}, root), on = i === rail;
    const ring = P(s, `M${sx + .75} ${y + 2.75}h16.5v20.5h-16.5Z`, { fill: "none", stroke: ORANGE, "stroke-width": 1.5 });
    svgEl("rect", { x: sx + 3, y: y + 8 - (on ? 3 : 0), width: 12, height: on ? 16 : 10, fill: c }, s);
    control(f => ring.setAttribute("opacity", f ? 1 : 0), () => { rail = i; build(false); say("Rail color set."); }, s); });
}
const PAGES = {
  welcome: [
    (g, W) => {                                        // JOIN
      T(g, 0, 0, 26, `${MODE[mode]} - ${MAP[mode]}`, { f: "m", size: 18, op: .9 });
      let x = 0; [["MATCH IN PROGRESS", 15, "#fff", .8], ["7:12", 17, VALUE, 1], [mode === "team" ? "Capture Limit 5" : "Frag Limit " + (mode === "dm" ? 30 : 15), 15, "#fff", .7], [mode === "team" ? "11/16 PLAYERS" : "9/16 PLAYERS", 15, "#fff", .7]]
        .forEach(([s, size, fill, op]) => { T(g, x, 28, 22, s, { size, fill, op }); x += measure(s, size) + 16; });
      if (mode === "team") {
        const cw = (W - 12) / 2;
        [[0, "MARINES", "3", "6", "FLAG AT BASE", "Team is full"], [1, "STROGG", "2", "5", "FLAG TAKEN", null]].forEach(([t, name, sc, n, flag, reason]) => {
          const cx = t * (cw + 12), cy = 56, root = svgEl("g", { opacity: reason ? .6 : 1 }, g);
          const body = svgEl("g", {}, root);
          P(body, `M${cx} ${cy}H${cx + cw}V${cy + 100}H${cx + 12}L${cx} ${cy + 88}Z`, { fill: grad([[0, OLIVE, .4], [.6, OLIVE, .25], [1, OLIVE, .08]]) });
          P(body, `M${cx + .75} ${cy}V${cy + 88}L${cx + 12} ${cy + 99.25}H${cx + cw}`, { fill: "none", stroke: grad([[0, OLIVE, 1], [.6, OLIVE, 1], [1, OLIVE, 0]]), "stroke-width": 1.5 });
          teamBand(root, cx, cy, cw, 30, BAND[t]); mark(root, t, cx + 12, cy + 5, 20);
          const nm = T(root, cx + 40, cy, 30, name, { f: "m", size: 20, op: .95 });
          T(root, cx + cw - 12, cy, 30, sc, { size: 22, fill: VALUE, anchor: "end" });
          T(root, cx + 16, cy + 36, 20, "PLAYERS", { size: 13, fill: HEADC }); T(root, cx + cw * .45 + 16, cy + 36, 20, n, { size: 16, op: .9 });
          T(root, cx + 16, cy + 58, 18, flag, { size: 14, op: .75 });
          if (reason) { lock(root, cx + cw - 30, cy + 39); T(root, cx + 16, cy + 78, 18, reason, { size: 14, fill: ERR }); }
          const fr = P(root, `M${cx + 3.5} ${cy + 33}V${cy + 87}L${cx + 13.5} ${cy + 96.5}H${cx + cw - 20}`, { fill: "none", stroke: ORANGE, "stroke-width": 1.2 });
          control(on => { body.setAttribute("opacity", on ? 1 : .62); fr.setAttribute("opacity", on ? 1 : 0); nm.setAttribute("fill", on ? ORANGE : "#fff"); },
            () => reason ? shake(root, reason) : join(name), root);
        });
        plate(g, 0, 166, Math.min(W, 640), "AUTO JOIN", { primary: true, sub: "Joins STROGG.", act: () => join("STROGG") });
        plate(g, 0, 228, Math.min(W, 640), "SPECTATE", { act: () => close("Spectating. The menu key opens the card again.") });
        ui.defaultFocus = 2;
      } else if (mode === "dm") {
        T(g, 0, 56, 18, "LEADERS", { size: 13, fill: HEADC });
        [["Vex", 24], ["Nyx", 19], ["Kaz", 18]].forEach(([n, s], r) => { const top = 76 + 22 * r;
          T(g, 0, top, 22, String(r + 1), { size: 16, fill: VALUE }); T(g, 28, top, 22, n, { size: 16, op: .85 }); T(g, W * .6 + 92, top, 22, String(s), { size: 16, fill: VALUE, anchor: "end" }); });
        plate(g, 0, 156, Math.min(W, 640), "JOIN GAME", { primary: true, act: () => join("the match") });
        plate(g, 0, 218, Math.min(W, 640), "SPECTATE", { act: () => close("Spectating. The menu key opens the card again.") });
        ui.defaultFocus = 0;
      } else {
        T(g, 0, 56, 18, "ARENAS", { size: 13, fill: HEADC });
        ["Arena 1   Vex 9 - 6 Kaz", "Arena 2   Nyx 3 - 3 Tal"].forEach((l, a) => { const s = svgEl("text", { x: 0, y: 76 + 18 * a + 9 + 5.4, "font-family": F_L, "font-weight": 500, "font-size": 15, fill: "#fff", "fill-opacity": .8, style: "white-space:pre" }, g); s.textContent = l; });
        plate(g, 0, 156, Math.min(W, 640), "Join the tournament", { primary: true, act: () => join("the tournament") });
        plate(g, 0, 218, Math.min(W, 640), "SPECTATE", { act: () => close("Spectating. The menu key opens the card again.") });
        ui.defaultFocus = 0;
      }
    },
    (g, W) => serverPage(g, W),                         // SERVER
    (g, W) => { playerLists(g, W, PAGE_H); ui.focusTab = true; },   // PLAYERS: read-only, the bar takes focus when it can scroll
    (g, W) => {                                        // SETTINGS
      const lw = 400, vx = 118; heading(g, 0, 0, "PLAYER");
      row(g, 0, 24, lw, "Name", vx, "field", "Kaz"); row(g, 0, 50, lw, "Clan Tag", vx, "field", "DMP");
      row(g, 0, 76, lw, "Model", vx, "choice", "Marine Kane"); railRow(g, 102, lw, vx);
      row(g, 0, 128, lw, "Crosshair", vx, "slider", [4, 0, 20, 1, "4"]);
      const s = svgEl("svg", { x: lw + 16, y: 124, width: 32, height: 32, viewBox: "0 0 64 64", overflow: "visible" }, g);
      s.innerHTML = A.uniqueIds ? A.uniqueIds(A.CROSSHAIRS?.crosshair_q3_3?.svg || A.CROSSHAIRS?.[Object.keys(A.CROSSHAIRS || {})[3]]?.svg || "") : "";
      settingsPlates(g, 166, lw); ui.defaultFocus = 0;
    },
  ],
  escape: [
    (g, W) => {                                        // TEAM
      const ph = PAGE_H, [band, tint, score, detail] = mode === "team" ? ["MARINES", 0, "3", "6 - 5"] : ["PLAYERS", 3, "19", "3 / 11"];
      teamBand(g, 0, 0, W, 36, BAND[tint]);
      T(g, 20, 0, 36, band, { f: "m", size: 20, op: .95 }); T(g, W * .7, 0, 36, score, { size: 20, fill: VALUE, anchor: "end" }); T(g, W * .72, 0, 36, detail, { size: 17, op: .8 });
      const sw = Math.min(W, 640);
      if (mode === "team") plate(g, 0, 50, sw, "SWITCH TEAM", { primary: true, sub: "MARINES 5 - 6 STROGG", act: () => close("Switched to Strogg; you respawn with them.") });
      else plate(g, 0, 50, sw, "SPECTATE", { primary: true, act: () => close("Spectating.") });
      if (mode === "team") plate(g, 0, 112, sw, "SPECTATE", { act: () => close("Spectating.") });
      plate(g, 0, mode === "team" ? 174 : 112, sw, "READY", { reason: "Ready is only used in a warm-up." });
      ["Kaz: rail on the bridge", "Orin: quad in 20, I'll take the lower route", "Vex: gg"].forEach((l, i) => T(g, 0, ph - 22 * (3 - i), 20, l, { size: 15, op: .65 }));
      ui.defaultFocus = 0;
    },
    (g, W) => { playerLists(g, 380, PAGE_H, { select: true }); playerStats(g, 404, W - 404); ui.defaultFocus = ui.pickFocus ?? 0; },   // PLAYERS: focus on the player whose statistics show
    (g, W) => {                                        // VOTE
      const lw = 360, rx = 384, rw = W - rx, vx = rw * .52, ph = PAGE_H;
      heading(g, 0, 0, "RUNNING VOTE");
      [["CALLED BY Kaz", .75, 0, "#fff"], ["KICK PLAYER Bolt", .92, 4, "#fff"], ["TIME LEFT 0:24", 1, 6, VALUE], ["5 YES VOTES, 2 NO VOTES", .8, 2, "#fff"]]
        .reduce((y, [s, op, gap, fill]) => { T(g, 0, y + gap, 18, s, { size: 15, op, fill }); return y + gap + 18; }, 24);
      const bt = 24 + 22 + 18 * 9;
      plate(g, 0, bt, lw, "VOTE YES", { primary: true, keycap: "F1", act: () => say("You voted yes.") });
      plate(g, 0, bt + 62, lw, "VOTE NO", { keycap: "F2", act: () => say("You voted no.") });
      heading(g, rx, 0, "NEW VOTE");
      const rows = [["Map", "choice", "OVER THE EDGE"], ["Gametype", "choice", MODE[mode].toUpperCase()], ["Time Limit", "slider", [15, 0, 60, 1, "15"]],
        mode === "team" ? ["Capture Limit", "slider", [5, 1, 50, 1, "5"]] : ["Frag Limit", "slider", [30, 0, 100, 1, "30"]],
        ...(mode === "team" ? [["Auto Balance Teams", "toggle", true], ["Shuffle Teams", "toggle", false]] : []),
        ["Restart map", "toggle", false], ["Buying", "choice", "OFF"], ["Kick player", "choice", "NO ONE"]];
      rows.forEach(([l, k, v], i) => row(g, rx, 24 + 26 * i, rw, l, vx, k, v, { act: () => say(`${l} drafted.`) }));
      plate(g, rx, ph - 62 + 4, Math.min(rw, 640), "CALL VOTE", { reason: "There is already a vote in progress" });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // MATCH: Match Control's six sections as an inner strip
      const secs = ["Status", "Teams", "Proposals", "Rules", "Series", "Evidence"];
      let x = 0;
      secs.forEach((s, i) => { const w = measure(s, 16) + 20, sg = svgEl("g", {}, g), on = i === matchSec;
        const lab = T(sg, x + 10, 0, 26, s, { size: 16, op: .7 }); if (on) svgEl("rect", { x, y: 23, width: w, height: 3, fill: ORANGE }, sg);
        svgEl("rect", { x, y: 0, width: w, height: 26, fill: "transparent" }, sg);
        control(f => { lab.setAttribute("fill", f || on ? ORANGE : "#fff"); lab.setAttribute("fill-opacity", f || on ? 1 : .7); }, () => { matchSec = i; fresh = true; build(false); say(`${s} section.`); }, sg);
        x += w + 4; });
      if (dev === "pad") ["LT", "RT"].forEach((k, i) => { const kx = W - 48 + i * 26; svgEl("rect", { x: kx, y: 6, width: 22, height: 14, rx: 3, fill: "#fff", "fill-opacity": .85 }, g); T(g, kx + 11, 6, 14, k, { size: 9.5, fill: DARK, anchor: "middle" }); });
      svgEl("rect", { x: 0, y: 26, width: W, height: 1, fill: OLIVE, "fill-opacity": .5 }, g);
      const s = svgEl("g", { transform: "translate(0 36)" }, g), rx = 336, rw = W - rx, vx = 163;
      const act = (rows, top = 0) => rows.forEach(([l, why], i) => row(s, rx, top + i * 32, rw, l, vx, null, null, { reason: why, act: () => say(`${l} sent to Match Control.`) }));
      const NP = "Operation is not allowed in the current phase.";
      if (matchSec === 0) {
        T(s, 0, 0, 22, "Warmup", { f: "m", size: 17, op: .9 });
        ["Warmup | Round inactive", "Readiness: 9/11 | Rules are not frozen.", "Running | No pause", "You: Player | Marine | Not ready", "Timeout budget: Marine 0/2 | Strogg 0/2"]
          .forEach((l, i) => T(s, 0, 28 + i * 17, 17, l, { size: 15, op: .8, w: 316 }));
        T(s, 0, 28 + 5 * 17 + 8, 26, "Referee", { size: 15, fill: OLIVE });
        row(s, 110, 28 + 5 * 17 + 8, 206, null, 8, "field", "", { act: () => say("The referee password; Enter signs in.") });
        act([["Set ready"], ["Team ready", "Operation conflicts with the current match state."], ["Request timeout", NP], ["Technical pause", NP], ["Resume", NP], ["Force ready"], ["Forfeit", NP], ["Abort match", NP]]);
      } else {
        const L = { 1: ["TEAM AND ROSTER", [["Join Marines"], ["Join Strogg"], ["Spectate"], ["Invite", "Choose a participant first."], ["Remove", "Captains only."], ["Lock team", "Captains only."]]],
          2: ["RUNNING PROPOSALS", [["Create proposal"], ["Vote yes", "No proposal is open."], ["Vote no", "No proposal is open."], ["Abstain", "No proposal is open."], ["Cancel proposal", "No proposal is open."]]],
          3: ["COMMITTED RULES", [["Select profile"], ["Stage value"], ["Commit rules", "Nothing is staged."], ["Discard changes", "Nothing is staged."]]],
          4: ["MAP POOL", [["Stage series"], ["Start series", "Stage a series first."], ["Cancel series", "No series is running."], ["Advance map", "No series is running."], ["Ban map", NP], ["Pick map", NP]]],
          5: ["EVIDENCE STATUS", [["Refresh"]]] }[matchSec];
        heading(s, 0, 0, L[0], 320);
        const items = matchSec === 1 ? ["Kaz · Captain", "Orin · Player", "You · Player", "Pike · Substitute"] : matchSec === 3 ? ["Capture limit 5", "Time limit 15", "Overtime sudden death"] : matchSec === 4 ? ["Heartless", "Over the Edge", "Lost Fleet"] : matchSec === 5 ? ["Demo recording: on", "Statistics: recorded"] : [];
        if (!items.length) T(s, 0, 24, 20, "No proposal is open.", { size: 15, op: .55 });
        items.forEach((t, i) => { svgEl("rect", { x: 0, y: 24 + i * 22 + 1, width: 320, height: 20, fill: "#fff", "fill-opacity": .08 }, s); T(s, 8, 24 + i * 22, 22, t, { size: 14, op: .85 }); });
        act(L[1]);
      }
      ui.defaultFocus = matchSec;
    },
    (g, W) => {                                        // SETTINGS: the player's rows, then the appearance table
      const lw = 380, vx = 110, rx = 404, rw = W - rx, tx = 112, cw = (rw - tx - 6) / 2, team = mode === "team";
      heading(g, 0, 0, "PLAYER");
      row(g, 0, 24, lw, "Name", vx, "field", "Kaz"); row(g, 0, 50, lw, "Clan Tag", vx, "field", "DMP");
      row(g, 0, 76, lw, "Model", vx, "choice", "Marine Kane"); railRow(g, 102, lw, vx);
      row(g, 0, 128, lw, "Handicap", vx, "slider", [100, 1, 100, 1, "100"]);
      settingsPlates(g, 166, lw);
      heading(g, rx + tx, 0, "OPPONENTS", cw); if (team) heading(g, rx + tx + cw + 6, 0, "TEAMMATES", cw);
      [["Force Model", "Strogg Tactical", "Default"], ["Outline", "Medium", "Low"], ["Rim Light", "Low", "Off"], ["Effect Color", "Red", "Cyan"], ["Brightskin", "Off", "Off"], ["Bright Color", "Green", "White"]]
        .forEach(([l, a, b], i) => { const y = 24 + 26 * i; T(g, rx + 21, y, 26, l, { size: 15, op: .85, w: tx - 29 });
          row(g, rx + tx, y, cw, null, 8, "choice", a); if (team) row(g, rx + tx + cw + 6, y, cw, null, 8, "choice", b); });
      row(g, rx, 24 + 6 * 26 + 8, rw, "Outline Width", tx, "choice", "2.0");
      if (!team) ["Teammate appearance applies only in team", "modes."].forEach((l, i) => T(g, rx, 24 + 7 * 26 + 20 + i * 18, 18, l, { size: 14, op: .6 }));
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // VOICE: consent, push-to-talk, receive mix and a local test
      const lw = 420, vx = 150, rx = 444, rw = W - rx;
      heading(g, 0, 0, "VOICE");
      [["Microphone access", "toggle", true], ["Send Voice", "toggle", true], ["Receive Voice", "toggle", true], ["Receive Volume", "slider", [.8, 0, 1, .05, "0.80"]],
        ["Mic Input Volume", "slider", [6, 0, 10, .5, "6.0"]], ["Voice Echo", "toggle", false]].forEach(([l, k, v], i) => row(g, 0, 24 + 26 * i, lw, l, vx, k, v));
      heading(g, rx, 0, "PUSH TO TALK"); cap(g, rx, 26, "V", { glyphs: false });   // the bound key's name, as the session names it
      plate(g, rx, 56, Math.min(rw, 320), "CONTROLS", { act: () => say("Opens Controls to rebind push to talk.") });
      const mt = 24 + 26 + 64; heading(g, rx, mt, "Mic Level");
      const tr = svgEl("rect", { x: rx, y: mt + 22, width: 320, height: 10, fill: "none", stroke: OLIVE, "stroke-opacity": .6, opacity: .38 }, g); void tr;
      const fill = svgEl("rect", { x: rx, y: mt + 22, width: 0, height: 10, fill: ORANGE }, g);
      if (!reduce) A.tick?.(fill, () => fill.isConnected && fill.setAttribute("width", 320 * Math.min(1, (A.playbackPeaks?.("voice:self", 1)?.[0] || 0) * 1.4)));
      else fill.setAttribute("width", 120);
      row(g, rx, mt + 44, rw, "Local microphone test", 170, "toggle", false, { act: () => say("Speak: the meter shows your input level.") });
      T(g, rx, mt + 82, 18, "Voice chat unavailable in this session.", { size: 14, op: .85, w: rw });
      ui.defaultFocus = 0;
    },
    (g, W) => serverPage(g, W),                         // SERVER
    (g, W) => {                                        // ADMIN
      if (!admin) {
        heading(g, 0, 0, "ENTER RCON PASSWORD");
        T(g, 0, 26, 22, "openQ4 Community", { f: "m", size: 17, op: .9 }); T(g, 0, 50, 20, "203.0.113.24:28004", { size: 15, fill: VALUE });
        row(g, 0, 84, 560, "Remote Console Password", 200, "field", "••••••••", { h: 30 });
        plate(g, 0, 128, 300, "ACCEPT", { primary: true, act: () => { admin = true; fresh = true; build(false); say("Signed in as admin."); } });
        ui.defaultFocus = 0; return;
      }
      const secs = ["PLAYERS", "SERVER", "CONSOLE"]; let x = 0;
      secs.forEach((s, i) => { const w = measure(s, 16) + 20, sg = svgEl("g", {}, g), on = i === adminSec;
        const lab = T(sg, x + 10, 0, 26, s, { size: 16, op: .7 }); if (on) svgEl("rect", { x, y: 23, width: w, height: 3, fill: ORANGE }, sg);
        svgEl("rect", { x, y: 0, width: w, height: 26, fill: "transparent" }, sg);
        control(f => { lab.setAttribute("fill", f || on ? ORANGE : "#fff"); lab.setAttribute("fill-opacity", f || on ? 1 : .7); }, () => { adminSec = i; fresh = true; build(false); }, sg); x += w + 4; });
      svgEl("rect", { x: 0, y: 26, width: W, height: 1, fill: OLIVE, "fill-opacity": .5 }, g);
      const s = svgEl("g", { transform: "translate(0 36)" }, g), sh = PAGE_H - 36;
      if (adminSec === 0) {
        const lw = W * .46, rx = lw + 16, rw = W - rx;
        ["Kaz", "Orin", "Bolt", "Pike", "Vex", "Nyx", "Tal"].forEach((n, i) => { const rg = svgEl("g", {}, s), sel = n === pick, y = i * 23;
          svgEl("rect", { x: 0, y: y + 1, width: lw - 12, height: 21, fill: sel ? OLIVE : "#fff", "fill-opacity": sel ? .45 : .08 }, rg);
          const nm = T(rg, 10, y, 23, n, { size: 15, op: .9 }); if (mode === "team") T(rg, lw - 22, y, 23, i < 4 ? "Marine" : "Strogg", { size: 14, op: .6, anchor: "end" });
          control(f => nm.setAttribute("fill", f ? ORANGE : "#fff"), () => { pick = n; build(false); }, rg); });
        plate(s, rx, 0, rw, "Kick Player", { act: () => { modal = "KICK PLAYER"; build(false); } });
        plate(s, rx, 62, rw, "Ban Player", { act: () => { modal = "BAN PLAYER"; build(false); } });
        plate(s, rx, 124, rw, "Force Team Switch", { reason: mode === "team" ? null : "There are no teams in this mode.", act: () => say(`${pick} moved to the other team.`) });
        const lk = svgEl("g", {}, s); tri(lk, rx, 194 + 9, 6.75, MARKER, .4); const ll = T(lk, rx + 10.5, 194, 24, "View Ban List", { f: "m", size: 16, op: .5 });
        control(f => ll.setAttribute("fill", f ? ORANGE : "#fff"), () => say("PLAYER BAN LIST opens over the card."), lk);
      } else if (adminSec === 1) {
        const lw = W * .5 - 8, rx = lw + 16, rw = W - rx;
        [["Map", "choice", "OVER THE EDGE"], ["Gametype", "choice", MODE[mode].toUpperCase()], ["Time Limit", "slider", [15, 0, 60, 1, "15"]], ["Capture Limit", "slider", [5, 1, 50, 1, "5"]]]
          .forEach(([l, k, v], i) => row(s, 0, i * 26, lw, l, 130, k, v));
        [["Auto Balance Teams", "toggle", true], ["Shuffle Teams", "toggle", false], ["Restart map", "toggle", false], ["Buying", "choice", "OFF"]]
          .forEach(([l, k, v], i) => row(s, rx, i * 26, rw, l, 170, k, v));
        plate(s, 0, sh - 58, Math.min(W, 640), "Apply changes", { primary: true, reason: "Change a setting first.", act: () => say("Server settings applied.") });
      } else {
        const ch = sh - 40;
        svgEl("rect", { x: 0, y: 0, width: W, height: ch, fill: "#000", "fill-opacity": .5, stroke: OLIVE, "stroke-opacity": .4 }, s);
        ["] status", "map: mp/q4ctf1  players: 11/16", "] kick Bolt", "Bolt was kicked."].forEach((l, i) => T(s, 8, 6 + i * 18, 18, l, { size: 14, op: .85 }));
        row(s, 0, ch + 8, W, null, 8, "field", "]", { h: 30 });
      }
      ui.defaultFocus = adminSec;
    },
  ],
  summary: [
    (g, W) => {                                        // SUMMARY: the result, the standings beside the chat
      const team = mode === "team";
      const [res, col, detail, score] = team ? ["MARINE TEAM WINS", 0, "", "5 - 3"] : mode === "dm" ? ["Vex wins!", 3, "3RD OF 11", "30"] : ["Vex wins!", 3, "2ND OF 8", "15"];
      teamBand(g, 0, 0, W, 40, BAND[col]);
      T(g, 20, 0, 40, res, { f: "m", size: 22, op: .95 });
      let rx = W - 12; T(g, rx, 0, 40, score, { size: 24, fill: VALUE, anchor: "end" }); rx -= measure(score, 24) + 16;
      if (detail) T(g, rx, 0, 40, detail, { size: 15, op: .75, anchor: "end" });
      // standings
      const sw = 488, st = svgEl("g", { transform: "translate(0 52)" }, g), lead = team ? 12 : 44, aw = 80;   // three 22 dp icons, or ACCURACY in the widest language
      const hd = { size: 11, op: .4 };
      T(st, lead, 0, 16, "NAME", hd); T(st, sw - 4 - 50 - 8 - aw - 8 - 88, 0, 16, "CLAN", hd); T(st, sw - 4 - 50 - 8 - aw, 0, 16, "ACCURACY", hd); T(st, sw - 4, 0, 16, "SCORE", { ...hd, anchor: "end" });
      P(st, `M0 15.5H${sw}`, { stroke: "#fff", "stroke-opacity": .1 });
      const W3 = [[6, 1, 5], [1, 0, 3], [7, 6, 2], [5, 2, 1], [0, 3, -1], [6, 5, 1], [1, -1, -1], [2, 0, 7], [3, 1, 0], [6, 0, 5]];
      const panels = team ? [["MARINES", 0, "5", [["Kaz", "DMP", 18], ["You", "DMP", 16, 1], ["Orin", "", 15], ["Bolt", "", 9], ["Pike", "RSQ", 7]]], ["STROGG", 1, "3", [["Vex", "STG", 24], ["Nyx", "STG", 19], ["Tal", "", 12], ["Juno", "", 10], ["Ash", "ASH", 6]]]]
        : [[null, 3, "", [["Vex", "STG", 30], ["Nyx", "STG", 24], ["You", "DMP", 21, 1], ["Kaz", "DMP", 18], ["Orin", "", 15], ["Tal", "", 12], ["Juno", "", 10], ["Bolt", "", 9], ["Pike", "RSQ", 7], ["Ash", "ASH", 6]]]];
      const avail = 325 - 52 - 16, bandH = team ? 2 * 22 + 6 : 0, nrows = panels.reduce((a, p) => a + p[3].length, 0), rh = Math.max(14, Math.min(24, (avail - bandH) / nrows));
      let y = 16, n = 0;
      panels.forEach(([title, tint, sc, rows], k) => {
        if (title) { if (k) y += 6; teamBand(st, 0, y, sw, 22, BAND[tint], 6); T(st, 16, y, 22, title, { f: "m", size: 15, op: .95 }); T(st, sw - 4, y, 22, sc, { size: 16, fill: VALUE, anchor: "end" }); y += 22; }
        rows.forEach(([nm, clan, s, you], r) => {
          const place = !team && r < 3 ? ["#0000FF", "#FF0000", "#FFFF00"][r] : null;
          svgEl("rect", { x: 0, y: y + 1, width: sw, height: rh - 2, fill: place || "#fff", "fill-opacity": place ? .15 : you ? .29 : .08 }, st);
          if (you) tri(st, 0, y + rh / 2 - 4, 8, ORANGE);
          if (!team) T(st, 12, y, rh, `${r + 1}.`, { size: 14, op: .6 });
          T(st, lead, y, rh, nm, { size: 14, op: .85 });
          T(st, sw - 4 - 50 - 8 - aw - 8 - 88, y, rh, clan, { size: 14, op: .6 });
          W3[n % 10].forEach((wi, i) => wi >= 0 && A.icon(st, WEAP[wi][0], { x: sw - 4 - 50 - 8 - aw + i * 22, y: y + (rh - Math.min(18, rh - 4)) / 2, h: Math.min(18, rh - 4), color: WEAP[wi][1] }));
          T(st, sw - 4, y, rh, String(s), { size: 14, op: .85, anchor: "end" });
          y += rh; n++;
        });
      });
      // chat
      const cx = sw + 24, cw = W - cx, ch = 325 - 52, c = svgEl("g", { transform: `translate(${cx} 52)` }, g);
      T(c, 0, 0, 16, "CHAT HISTORY", hd); P(c, `M0 15.5H${cw}`, { stroke: "#fff", "stroke-opacity": .1 });
      const msgs = [["server", "", "Vex fragged Kaz with a railgun."], ["Kaz", 0, "rail on the bridge"], ["Orin", 0, "quad in 20, I'll take the lower route", "team"], ["Vex", 1, "gg"], ["Nyx", 1, "gg wp, rematch?"], ["You", 0, "gg, one more", "you"]];
      const lines = []; msgs.forEach(([nm, t, txt, k]) => {                       // wrap each message to the column
        const words = (nm === "server" ? txt : `${nm}: ${txt}`).split(" "), room = cw - (k === "team" ? 50 : k === "you" ? 13 : nm === "server" ? 18 : 0);
        let line = ""; const out = []; words.forEach(wd => { const t2 = line ? line + " " + wd : wd; if (measure(t2, 14) > room && line) { out.push(line); line = wd; } else line = t2; }); out.push(line);
        lines.push([nm, t, k, out]); });
      let ly = ch - 30 - 8; [...lines].reverse().forEach(([nm, t, k, out]) => { ly -= out.length * 18 + 4;
        const x0 = k === "team" ? 50 : k === "you" ? 13 : nm === "server" ? 18 : 0;
        if (k === "you") tri(c, 0, ly + 4 + 4, 8, ORANGE);
        if (k === "team") { P(c, `M0 ${ly + 5.5}h44v15h-40l-4-4z`, { fill: TEAM_TEXT[t], "fill-opacity": .85 }); T(c, 22, ly + 5.5, 15, "TEAM", { size: 10.8, fill: DARK, anchor: "middle", ls: .6 }); }
        if (nm === "server") { svgEl("circle", { cx: 6.5, cy: ly + 4 + 7.5, r: 5.2, fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.2 }, c); P(c, `M6.5 ${ly + 8.4}v1.2M6.5 ${ly + 10.8}v4`, { stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.4 }); }
        out.forEach((l, i) => { const e = svgEl("text", { x: x0, y: ly + 4 + i * 18 + 9 + 5, "font-family": F_L, "font-weight": 500, "font-size": 14 }, c);
          const body = nm === "server" ? "#fff" : k === "team" ? TEAM_TEXT[t] : NOTE, nameC = k === "team" ? TEAM_TEXT[t] : "#fff";
          if (i === 0 && nm !== "server") { const a = svgEl("tspan", { fill: nameC }, e); a.textContent = `${nm}: `; const b = svgEl("tspan", { fill: body }, e); b.textContent = l.slice(nm.length + 2); }
          else { e.setAttribute("fill", body); e.setAttribute("fill-opacity", nm === "server" ? .7 : 1); e.textContent = l; } }); });
      svgEl("rect", { x: 0, y: 20, width: cw, height: 14, fill: grad([[0, "#000", .94], [1, "#000", 0]], { x2: 0, y2: 1 }) }, c);
      // the entry field: the stock chat plate with its channel tag
      const fy = ch - 30, fg = svgEl("g", {}, c);
      P(fg, `M0 ${fy}H${cw}V${fy + 30}H12L0 ${fy + 18}Z`, { fill: grad([[0, "#fff", .07], [.7, "#fff", .07], [1, "#fff", .02]]) });
      const r1 = P(fg, `M.75 ${fy}V${fy + 18}L12 ${fy + 29.25}H${cw * .7}`, { fill: "none", stroke: CHAT_RAIL, "stroke-opacity": .8, "stroke-width": 1.5 });
      const r2 = P(fg, `M.75 ${fy}V${fy + 18}L12 ${fy + 29.25}H${cw * .7}`, { fill: "none", stroke: ORANGE, "stroke-width": 1.5 });
      const chan = typing && team && ui.teamChan ? ["TEAM", TEAM_TEXT[0]] : ["ALL", NOTE], tw = measure(chan[0], 10.8) + 12;
      P(fg, `M10 ${fy + 7.5}h${tw}v15H14l-4-4z`, { fill: chan[1], "fill-opacity": .85 }); T(fg, 10 + tw / 2, fy + 7.5, 15, chan[0], { size: 10.8, fill: DARK, anchor: "middle", ls: .6 });
      const ex = 10 + tw + 8;
      if (typing) { const typed = "rematch on Over the Edge?"; T(fg, ex, fy, 30, typed, { size: 16, fill: NOTE }); const caret = svgEl("rect", { x: ex + measure(typed, 16) + .3, y: fy + 7.5, width: 1.8, height: 15, fill: NOTE }, fg);
        if (!reduce) { const t0 = performance.now(); A.tick?.(caret, n => caret.setAttribute("opacity", ((n - t0) % 1000) < 500 ? 1 : 0)); } }
      else T(fg, ex, fy, 30, "Send a message", { size: 15, op: .38 });
      control(on => { r1.setAttribute("opacity", on ? 0 : 1); r2.setAttribute("opacity", on ? 1 : 0); }, () => { typing = !typing; ui.teamChan = false; build(false); say(typing ? "Typing: Enter sends, Tab switches the channel, Esc cancels." : "Stopped typing."); }, fg);
      ui.defaultFocus = 0;
    },
    (g, W) => { playerLists(g, 380, PAGE_H, { select: true }); playerStats(g, 404, W - 404, { plates: false, awards: pick === "You" ? ["Rail Master", "Brawler"] : ["Rail Master"] }); ui.defaultFocus = ui.pickFocus ?? 0; },  // STATISTICS
    (g, W) => {                                        // SCOREBOARD: the scoreboard's rows and columns
      const w = W - 36, flag = mode === "team", hd = { size: 11, op: .4 };
      const cols = { name: 54, clan: w - 4 - 44 - 10 - 22 - 56 * (flag ? 4 : 3) - 96, nums: w - 4 - 44 - 10 - 22 - 56 * (flag ? 4 : 3) };
      T(g, cols.name, 0, 16, "NAME", hd); T(g, cols.clan, 0, 16, "CLAN", hd);
      (flag ? ["SCORE", "KILLS", "DEATHS", "TIME"] : ["SCORE", "DEATHS", "TIME"]).forEach((k, i) => T(g, cols.nums + 56 * (i + 1), 0, 16, k, { ...hd, anchor: "end" }));
      T(g, w - 4, 0, 16, "PING", { ...hd, anchor: "end" }); P(g, `M0 15.5H${w}`, { stroke: "#fff", "stroke-opacity": .1 });
      const clip = id("clip"); svgEl("rect", { x: -2, y: 16, width: W + 4, height: PAGE_H - 16 }, svgEl("clipPath", { id: clip }, ui.defs));
      const head = g; g = svgEl("g", { "clip-path": `url(#${clip})` }, head);
      let y = 16, total = 16;
      const clans = { Kaz: "DMP", You: "DMP", Vex: "STG", Nyx: "STG", Pike: "RSQ", Ash: "ASH" };
      roster().forEach(([title, tint, score, rows], k) => { if (!rows.length) return; if (k) y += 6;
        teamBand(g, 0, y, w, 22, BAND[tint], 6); T(g, 16, y, 22, title, { f: "m", size: 15, op: .95 }); if (score) T(g, w - 4, y, 22, score, { size: 16, fill: VALUE, anchor: "end" }); y += 22;
        rows.forEach(([n, sc, ping, f], r) => {
          const me = f === "me", c = svgEl("g", { opacity: f === "dead" ? .5 : 1 }, g);
          svgEl("rect", { x: 0, y: y + 1, width: w, height: 22, fill: "#fff", "fill-opacity": me ? .29 : .08 }, c);
          if (me) tri(c, 2, y + 8, 8, ORANGE); speaker(c, 14, y + 5, n === "Reed"); friend(c, 32, y + 5, n === "Kaz");
          T(c, cols.name, y, 24, n, { size: 14, op: .85 });
          if (f === "bot") botTag(c, cols.clan, y + 4.5); else T(c, cols.clan, y, 24, clans[n] || "", { size: 14, op: .6 });
          if (tint !== 2) (flag ? [sc, Math.round(sc * .7), 12 - r, 12] : [sc, 12 - r, 12]).forEach((v, i) => T(c, cols.nums + 56 * (i + 1), y, 24, String(v), { size: 14, op: .85, anchor: "end" }));
          if (f !== "bot") { bars(c, w - 4 - 44 - 22, y + 4, ping); T(c, w - 4, y, 24, String(ping), { size: 14, op: .6, anchor: "end" }); }
          y += 24; }); });
      total = y;
      scrollbar(head, w, PAGE_H, total);
      ui.focusTab = true;
    },
  ],
  buy: [0, 1, 2, 3].map(k => (g, W) => buyPage(g, W, k)),
};

/* The buy card (retained_mp_buy.py): the catalog (buyCatalog in src/game/mp/Buying.cpp) under The Awakening's
   prices; 32 dp plates on a 36 dp pitch in a 520 dp column, the detail of the hot plate beside it. */
const CREDITS = 2400;
const CATALOG = [
  [["Shotgun", 500, "item_shotgun", "gun_shotgun", "owned"], ["Hyperblaster", 700, "item_hyperblaster", "gun_hyperblaster"], ["Grenade Launcher", 1000, "item_grenade", "gun_grenadelauncher"],
    ["Nailgun", 900, "item_nailgun", "gun_nailgun"], ["Rocket Launcher", 2000, "item_rocket", "gun_rocketlauncher"], ["Railgun", 3000, "item_railgun", "gun_railgun"],
    ["Lightning Gun", 1500, "item_lightning", "gun_lightninggun"], ["Dark Matter Gun", 5000, "item_darkmatter", "gun_darkmatter"]],
  [["Bounce Shot Mod", 8000, "item_hyperblaster", "gun_hyperblaster", "Carry the weapon first", "Hyperblaster bolts bounce off walls."], ["Firepower Mod", 5000, "item_nailgun", "gun_nailgun", "Carry the weapon first", "The nailgun fires a third faster."],
    ["Burst Fire Mod", 12000, "item_rocket", "gun_rocketlauncher", "Carry the weapon first", "Three homing rockets in quick succession, then a reload."], ["Chain Lightning Mod", 6000, "item_lightning", "gun_lightninggun", "Carry the weapon first", "Lightning arcs on to enemies near your target."]],
  [["Light Armor", 300, "item_armorshard", "item_armorshard", null, "Adds 50 armor."], ["Heavy Armor", 700, "item_armor", "item_armor", null, "Adds 100 armor."], ["Ammo Refill", 500, "ammo", "ammo", "Already full", "Adds ammunition for every weapon you carry."]],
  [["Team Ammo Regen", 10000, "item_teamammo", "item_teamammo", null, "Your team regenerates ammunition for 30 seconds."], ["Team Health Regen", 12500, "item_teamhealth", "item_teamhealth", null, "Your team regenerates health for 30 seconds."],
    ["Team Doubler", 15000, "item_teamdamage", "item_teamdamage", null, "Your team deals almost double damage for 30 seconds."], ["FC Armor Regen", 3000, "item_fcarmorregen", "item_fcarmorregen", null, "Your team's armor builds past its maximum for 30 seconds."]],
];
function buyPage(g, W, k) {
  const LW = 520, dx = LW + 28, dw = W - dx;
  P(g, `M${LW + 14.5} 0V${PAGE_H}`, { stroke: OLIVE, "stroke-opacity": .32 });
  let items = CATALOG[k];
  if (k === 3 && mode !== "team") { T(g, 0, 0, 22, "Team modes only.", { size: 15, op: .75 }); items = items.map(it => [...it.slice(0, 4), "Team modes only", it[5]]); }
  items.forEach(([name, price, icon, drawing, state, what], i) => {
    const y = i * 36 + (k === 3 && mode !== "team" ? 30 : 0), owned = state === "owned", dear = !owned && price > CREDITS, why = owned ? null : state || (dear ? "Not enough credits" : null);
    const root = svgEl("g", { opacity: why ? .6 : 1 }, g), pl = svgEl("g", {}, root);
    P(pl, `M0 ${y + 3}H${LW}V${y + 29}H12L0 ${y + 17}Z`, { fill: grad([[0, OLIVE, .49], [.33, OLIVE, .49], [1, OLIVE, 0]]) });
    P(pl, `M.75 ${y + 3}V${y + 17}L12 ${y + 28.25}H${LW}`, { fill: "none", stroke: grad([[0, OLIVE, 1], [.33, OLIVE, 1], [1, OLIVE, 0]]), "stroke-width": 1.5 });
    const inset = P(root, `M3.5 ${y + 6}V${y + 18.5}L13.5 ${y + 25.5}H${LW - 20}`, { fill: "none", stroke: ORANGE, "stroke-width": 1.2 });
    const mk = tri(root, 10, y + 9, 7, MARKER), mh = tri(root, 10, y + 9, 7, ORANGE);
    A.icon(root, icon, { x: 26, y: y + 6, h: 20, color: "#fff" });
    const lab = T(root, 60, y, 32, name, { f: "m", size: 16, op: .8 });
    const after = 60 + mw(name, 16) + 8;
    if (why) lock(root, after, y + 9); else if (owned) P(root, `M${after + 1.5} ${y + 16.5}l3.5 3.5 6.5-8`, { fill: "none", stroke: VALUE, "stroke-opacity": .85, "stroke-width": 1.8 });
    T(root, LW - 12, y, 32, String(price), { size: 17, fill: dear ? ERR : VALUE, anchor: "end" });
    const det = svgEl("g", { opacity: 0, "pointer-events": "none" }, root);
    A.icon(det, drawing, { x: dx, y: 0, h: 48, color: "#fff" });
    const words = name.split(" "), l1 = [], l2 = []; words.forEach(wd => (mw([...l1, wd].join(" "), 22) < dw * .92 && !l2.length ? l1 : l2).push(wd));
    T(det, dx, 60, 28, l1.join(" "), { f: "m", size: 22, op: .9 }); if (l2.length) T(det, dx, 88, 28, l2.join(" "), { f: "m", size: 22, op: .9 });
    T(det, dx, 122, 16, "PRICE", { size: 13, op: .45 }); T(det, dx, 138, 28, String(price), { f: "m", size: 22, fill: VALUE });
    if (why) T(det, dx, 170, 20, why, { size: 15, fill: ERR, w: dw }); else if (owned) T(det, dx, 170, 20, "Owned", { size: 15, fill: VALUE, op: .85 });
    if (what) { const out = []; let line = ""; what.split(" ").forEach(wd => { const t2 = line ? line + " " + wd : wd; if (measure(t2, 15) > dw && line) { out.push(line); line = wd; } else line = t2; }); out.push(line);
      out.slice(0, 3).forEach((l, j) => T(det, dx, 198 + j * 20, 20, l, { size: 15, op: .8 })); }
    control(on => { pl.setAttribute("opacity", on ? 1 : .28); inset.setAttribute("opacity", on ? 1 : 0); mk.setAttribute("opacity", on ? 0 : .28); mh.setAttribute("opacity", on ? 1 : 0);
      lab.setAttribute("fill", on ? ORANGE : "#fff"); lab.setAttribute("fill-opacity", on ? 1 : .8); det.setAttribute("opacity", on ? 1 : 0); },
      () => why ? shake(root, why) : say(owned ? `${name}: already owned.` : `Bought ${name}; the menu stays open.`), root);
  });
  ui.defaultFocus = 0;
}

/* ---------- The card ---------- */
let ui = {};
function drawTabs(g, w) {
  const labels = VARIANTS[variant].tabs, inner = w - 76, nat = labels.map(t => mw(t, 14) + 31);
  const extra = Math.max(0, (inner - nat.reduce((a, b) => a + b, 0)) / labels.length), base = TAB_TOP + RISE;
  P(g, `M12 ${base}H${w - 12}V${base + RISE}H12Z`, { fill: grad([[0, TABFILL, .57], [1, TABFILL, .02]], { x2: 0, y2: 1 }) });
  P(g, `M12 ${base - .75}H38M${w - 38} ${base - .75}H${w - 12}`, { stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 });
  let x = 38;
  labels.forEach((t, i) => {
    const tw = nat[i] + extra, on = i === tab, tg = svgEl("g", { style: "cursor:pointer", role: "tab", "aria-selected": on }, g);
    if (on) {
      const edge = `M${x} ${base}L${x + 3} ${base - 3}V${TAB_TOP}H${x + tw - 18}L${x + tw} ${TAB_TOP + 18}V${base}`;
      P(tg, edge + "Z", { fill: grad([[0, TABFILL, 1], [1, TABFILL, .6]], { x2: 0, y2: 1 }) });
      P(tg, edge, { fill: "none", stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 });
    } else P(tg, `M${x} ${base - .75}H${x + tw}`, { stroke: "#fff", "stroke-opacity": .75, "stroke-width": 1.5 });
    svgEl("rect", { x, y: TAB_TOP, width: tw, height: RISE, fill: "transparent" }, tg);
    const lab = T(tg, x + 8 + (tw - 31) / 2, TAB_TOP, RISE, t, { f: "m", size: 14, op: on ? 1 : .55, anchor: "middle" });
    if (on) ui.tabLabel = lab;
    tg.addEventListener("pointerenter", () => !on && lab.setAttribute("fill-opacity", .85));
    tg.addEventListener("pointerleave", () => !on && lab.setAttribute("fill-opacity", .55));
    tg.addEventListener("click", e => { e.stopPropagation(); setTab(i); });
    x += tw;
  });
  const pk = svgEl("g", { style: "cursor:pointer" }, g), nk = svgEl("g", { style: "cursor:pointer" }, g);
  cap(pk, 12, TAB_TOP + 4, "Q", { fixed: true }); cap(nk, w - 32, TAB_TOP + 4, "E", { fixed: true });
  pk.addEventListener("click", e => { e.stopPropagation(); setTab(tab - 1); }); nk.addEventListener("click", e => { e.stopPropagation(); setTab(tab + 1); });
}
function drawPrompts(g, w) {
  const y = PROMPT_Y, glyphs = variant !== "summary";       // the summary's bar keeps the keyboard's caps (prompt_entry glyphs=False)
  let x = INSET;
  const entry = (keys, verb, fn) => { const pg = svgEl("g", { style: fn ? "cursor:pointer" : "" }, g);
    keys.forEach((k, i) => { x += cap(pg, x, y, k, { glyphs }) + (i < keys.length - 1 ? 3 : 6); });
    const v = T(pg, x, y, PROMPT_H, verb, { f: "m", size: 14, op: .8 }); x += mw(verb, 14) + 18;
    if (fn) { pg.addEventListener("pointerenter", () => v.setAttribute("fill", ORANGE)); pg.addEventListener("pointerleave", () => v.setAttribute("fill", "#fff")); pg.addEventListener("click", e => { e.stopPropagation(); fn(); }); } };
  const back = { welcome: "SPECTATE", escape: "RESUME", summary: "RETURN TO GAME", buy: "RESUME" }[variant];
  if (variant === "summary" && typing) {
    entry(["ENTER"], "SEND", () => say("Sent; still typing."));
    if (mode === "team") entry(["TAB"], "CHANNEL", () => { ui.teamChan = !ui.teamChan; build(false); });
    entry(["ESCAPE"], "CANCEL", () => { typing = false; build(false); });
    return;
  }
  entry(["ESCAPE"], back, () => close(variant === "welcome" ? "Spectating. The menu key opens the card again." : "Back in the match."));
  entry(["Q", "E"], "TABS");
  entry(["ENTER"], variant === "buy" ? "BUY" : "SELECT", activate);
  const links = variant === "welcome" ? ["MAIN MENU", "LEAVE SERVER"] : variant === "escape" ? ["MAIN MENU", "DISCONNECT"] : [];
  let lx = w - INSET;
  [...links].reverse().forEach((l, i) => {
    const lw = 10.5 + mw(l, 16); lx -= lw; const lg = svgEl("g", { style: "cursor:pointer" }, g), at = lx;
    const m = tri(lg, at, y - 1.75 + 10.875 - 3, 6.75, MARKER, .4), t = T(lg, at + 10.5, y - 1.75, 25.5, l, { f: "m", size: 16, op: .5 });
    lg.addEventListener("pointerenter", () => { m.setAttribute("fill", ORANGE); m.setAttribute("fill-opacity", 1); t.setAttribute("fill", ORANGE); t.setAttribute("fill-opacity", 1); });
    lg.addEventListener("pointerleave", () => { m.setAttribute("fill", MARKER); m.setAttribute("fill-opacity", .4); t.setAttribute("fill", "#fff"); t.setAttribute("fill-opacity", .5); });
    lg.addEventListener("click", e => { e.stopPropagation(); if (i === 0) { modal = links[1]; build(false); } else close("Main menu; you are still connected and the match continues."); });
    lx -= 21;
  });
}
function header(g, w) {
  P(g, `M1.5 3H${w - 1.5}V37H20.5L1.5 18Z`, { fill: grad([[0, "#fff", .16], [.5, "#fff", .16], [1, "#fff", 0]]) });
  P(g, `M1.5 40.5H${w - 1.5}`, { stroke: RAIL_CARD, "stroke-opacity": .8 });
  tri(g, INSET, 16, 8, MARKER);
  const trail = variant === "welcome" ? [[mode === "team" ? "11/16 PLAYERS" : "9/16 PLAYERS", 15, "#fff", .8]]
    : variant === "buy" ? [["THE AWAKENING", 15, "#fff", .6], [`Credits: ${CREDITS}`, 17, VALUE, 1]]
    : [[MODE[mode], 15, "#fff", .7], [variant === "summary" ? "0:18" : "7:12", 17, VALUE, 1],
       [mode === "team" ? (variant === "summary" ? "MARINES 5 - 3 STROGG" : "MARINES 3 - 2 STROGG") : mode === "dm" ? "3rd place with 19" : "2nd place with 6", 15, "#fff", .85]];
  let x = w - INSET; [...trail].reverse().forEach(([s, size, fill, op]) => { T(g, x, 0, HEAD, s, { size, fill, op, anchor: "end" }); x -= measure(s, size) + 16; });
  const title = variant === "welcome" ? "WELCOME TO openQ4 Community" : variant === "buy" ? "PURCHASE EQUIPMENT" : MAP[mode];
  T(g, INSET + 16, 0, HEAD, title, { f: "m", size: 20, op: .9, w: x - INSET - 16 });
}

let modalAt = 0;
function drawModal() {                       // the stock modal over the soft-focused card and view (section 6)
  const mf = id("msoft"), f = svgEl("filter", { id: mf, x: "-2%", y: "-2%", width: "104%", height: "104%", "color-interpolation-filters": "sRGB" }, ui.defs);
  const mb = svgEl("feGaussianBlur", { stdDeviation: 0 }, f), ms = svgEl("feColorMatrix", { type: "saturate", values: 1 }, f);
  const under = svgEl("g", { filter: `url(#${mf})` });
  [...svg.childNodes].forEach(n => { if (n !== ui.defs) under.appendChild(n); });
  svg.appendChild(under);
  if (!modalAt) modalAt = performance.now();
  const soften = now => { const p = reduce ? 1 : Math.min(1, (now - modalAt) / 200);
    mb.setAttribute("stdDeviation", (5 * p).toFixed(2)); ms.setAttribute("values", (1 - .2 * p).toFixed(3)); if (p < 1 && mb.isConnected) requestAnimationFrame(soften); };
  soften(performance.now());
  const g = svgEl("g", {}, svg), w = 320, h = 136, x = 160, y = 155;
  const gid = id("mg"), gg = svgEl("linearGradient", { id: gid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs), e = 26.7 / (h + 53.4);
  [[0, 0], [e, .45], [.5, .9], [1 - e, .45], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#46501B", "stop-opacity": a }, gg));
  const mid = id("mm"), mk = svgEl("mask", { id: mid, maskUnits: "userSpaceOnUse", x: x - 15, y: y - 27, width: w + 30, height: h + 54 }, ui.defs);
  const hg = id("mh"), hl = svgEl("linearGradient", { id: hg, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs), fe = 10.7 / (w + 21.4);
  [[0, 0], [fe, 1], [1 - fe, 1], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#fff", "stop-opacity": a }, hl));
  svgEl("rect", { x: x - 10.7, y: y - 26.7, width: w + 21.4, height: h + 53.4, fill: `url(#${hg})` }, mk);
  svgEl("rect", { x: x - 10.7, y: y - 26.7, width: w + 21.4, height: h + 53.4, fill: `url(#${gid})`, mask: `url(#${mid})`, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
  const sd = 11.3, L = w * 7 / 512, x0 = x + L, x1 = x + w - L, y0 = y + 2.37, y1 = y + h - 3.56, sw = (x1 - x0) * .73;
  svgEl("path", { d: `M${x0} ${y0}H${x0 + 4}V${y0 + 4}H${x0 + 16}L${x0 + 16 + sd} ${y0 + sd}H${x0 + sw - sd}L${x0 + sw} ${y0}H${x1}V${y1}H${x0 + 25}L${x0} ${y1 - 25}Z`, fill: "#000", "fill-opacity": .7 }, g);
  const tt = svgEl("text", { x: x0 + 16 + sd, y: y0 + 7.1, "font-family": F_M, "font-size": 9, "letter-spacing": -.6, fill: "#fff", stroke: "#000", "stroke-opacity": .85, "stroke-width": 1.33, "stroke-linejoin": "round", "paint-order": "stroke fill" }, g);
  tt.textContent = modal;
  const body = modal === "KICK PLAYER" ? [`Kick ${pick} from the server?`] : modal === "BAN PLAYER" ? [`Ban ${pick} from the server?`] : ["Are you sure you want to disconnect from", "this multiplayer server?"];
  body.forEach((l, i) => { const t = svgEl("text", { x: x + 22, y: y + 58.3 + i * 13, "font-family": F_L, "font-weight": 500, "font-size": 9, fill: "#fff" }, g); t.textContent = l; });
  focusables = [];
  const btn = (bx, label, fn) => { const bg = svgEl("g", { style: "cursor:pointer" }, g);
    P(bg, `M${bx} ${y + h - 30}H${bx + 120}V${y + h - 12}H${bx + 8}L${bx} ${y + h - 20}Z`, { fill: OLIVE, "fill-opacity": .3 });
    const t = svgEl("text", { x: bx + 14, y: y + h - 17.5, "font-family": F_M, "font-size": 9, fill: "#fff" }, bg); t.textContent = label;
    control(on => t.setAttribute("fill", on ? ORANGE : "#fff"), fn, bg); };
  btn(x + 20, "YES", () => { const was = modal; modal = null; if (was === "KICK PLAYER" || was === "BAN PLAYER") { build(false); say(`${pick} ${was === "KICK PLAYER" ? "kicked" : "banned"}.`); } else { closed = true; build(false); say("Disconnected. The main menu would open here."); } });
  btn(x + w - 142, "NO", () => { modal = null; build(false); });
  setFocus(1);
}
function drawClosed() {
  const g = svgEl("g", { style: "cursor:pointer" }, svg);
  svgEl("path", { d: "M250 440H390V456H253L250 453Z", fill: "#000", "fill-opacity": .6 }, g);
  const t = svgEl("text", { x: 320, y: 451, "text-anchor": "middle", "font-family": F_L, "font-weight": 500, "font-size": 8, fill: "#fff", "fill-opacity": .8 }, g);
  t.textContent = variant === "escape" ? `${dev === "pad" ? "Start" : "Esc"} opens the menu` : variant === "buy" ? "The buy key opens the card" : "Card closed · Open again";
  g.addEventListener("click", () => openMenu());
}

function build(animate = true) {
  svg.innerHTML = ""; ui = { defs: svgEl("defs", {}, svg), teamChan: ui.teamChan }; focusables = [];
  const lay = hud.fit(svg), w = VARIANTS[variant].w;
  // the live view, softened while the card is up (modal.softfocus: a 5 u blur, 0.80 saturation, never dimmed)
  const fid = id("soft"), f = svgEl("filter", { id: fid, x: "-2%", y: "-2%", width: "104%", height: "104%", "color-interpolation-filters": "sRGB" }, ui.defs);
  ui.blur = svgEl("feGaussianBlur", { stdDeviation: 0 }, f); svgEl("feColorMatrix", { type: "saturate", values: .8 }, f);
  const scene = svgEl("g", {}, svg); A.levelshot?.(scene, "mp", lay);
  const softOn = soft && !closed && !closing;
  if (softOn) scene.setAttribute("filter", `url(#${fid})`);
  else if (!closed && !closing) {                // the opaque-backing fallback: a 0.45 black scrim with a 0.60 vignette top and bottom (section 13.6)
    svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: "#000", "fill-opacity": .45 }, svg);
    svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: grad([[0, "#000", .6], [.3, "#000", 0], [.7, "#000", 0], [1, "#000", .6]], { x2: 0, y2: 1 }) }, svg);
  }
  hud.regionsLayer(svg, lay);
  if (closed) { drawClosed(); return; }
  // the card in dp: 1.5 dp a unit in landscape; a view narrower than the card and its 16 dp margins shrinks the canvas
  const dpU = 720 / Math.min(lay.W, lay.H), k = Math.min(1, lay.W * dpU / Math.ceil(w + 32)), s = k / dpU;
  const card = svgEl("g", {}, svg); ui.card = card;
  const outline = (i) => { const d = i * .4142; return `M${3 + d} ${i}H${w - 12 - d}L${w - i} ${12 + d}V${H - 3 - d}L${w - 3 - d} ${H - i}H${12 + d}L${i} ${H - 12 - d}V${3 + d}Z`; };
  P(card, outline(0), { fill: "#000", "fill-opacity": .94 });
  P(card, outline(.75), { fill: "none", stroke: RAIL_CARD, "stroke-width": 1.5 });
  header(card, w);
  drawTabs(card, w);
  const content = svgEl("g", { transform: `translate(${INSET} ${PAGE_TOP})` }, card); ui.content = content;
  PAGES[variant][tab](content, w - 2 * INSET);
  drawPrompts(card, w);
  if (fresh) { focus = ui.focusTab ? -1 : ui.defaultFocus ?? 0; fresh = false; }
  if (focus >= 0) setFocus(focus); else { focusables.forEach(c => c.update(false)); ui.tabLabel?.setAttribute("fill", ORANGE); }
  const place = (rise, alpha) => { card.setAttribute("transform", `translate(${320 - w * s / 2} ${240 - H * s / 2 + rise / dpU}) scale(${s})`); card.setAttribute("opacity", alpha); };
  if (modal) drawModal(); else modalAt = 0;
  if (animate) {                                 // open: softening 250 ms, the card rises 12 dp and fades in over 150 ms; reduced: 80 ms, no rise
    const t0 = performance.now(), fade = reduce ? 80 : 150, ramp = reduce ? 80 : 250, rise = reduce ? 0 : 12;
    place(rise, 0);
    const step = n => { const t = n - t0, p = Math.min(1, t / fade), e = 1 - Math.pow(1 - p, 3), q = Math.min(1, t / ramp);
      place(rise * (1 - e), e); if (softOn) ui.blur.setAttribute("stdDeviation", (5 * q).toFixed(2));
      if (q < 1 || p < 1) requestAnimationFrame(step); };
    requestAnimationFrame(step);
  } else { place(0, 1); if (softOn) ui.blur.setAttribute("stdDeviation", 5); }
}
function setTab(i) {
  const n = VARIANTS[variant].tabs.length; tab = (i + n) % n; fresh = true; typing = false;
  build(false);
  const c = ui.content; if (c && !reduce) { c.setAttribute("opacity", 0); const t0 = performance.now(); const step = x => { const p = Math.min(1, (x - t0) / 150); c.setAttribute("opacity", p); if (p < 1) requestAnimationFrame(step); }; requestAnimationFrame(step); }
  say(`${VARIANTS[variant].tabs[tab]} tab.`);
}
function close(msg) {                            // every way out closes the card at once; the softening releases over 250 ms
  ui.card?.setAttribute("opacity", 0);
  if (reduce) { closed = true; build(false); say(msg); return; }
  closing = true; const t0 = performance.now(), from = Number(ui.blur.getAttribute("stdDeviation")) || 0;
  const step = n => { const q = Math.min(1, (n - t0) / 250); ui.blur.setAttribute("stdDeviation", (from * (1 - q)).toFixed(2));
    if (q < 1) requestAnimationFrame(step); else { closing = false; closed = true; build(false); } };
  requestAnimationFrame(step); say(msg);
}
function join(team) { close(`Joined ${team}. You spawn in the next wave.`); }
const OPEN_SAY = { welcome: "Welcome card: Auto join is focused; Esc spectates for now.", escape: "Escape card: Resume leads; the match keeps running.",
  summary: "Match summary: the chat field is focused; Esc returns to the game.", buy: "Buy card: the first item is focused; buying keeps the card open." };
function openMenu() { closed = false; modal = null; fresh = true; typing = false; build(true); say(OPEN_SAY[variant]); }

svg.addEventListener("keydown", e => {
  if (closed) { if (e.key === "Escape" || e.key === "Enter") { e.preventDefault(); openMenu(); } return; }
  const k = e.key.toLowerCase();
  if (variant === "summary" && typing) {
    if (e.key === "Escape") { e.preventDefault(); typing = false; build(false); say("Stopped typing; the entry is cleared."); }
    else if (e.key === "Tab" && mode === "team") { e.preventDefault(); ui.teamChan = !ui.teamChan; build(false); }
    else if (e.key === "Enter") { e.preventDefault(); say("Sent; still typing."); }
    return;
  }
  if (k === "q" || e.key === "ArrowLeft" || e.key === "PageUp") { e.preventDefault(); setTab(tab - 1); }
  else if (k === "e" || e.key === "ArrowRight" || e.key === "PageDown") { e.preventDefault(); setTab(tab + 1); }
  else if (e.key === "ArrowDown") { e.preventDefault(); setFocus(focus + 1); }
  else if (e.key === "ArrowUp") { e.preventDefault(); setFocus(focus - 1); }
  else if (e.key === "Enter" || e.key === " ") { e.preventDefault(); activate(); }
  else if (e.key === "Escape") { e.preventDefault(); if (modal) { modal = null; build(false); } else close(variant === "welcome" ? "Spectating. The menu key opens the card again." : "Back in the match."); }
});
const segs = (hostId, attr, fn) => document.getElementById(hostId)?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  b.parentNode.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); fn(b.dataset[attr]); });
segs("mmVariant", "v", v => { variant = v; tab = 0; focus = 0; matchSec = 0; adminSec = 0; openMenu(); });
segs("mmMode", "m", v => { mode = v; tab = Math.min(tab, VARIANTS[variant].tabs.length - 1); pick = "Vex"; openMenu(); });
segs("mmDev", "d", v => { dev = v; build(false); });
document.getElementById("mmSoft")?.addEventListener("click", e => { e.stopImmediatePropagation(); const b = e.currentTarget, on = !b.classList.contains("on"); b.classList.toggle("on", on); b.setAttribute("aria-pressed", on); soft = on; build(false); }, true);
document.getElementById("mmOpen")?.addEventListener("click", openMenu);
const ctl = document.getElementById("mmLayoutCtl"); if (ctl) hud.controls(ctl, { scale: false, classic: false });
build(true);
hud.on(() => build(false));
})();
