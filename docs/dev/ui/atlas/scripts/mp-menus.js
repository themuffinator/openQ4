
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const svg = document.getElementById("mmSvg");
if (!svg) return;
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif", F_C = "Chakra Petch, Arial Narrow, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const RAIL = "#A0A040", RAIL_D = "#CCCC51", OLIVE = "#8B964B", TABFILL = "#5A652A", MARKER = "#909A49", ORANGE = "#E38900", VALUE = "#FFBE23";
const MAR = "#6AA42B", STR = "#FF7B04", SPC = "#999999", NOTE = "#FFFF8D", ERR = "#E46D56", OK = "#B5C784", SEC = "#B8C29E";
let uid = 0; const id = p => `${p}${++uid}`;
const measure = (t, size, fam = F_L, w = 500) => A.measureText ? A.measureText(t, size, fam, w) : t.length * size * .55;
const T = (g, x, y, s, o = {}) => { const e = svgEl("text", { x, y, "font-family": F_L, "font-weight": 500, "font-size": 8.4, fill: "#fff", ...o }, g); e.textContent = s; return e; };

/* ---------- State ---------- */
const CARD = { welcome: { w: 380, h: 268 }, escape: { w: 432, h: 318 } };
const TABS = { welcome: ["JOIN", "SERVER", "PLAYERS", "SETTINGS"], escape: ["TEAM", "PLAYERS", "VOTE", "MATCH", "SETTINGS", "VOICE", "SERVER", "ADMIN"] };
const HEAD = 26.7, STRIP = 24, FOOT = 22, IN = 16;
let variant = "welcome", mode = "team", dev = "kbm", soft = true;
let fresh = true, tab = 0, focus = 0, sub = 0, adminSub = 0, admin = false, openT = 0, closing = false, closed = false, modal = null, flash = null, pickPlayer = "Vex";
const vote = { yes: 5, no: 2, mine: null };
const set = { name: "Kaz", clan: "DMP", model: 0, rail: 2, handicap: 100, force: 0, outline: 2, send: 1, recv: 1, rvol: .8, mvol: .6, echo: 0, xh: 3, map: 0, gt: 2, tl: 15, cl: 5 };
const MODELS = ["Marine · Kane", "Marine · Rhodes", "Strogg · Tactical", "Strogg · Berserker"];
const RAILS = [["Red", "#FF2A1A"], ["Orange", "#FF8A00"], ["Yellow", "#FFE21A"], ["Green", "#35E83A"], ["Cyan", "#1AE8E8"], ["Blue", "#2A6BFF"], ["Magenta", "#E82AE0"]];
const XH = ["crosshair_q3_1", "crosshair_q3_3", "crosshair_q3_5", "crosshair_machinegun", "crosshair_railgun"];
const MAPS = ["Over the Edge", "Heartless", "The Lost Fleet", "Speed Trap"], GTS = ["DM", "Tourney", "Team DM", "CTF", "Arena CTF", "Dead Zone"];
const status = document.getElementById("mmStatus");
const say = t => { if (status) status.textContent = t; };

/* Rosters: in Welcome you are still spectating; in Escape you play for the Marines. */
const roster = () => {
  const me = variant === "escape";
  const m = [["Kaz", 18, 38], ["Orin", 15, 51], ["Bolt", 9, 0, "bot"], ["Pike", 7, 88], ["Reed", 4, 140]];
  if (me) m.unshift(["You", 19, 24, "me"]); else m.push(["Sarge", 12, 61]);
  return { m, s: [["Vex", 24, 31], ["Nyx", 19, 47], ["Tal", 12, 66], ["Juno", 10, 95], ["Ash", 6, 120]], spec: me ? [["Quill", 0, 44]] : [["You", 0, 24, "me"], ["Quill", 0, 44]] };
};

/* ---------- Glyphs and small widgets ---------- */
function key(g, x, y, label) {                 // a key cap or controller glyph; returns its width
  const pad = dev === "pad";
  if (pad && /^[ABXY]$/.test(label)) { svgEl("circle", { cx: x + 5, cy: y - 2.6, r: 5, fill: "#fff", "fill-opacity": .88 }, g); T(g, x + 5, y, label, { "text-anchor": "middle", "font-weight": 700, "font-size": 6.6, fill: "#10140A" }); return 11; }
  const w = measure(label, 6, F_P, 500) + 6;
  svgEl("path", { d: `M${x} ${y - 7.4}H${x + w}V${y + 1.8}H${x + 2.4}L${x} ${y - .6}Z`, fill: "#fff", "fill-opacity": .82 }, g);
  T(g, x + w / 2, y - .2, label, { "text-anchor": "middle", "font-family": F_P, "font-size": 6, fill: "#10140A" });
  return w;
}
const K = { esc: () => dev === "pad" ? "B" : "Esc", enter: () => dev === "pad" ? "A" : "Enter", prev: () => dev === "pad" ? "LB" : "Q", next: () => dev === "pad" ? "RB" : "E", alt: () => dev === "pad" ? "X" : "F" };
function lock(g, x, y, col = "#fff") { svgEl("path", { d: `M${x + 1.3} ${y - 3}v-1.6a2.2 2.2 0 0 1 4.4 0v1.6`, fill: "none", stroke: col, "stroke-width": .9 }, g); svgEl("rect", { x, y: y - 3.2, width: 7, height: 5.2, rx: .6, fill: col }, g); }
function toggle2(g, x, y, on, onClick) {        // No / Yes pair
  ["No", "Yes"].forEach((t, i) => { const bx = x + i * 28, sel = on === i;
    const r = svgEl("path", { d: `M${bx} ${y - 8}H${bx + 26}V${y + 2}H${bx + 3}L${bx} ${y - 1}Z`, fill: sel ? OLIVE : "#fff", "fill-opacity": sel ? .55 : .06, style: "cursor:pointer" }, g);
    T(g, bx + 13.5, y - .6, t, { "text-anchor": "middle", "font-size": 7.4, "fill-opacity": sel ? 1 : .55, "pointer-events": "none" });
    r.addEventListener("click", e => { e.stopPropagation(); onClick(i); }); });
}
function stepper(g, x, y, w, value, onStep) {    // ◄ value ►
  const a = (dx, d) => { const p = svgEl("path", { d: `M${dx} ${y - 3.2}l${-d * 4} -3.4v6.8z`, fill: VALUE, "fill-opacity": .9, style: "cursor:pointer" }, g);
    const hit = svgEl("rect", { x: dx - 6 + (d > 0 ? 0 : -2), y: y - 10, width: 10, height: 13, fill: "transparent", style: "cursor:pointer" }, g);
    [p, hit].forEach(el => el.addEventListener("click", e => { e.stopPropagation(); onStep(-d); })); };
  a(x + 4, -1); a(x + w - 4, 1);
  T(g, x + w / 2, y, value, { "text-anchor": "middle", "font-size": 8, fill: VALUE });
}
function slider(g, x, y, w, v) {                 // tick ramp track and a bracket thumb (section 7 slider)
  for (let i = 0; i <= 20; i++) svgEl("rect", { x: x + i * w / 20 - .4, y: y - 2 - i * .18, width: .8, height: 2 + i * .18, fill: "#fff", "fill-opacity": i / 20 <= v ? .75 : .2 }, g);
  const tx = x + v * w; svgEl("path", { d: `M${tx - 2.5} ${y - 8}H${tx + 2.5}V${y + 2}H${tx - 2.5}`, fill: "none", stroke: VALUE, "stroke-width": 1.2 }, g);
  T(g, x + w + 8, y, `${Math.round(v * 100)}%`, { "font-size": 7.6, fill: VALUE });
}
const row = (g, y, label, w) => { svgEl("path", { d: `M0 ${y + 4.5}H${w}`, stroke: "#fff", "stroke-opacity": .07 }, g); T(g, 2, y, label, { "font-size": 8.2, "fill-opacity": .8 }); };
const heading = (g, x, y, t, col = "#fff") => T(g, x, y, t, { "font-weight": 700, "font-size": 6.8, "letter-spacing": .8, fill: col, "fill-opacity": .5 });

/* Plates: the menu plate in the card's scale. Rest olive 0.14 with a leading rail; focus 0.40 with the orange marker;
   unavailable plates stay, dimmed, with a lock and their reason. */
let focusables = [];
function plate(g, x, y, w, h, label, { sub: subText, primary, danger, disabled, reason, act } = {}) {
  const c = Math.min(6, h * .3), idx = focusables.length;
  const root = svgEl("g", { style: "cursor:pointer" }, g);
  const gid = id("pl"), lg = svgEl("linearGradient", { id: gid, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
  const stops = [0, .33, 1].map(o => svgEl("stop", { offset: o }, lg));
  const body = svgEl("path", { d: `M${x} ${y}H${x + w}V${y + h}H${x + c}L${x} ${y + h - c}Z`, fill: `url(#${gid})` }, root);
  const rail = svgEl("path", { d: `M${x + .5} ${y}V${y + h - c}L${x + c} ${y + h - .5}H${x + w}`, fill: "none", stroke: danger ? ERR : RAIL, "stroke-width": 1 }, root);
  const mk = svgEl("path", { d: `M${x - 5} ${y + h / 2 - 3.2}l4 3.2-4 3.2z`, fill: ORANGE }, root);
  const lab = T(root, x + 10, y + (subText || reason ? h / 2 - 1 : h / 2 + 2.6), label, { "font-family": F_M, "font-weight": 400, "font-size": 6.6, "letter-spacing": -.1 });
  let subEl = null;
  if (subText || reason) subEl = T(root, x + 10, y + h / 2 + 8, disabled ? reason : subText, { "font-size": 7.2, fill: disabled ? ERR : "#fff" });
  if (disabled) lock(root, x + 16 + measure(label, 6.6, F_M, 400), y + (subText || reason ? h / 2 - 1 : h / 2 + 2.6) - .4, "#fff");
  const p = { root, idx, act, disabled, update(on) {
    const col = danger && on ? "#8a3020" : OLIVE, a = .49 * (disabled ? .12 : on ? 1 : primary ? .62 : .28);
    stops.forEach((s, k) => { s.setAttribute("stop-color", col); s.setAttribute("stop-opacity", k < 2 ? a : 0); });
    rail.setAttribute("stroke-opacity", disabled ? .25 : on ? 1 : .6);
    mk.setAttribute("opacity", on ? 1 : 0);
    lab.setAttribute("fill-opacity", disabled ? .38 : on ? 1 : .82);
    subEl?.setAttribute("fill-opacity", disabled ? .85 : on ? .75 : .5);
    if (disabled) root.querySelector("rect")?.setAttribute("fill-opacity", .5);
  } };
  root.addEventListener("pointerenter", () => setFocus(idx));
  root.addEventListener("click", e => { e.stopPropagation(); setFocus(idx); activate(); });
  focusables.push(p); p.update(false);
  return p;
}
function setFocus(i) { focus = Math.max(0, Math.min(focusables.length - 1, i)); focusables.forEach((p, k) => p.update(k === focus)); }
function activate() {
  const p = focusables[focus]; if (!p) return;
  if (p.disabled) { flash = p; say(`Unavailable: ${p.root.querySelectorAll("text")[1]?.textContent || "this action is not allowed now"}.`);
    if (!reduce) { const t0 = performance.now(); const step = n => { const q = Math.min(1, (n - t0) / 300); p.root.setAttribute("transform", `translate(${Math.sin(q * Math.PI * 5) * 2 * (1 - q)} 0)`); if (q < 1) requestAnimationFrame(step); }; requestAnimationFrame(step); }
    return; }
  p.act?.();
}

/* ---------- Pages ---------- */
function teamCard(g, x, y, w, h, team, list, score, flag, opts) {
  const col = team === "m" ? MAR : STR, name = team === "m" ? "MARINE" : "STROGG";
  const p = plate(g, x, y + 16, w, h - 16, `JOIN ${name}`, opts);
  svgEl("path", { d: `M${x + 6} ${y}H${x + w}V${y + 15}H${x}V${y + 6}Z`, fill: col, "fill-opacity": opts.disabled ? .4 : .9 }, g);
  A.icon(g, team === "m" ? "marinelogo" : "strogglogo", { x: x + 5, y: y + 2.5, h: 10, color: "#fff" });
  T(g, x + 19, y + 11, name, { "font-weight": 700, "font-size": 8.6 });
  T(g, x + w - 5, y + 11.5, String(score), { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 11 });
  T(g, x + w - 8, y + 29, `${list.length} players`, { "text-anchor": "end", "font-size": 7.4, "fill-opacity": .6 });
  T(g, x + w - 8, y + 39, flag, { "text-anchor": "end", "font-size": 7.4, fill: team === "m" ? "#AAE355" : "#FF8E00", "fill-opacity": .8 });
  return p;
}
function playerList(g, x, y, w, title, col, rows, { count = true, select } = {}) {
  const top = title ? 14 : 0;
  if (title) {
    svgEl("path", { d: `M${x + 5} ${y}H${x + w}V${y + 12}H${x}V${y + 5}Z`, fill: col, "fill-opacity": .85 }, g);
    T(g, x + 6, y + 9.2, title, { "font-weight": 700, "font-size": 7.4, "letter-spacing": .5 });
    if (count) T(g, x + w - 4, y + 9.2, `${rows.length}`, { "text-anchor": "end", "font-weight": 700, "font-size": 7.4 });
  }
  rows.forEach(([n, sc, ping, flag], i) => {
    const ry = y + top + i * 12.5, me = flag === "me", sel = select && n === pickPlayer;
    const band = svgEl("rect", { x, y: ry, width: w, height: 11.5, fill: sel ? ORANGE : "#9A9C6E", "fill-opacity": sel ? .28 : me ? .29 : .08 }, g);
    if (select) { band.style.cursor = "pointer"; band.addEventListener("click", e => { e.stopPropagation(); pickPlayer = n; build(false); }); }
    if (me) svgEl("path", { d: `M${x - 6} ${ry + 3}h4.5v4.5z`, fill: ORANGE }, g);
    A.icon(g, "icon_speaker", { x: x + 2, y: ry + 2, h: 7.5, color: "#fff", opacity: .55 });
    T(g, x + 13, ry + 8.4, n, { "font-size": 8, "pointer-events": "none" });
    if (flag === "bot") { svgEl("path", { d: `M${x + 13 + measure(n, 8) + 4} ${ry + 2}h15v8h-12l-3-3z`, fill: OLIVE, "fill-opacity": .6 }, g); T(g, x + 13 + measure(n, 8) + 11.5, ry + 8, "BOT", { "text-anchor": "middle", "font-weight": 700, "font-size": 5.4 }); }
    T(g, x + w - 30, ry + 8.4, String(sc), { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 8.4, "pointer-events": "none" });
    T(g, x + w - 3, ry + 8.4, flag === "bot" ? "—" : String(ping), { "text-anchor": "end", "font-size": 7.4, "fill-opacity": .55, "pointer-events": "none" });
  });
  return y + top + rows.length * 12.5;
}
function serverPage(g, W, H) {
  T(g, 0, 7, "OPENQ4 COMMUNITY · CTF", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
  T(g, W, 7, "203.0.113.24:28004", { "text-anchor": "end", "font-family": F_P, "font-size": 7, fill: SEC });
  svgEl("path", { d: `M0 14H${W}V40H0Z`, fill: NOTE, "fill-opacity": .05 }, g);
  svgEl("path", { d: "M.5 14V40", stroke: NOTE, "stroke-opacity": .5 }, g);
  T(g, 7, 24.5, "Welcome to the openQ4 community server. Play fair, call your", { "font-style": "italic", "font-size": 8, fill: NOTE, "fill-opacity": .85 });
  T(g, 7, 35, "flag runs, have fun. Match nights: Fridays 20:00 UTC.", { "font-style": "italic", "font-size": 8, fill: NOTE, "fill-opacity": .85 });
  const rules = mode === "dm" ? [["Mode", "Deathmatch"], ["Map", "Over the Edge"], ["Frag limit", "30"], ["Time limit", "15 min"], ["Players", "9 of 16"], ["Warm-up", "30 s"], ["Allow buying", "No"], ["Next map", "Speed Trap"]]
    : mode === "tourney" ? [["Mode", "Tourney"], ["Map", "Q4TOURNEY1"], ["Frag limit", "15"], ["Round limit", "4"], ["Players", "8 of 16"], ["Arenas", "4"], ["Allow buying", "No"], ["Next map", "Q4TOURNEY6"]]
    : [["Mode", "Capture the Flag"], ["Map", "Heartless"], ["Capture limit", "5"], ["Time limit", "15 min"], ["Players", "11 of 16"], ["Friendly fire", "Off"], ["Auto balance", "On"], ["Next map", "Over the Edge"]];
  const cw = (W - 12) / 2;
  rules.forEach(([k, v], i) => { const cx = (i % 2) * (cw + 12), cy = 56 + Math.floor(i / 2) * 15;
    svgEl("path", { d: `M${cx} ${cy + 4.5}H${cx + cw}`, stroke: "#fff", "stroke-opacity": .07 }, g);
    T(g, cx + 2, cy, k, { "font-size": 8, "fill-opacity": .5 }); T(g, cx + cw - 2, cy, v, { "text-anchor": "end", "font-size": 8.2 }); });
  heading(g, 0, 124, "ROTATION");
  (mode === "dm" ? ["Over the Edge", "Speed Trap", "The Lost Fleet"] : mode === "tourney" ? ["Q4TOURNEY1", "Q4TOURNEY6"] : ["Heartless", "Over the Edge", "Death Before Dishonor"]).forEach((m, i) => {
    const x = i * (W / 3); if (i === 0) svgEl("path", { d: `M${x} 128H${x + W / 3 - 6}V140H${x + 3}L${x} 137Z`, fill: OLIVE, "fill-opacity": .35 }, g);
    T(g, x + 5, 137, `${i + 1}. ${m}`, { "font-size": 7.6, "fill-opacity": i ? .6 : 1 }); });
}
function settingsRows(g, W, rows, y0 = 6) {
  rows.forEach(([label, draw], i) => { const y = y0 + i * 15.5; row(g, y, label, W); draw(g, W * .46, y); });
}
const PAGES = {
  welcome: [
    (g, W) => {                                        // JOIN
      if (mode === "team") {
        T(g, 0, 7, "CAPTURE THE FLAG · HEARTLESS", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
        const i = T(g, 0, 18, "", { "font-size": 8 }); [["In progress · ", .6], ["7:12 left", 1, VALUE], [" · capture limit 5 · 11 of 16 players", .6]].forEach(([s, a, c]) => { const t = svgEl("tspan", { "fill-opacity": a, fill: c || "#fff" }, i); t.textContent = s; });
        const r = roster(), cw = (W - 8) / 2;
        teamCard(g, 0, 25, cw, 56, "m", r.m, 3, "Flag at base", { disabled: true, reason: "Team full · 6 v 5" });
        teamCard(g, cw + 8, 25, cw, 56, "s", r.s, 2, "Flag taken by Kaz", { act: () => join("Strogg") });
        plate(g, 0, 90, W, 26, "AUTO JOIN", { primary: true, sub: "Puts you on Strogg, the smaller team", act: () => join("Strogg") });
        plate(g, 0, 122, W, 20, "SPECTATE", { act: () => close("Spectating. Open the menu to join a team.") });
        T(g, 0, 156, "Joining puts you in the next respawn wave.", { "font-size": 7.4, "fill-opacity": .45 });
        ui.defaultFocus = 2;
      } else if (mode === "dm") {
        T(g, 0, 7, "DEATHMATCH · OVER THE EDGE", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
        T(g, 0, 18, "In progress · 7:12 left · frag limit 30 · 9 of 16 players", { "font-size": 8, "fill-opacity": .6 });
        svgEl("path", { d: `M0 28H${W}V56H0Z`, fill: "#fff", "fill-opacity": .04 }, g);
        heading(g, 8, 38, "LEADERS"); [["1ST", "Vex", 24], ["2ND", "Nyx", 19], ["3RD", "Kaz", 18]].forEach(([o, n, s], k) => { const x = 8 + k * (W / 3); T(g, x, 51, o, { "font-weight": 700, "font-size": 7, fill: VALUE }); T(g, x + 18, 51, n, { "font-size": 8.4 }); T(g, x + W / 3 - 16, 51, String(s), { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 9 }); });
        plate(g, 0, 66, W, 30, "JOIN GAME", { primary: true, sub: "You start at 0 frags in the next respawn", act: () => join("the match") });
        plate(g, 0, 102, W, 20, "SPECTATE", { act: () => close("Spectating. Open the menu to join.") });
        ui.defaultFocus = 0;
      } else {
        T(g, 0, 7, "TOURNEY · Q4TOURNEY1", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
        T(g, 0, 18, "Round 2 of 4 · two arenas playing · frag limit 15", { "font-size": 8, "fill-opacity": .6 });
        [["ARENA 1", "Vex 9 – 6 Kaz", "4:10"], ["ARENA 2", "Nyx 3 – 3 Tal", "6:02"]].forEach(([a, m, t], k) => { const x = k * (W / 2 + 4), w = W / 2 - 4;
          svgEl("path", { d: `M${x} 28H${x + w}V52H${x + 3}L${x} 49Z`, fill: "#fff", "fill-opacity": .05 }, g);
          T(g, x + 6, 37, a, { "font-weight": 700, "font-size": 6.8, "letter-spacing": .6, "fill-opacity": .5 }); T(g, x + 6, 48, m, { "font-size": 8.4 }); T(g, x + w - 5, 48, t, { "text-anchor": "end", "font-size": 8, fill: VALUE }); });
        plate(g, 0, 62, W, 30, "JOIN THE QUEUE", { primary: true, sub: "You would be third in line for round 3", act: () => join("the Tourney queue") });
        plate(g, 0, 98, W / 2 - 4, 20, "WATCH ARENA 1", { act: () => close("Following arena 1.") });
        plate(g, W / 2 + 4, 98, W / 2 - 4, 20, "WATCH ARENA 2", { act: () => close("Following arena 2.") });
        ui.defaultFocus = 0;
      }
    },
    (g, W) => { serverPage(g, W); ui.defaultFocus = 0; },       // SERVER
    (g, W) => {                                        // PLAYERS
      const r = roster(), cw = (W - 10) / 2;
      if (mode === "team") { playerList(g, 0, 0, cw, "MARINE", MAR, r.m); playerList(g, cw + 10, 0, cw, "STROGG", STR, r.s); playerList(g, 0, 94, W, "SPECTATORS", SPC, r.spec); }
      else { const all = [...r.m, ...r.s].sort((a, b) => b[1] - a[1]); playerList(g, 0, 0, cw, "PLAYERS", OLIVE, all.slice(0, 6)); playerList(g, cw + 10, 14, cw, "", OLIVE, all.slice(6), { count: false }); playerList(g, 0, 94, W, "SPECTATORS", SPC, r.spec); }
      T(g, 0, 146, `Hover a player and press ${K.alt()} to mute or befriend them.`, { "font-size": 7.4, "fill-opacity": .45 });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // SETTINGS
      settingsRows(g, W, [
        ["Name", (g2, x, y) => { svgEl("path", { d: `M${x} ${y - 8}H${W}V${y + 3}H${x}Z`, fill: "#fff", "fill-opacity": .06, stroke: RAIL, "stroke-opacity": .4 }, g2); T(g2, x + 4, y, set.name, { "font-size": 8.2 }); svgEl("rect", { x: x + 5 + measure(set.name, 8.2), y: y - 7, width: .9, height: 8, fill: "#fff" }, g2); }],
        ["Clan tag", (g2, x, y) => T(g2, x + 4, y, set.clan, { "font-size": 8.2, fill: VALUE })],
        ["Model", (g2, x, y) => stepper(g2, x, y, W - x, MODELS[set.model], d => { set.model = (set.model + d + MODELS.length) % MODELS.length; build(false); })],
        ["Rail color", (g2, x, y) => RAILS.forEach(([n, c], i) => { const r = svgEl("path", { d: `M${x + i * 15} ${y - 8}h12v10h-9l-3-3z`, fill: c, stroke: i === set.rail ? "#fff" : "none", "stroke-width": 1.2, style: "cursor:pointer" }, g2);
          r.addEventListener("click", e => { e.stopPropagation(); set.rail = i; build(false); say(`Rail color: ${n}.`); }); })],
        ["Crosshair", (g2, x, y) => { stepper(g2, x, y, W - x, "", d => { set.xh = (set.xh + d + XH.length) % XH.length; build(false); }); const s = svgEl("svg", { x: x + (W - x) / 2 - 7, y: y - 10.5, width: 14, height: 14, viewBox: "0 0 64 64", overflow: "visible" }, g2); s.innerHTML = A.uniqueIds ? A.uniqueIds(A.CROSSHAIRS?.[XH[set.xh]]?.svg || "") : ""; }],
      ]);
      plate(g, 0, 90, W, 20, "ALL SETTINGS", { sub: null, act: () => say("Opens the full Settings screen; the match keeps running.") });
      ui.defaultFocus = 0;
    },
  ],
  escape: [
    (g, W) => {                                        // TEAM
      if (mode === "team") {
        svgEl("path", { d: `M6 0H${W}V20H0V6Z`, fill: MAR, "fill-opacity": .85 }, g);
        A.icon(g, "marinelogo", { x: 6, y: 4, h: 12, color: "#fff" });
        T(g, 24, 13.6, "YOU PLAY FOR MARINE", { "font-weight": 700, "font-size": 8.6 });
        T(g, W - 6, 14, "3 – 2 · 6 v 5", { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 10 });
        plate(g, 0, 28, W / 2 - 4, 28, "SWITCH TO STROGG", { sub: "Teams would be 5 v 6", act: () => close("Switched to Strogg; you respawn with them.") });
        plate(g, W / 2 + 4, 28, W / 2 - 4, 28, "SPECTATE", { act: () => close("Spectating.") });
        plate(g, 0, 62, W, 22, "READY", { disabled: true, reason: "Ready status is for the warm-up" });
      } else {
        T(g, 0, 7, mode === "dm" ? "YOU ARE PLAYING · 19 FRAGS · 3RD" : "YOU ARE IN ARENA 2 · 3 – 3", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
        plate(g, 0, 16, W, 28, "SPECTATE", { sub: mode === "dm" ? "Your score is kept if you rejoin" : "You forfeit the round in progress", act: () => close("Spectating.") });
        plate(g, 0, 50, W, 22, "READY", { disabled: true, reason: "Ready status is for the warm-up" });
      }
      heading(g, 0, 104, "RECENT CHAT");
      [["Kaz", "rail on the bridge", "#fff"], ["Orin", "quad in 20, I'll take the lower route", "#AAE355"], ["Vex", "gg", "#FF8E00"]].forEach(([n, t, c], i) => {
        const y = 116 + i * 12; svgEl("rect", { x: 0, y: y - 8.5, width: W, height: 11, fill: "#000", "fill-opacity": .35 }, g);
        const e = T(g, 4, y, "", { "font-size": 8 }); const a = svgEl("tspan", { fill: c === "#fff" ? NOTE : c }, e); a.textContent = `${n}: `; const b = svgEl("tspan", { fill: c === "#fff" ? NOTE : c, "fill-opacity": .85 }, e); b.textContent = t; });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // PLAYERS: roster and the selected player's statistics
      const r = roster(), lw = 214;
      if (mode === "team") { let y = playerList(g, 0, 0, lw, "MARINE", MAR, r.m, { select: true }); y = playerList(g, 0, y + 5, lw, "STROGG", STR, r.s, { select: true }); playerList(g, 0, y + 5, lw, "SPECTATORS", SPC, r.spec, { select: true }); }
      else { const all = [...r.m, ...r.s].sort((a, b) => b[1] - a[1]); const y = playerList(g, 0, 0, lw, "PLAYERS", OLIVE, all, { select: true }); playerList(g, 0, y + 5, lw, "SPECTATORS", SPC, r.spec, { select: true }); }
      const x = lw + 12, w = W - x;
      svgEl("path", { d: `M${x + 8} 0H${x + w}V174H${x}V8Z`, fill: "#fff", "fill-opacity": .04, stroke: RAIL, "stroke-opacity": .35 }, g);
      T(g, x + 8, 13, pickPlayer.toUpperCase(), { "font-family": F_M, "font-weight": 400, "font-size": 7.6 });
      [["KILLS", 24], ["DEATHS", 5], ["SCORE", 24]].forEach(([k, v], i) => { const cx = x + 8 + i * (w - 16) / 3; heading(g, cx, 28, k); T(g, cx, 40, String(v), { "font-family": F_C, "font-weight": 600, "font-size": 11 }); });
      heading(g, x + 8, 56, "ACCURACY");
      [["machinegun", "#FFFF00", .32], ["shotgun", "#FF8C00", .41], ["rocketlauncher", "#FF3300", .28], ["railgun", "#00FF00", .61], ["lightninggun", "#FFFFBA", .35]].forEach(([k, c, v], i) => {
        const y = 62 + i * 12; A.icon(g, k, { x: x + 8, y, h: 9, color: c });
        svgEl("rect", { x: x + 24, y: y + 2.5, width: (w - 58) * v, height: 4, fill: c, "fill-opacity": .75 }, g);
        svgEl("rect", { x: x + 24, y: y + 2.5, width: w - 58, height: 4, fill: "none", stroke: "#fff", "stroke-opacity": .15, "stroke-width": .5 }, g);
        T(g, x + w - 8, y + 7.4, `${Math.round(v * 100)}%`, { "text-anchor": "end", "font-size": 7.4, "fill-opacity": .8 }); });
      heading(g, x + 8, 132, "AWARDS"); T(g, x + 8, 144, "Excellent ×3 · Impressive ×2 · Assist ×1", { "font-size": 7.4, "fill-opacity": .8 });
      plate(g, x + 6, 152, w / 2 - 9, 16, "MUTE", { act: () => say(`${pickPlayer} is muted.`) });
      plate(g, x + w / 2 + 3, 152, w / 2 - 9, 16, "FRIEND", { act: () => say(`${pickPlayer} is on your friends list.`) });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // VOTE
      svgEl("path", { d: `M8 0H${W}V52H0V8Z`, fill: VALUE, "fill-opacity": .07, stroke: VALUE, "stroke-opacity": .35 }, g);
      T(g, 10, 12, "VOTE · KAZ", { "font-weight": 700, "font-size": 7, "letter-spacing": .7, fill: VALUE });
      T(g, 10, 24, "Change the map to Over the Edge (q4dm7)", { "font-size": 8.4 });
      T(g, W - 8, 12, "0:24 left", { "text-anchor": "end", "font-size": 7.6, fill: VALUE });
      const tot = 11, bx = 10, bw = W - 186;
      [["YES", vote.yes, OK], ["NO", vote.no, ERR]].forEach(([k, n, c], i) => { const y = 32 + i * 9;
        T(g, bx, y + 5.4, k, { "font-weight": 700, "font-size": 6.4, "fill-opacity": .7 }); svgEl("rect", { x: bx + 18, y, width: bw * n / tot, height: 6, fill: c, "fill-opacity": .8 }, g);
        svgEl("rect", { x: bx + 18, y, width: bw, height: 6, fill: "none", stroke: "#fff", "stroke-opacity": .15, "stroke-width": .5 }, g); T(g, bx + 24 + bw, y + 5.6, String(n), { "font-size": 7.4 }); });
      if (vote.mine) T(g, W - 8, 44, `You voted ${vote.mine}`, { "text-anchor": "end", "font-size": 7.6, "fill-opacity": .7 });
      else { plate(g, W - 128, 30, 58, 18, `${dev === "pad" ? "" : "F1 · "}YES`, { act: () => { vote.mine = "YES"; vote.yes++; build(false); say("You voted yes."); } });
        plate(g, W - 64, 30, 58, 18, `${dev === "pad" ? "" : "F2 · "}NO`, { act: () => { vote.mine = "NO"; vote.no++; build(false); say("You voted no."); } }); }
      heading(g, 0, 68, "CALL A VOTE");
      settingsRows(g, W, [
        ["Map", (g2, x, y) => stepper(g2, x, y, W - x, MAPS[set.map], d => { set.map = (set.map + d + MAPS.length) % MAPS.length; build(false); })],
        ["Gametype", (g2, x, y) => stepper(g2, x, y, W - x, GTS[set.gt], d => { set.gt = (set.gt + d + GTS.length) % GTS.length; build(false); })],
        ["Time limit", (g2, x, y) => stepper(g2, x, y, W - x, `${set.tl} min`, d => { set.tl = Math.max(5, Math.min(60, set.tl + d * 5)); build(false); })],
        ["Capture limit", (g2, x, y) => stepper(g2, x, y, W - x, String(set.cl), d => { set.cl = Math.max(1, Math.min(20, set.cl + d)); build(false); })],
        ["Kick player", (g2, x, y) => stepper(g2, x, y, W - x, "Bolt", () => {})],
      ], 82);
      plate(g, 0, 160, W, 20, "CALL VOTE", { disabled: true, reason: "A vote is already running" });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // MATCH: Match Control with its own sections
      const secs = ["Status", "Teams", "Proposals", "Rules", "Series", "Evidence"]; let x = 0;
      secs.forEach((s, i) => { const w = measure(s, 7.4) + 12, on = i === sub;
        const r = svgEl("path", { d: `M${x} 0H${x + w}V12H${x + 3}L${x} 9Z`, fill: on ? OLIVE : "#fff", "fill-opacity": on ? .55 : .06, style: "cursor:pointer" }, g);
        T(g, x + w / 2, 8.8, s, { "text-anchor": "middle", "font-size": 7.4, "fill-opacity": on ? 1 : .6, "pointer-events": "none" });
        r.addEventListener("click", e => { e.stopPropagation(); sub = i; build(false); }); x += w + 3; });
      T(g, W, 9, "LIVE · ROUND 1", { "text-anchor": "end", "font-weight": 700, "font-size": 7.2, "letter-spacing": .6, fill: VALUE });
      if (sub === 0) {
        heading(g, 0, 28, "READINESS");
        [["You", "Ready", OK], ["Marine", "6 of 6 ready", OK], ["Strogg", "5 of 5 ready", OK]].forEach(([k, v, c], i) => { const y = 40 + i * 12; T(g, 2, y, k, { "font-size": 8, "fill-opacity": .6 }); T(g, 70, y, v, { "font-size": 8, fill: c }); });
        heading(g, 0, 88, "PAUSE");
        plate(g, 0, 94, W / 2 - 4, 24, "REQUEST TIMEOUT", { sub: "1 of 2 left for your team", act: () => say("Timeout requested; the match pauses at the next break.") });
        plate(g, W / 2 + 4, 94, W / 2 - 4, 24, "TECHNICAL PAUSE", { disabled: true, reason: "Captains only" });
        plate(g, 0, 124, W / 2 - 4, 20, "FORFEIT", { danger: true, disabled: true, reason: "Captains only" });
        plate(g, W / 2 + 4, 124, W / 2 - 4, 20, "REFEREE LOGIN", { act: () => say("Referee login asks for the referee password.") });
      } else {
        const notes = { 1: "Rosters, substitutes and team locks. Captains invite and remove players here.", 2: "No proposal is open. Propose a rules change or the next map; the other side accepts or declines.", 3: "Rules profile: openQ4 CTF 1.0. Capture limit 5, time limit 15, overtime sudden death.", 4: "Series: best of three, game 1. Map order: Heartless, Over the Edge, decider by veto.", 5: "Evidence: demo and statistics records for the referee. Nothing is flagged." };
        const words = notes[sub].split(" "); let line = "", y = 30; words.forEach(wd => { const t = line ? line + " " + wd : wd; if (measure(t, 8.2) > W - 4) { T(g, 2, y, line, { "font-size": 8.2, "fill-opacity": .8 }); y += 12; line = wd; } else line = t; }); T(g, 2, y, line, { "font-size": 8.2, "fill-opacity": .8 });
        plate(g, 0, 124, W, 20, "OPEN IN MATCH CONTROL", { act: () => say("Opens the full Match Control surface on this section.") });
      }
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // SETTINGS
      settingsRows(g, W, [
        ["Name", (g2, x, y) => T(g2, x + 4, y, set.name, { "font-size": 8.2, fill: VALUE })],
        ["Model", (g2, x, y) => stepper(g2, x, y, W - x, MODELS[set.model], d => { set.model = (set.model + d + MODELS.length) % MODELS.length; build(false); })],
        ["Rail color", (g2, x, y) => RAILS.forEach(([n, c], i) => { const r = svgEl("path", { d: `M${x + i * 15} ${y - 8}h12v10h-9l-3-3z`, fill: c, stroke: i === set.rail ? "#fff" : "none", "stroke-width": 1.2, style: "cursor:pointer" }, g2);
          r.addEventListener("click", e => { e.stopPropagation(); set.rail = i; build(false); }); })],
        ["Handicap", (g2, x, y) => slider(g2, x, y, W - x - 34, set.handicap / 100)],
        ["Force model", (g2, x, y) => ["Self", "Enemy", "Teammate"].forEach((s, i) => { const bx = x + i * 44, on = i === set.force;
          const r = svgEl("path", { d: `M${bx} ${y - 8}H${bx + 42}V${y + 2}H${bx + 3}L${bx} ${y - 1}Z`, fill: on ? OLIVE : "#fff", "fill-opacity": on ? .55 : .06, style: "cursor:pointer" }, g2);
          T(g2, bx + 21, y - .6, s, { "text-anchor": "middle", "font-size": 7.4, "fill-opacity": on ? 1 : .55, "pointer-events": "none" }); r.addEventListener("click", e => { e.stopPropagation(); set.force = i; build(false); }); })],
        ["Outline", (g2, x, y) => stepper(g2, x, y, W - x, ["Off", "Low", "Medium", "High"][set.outline], d => { set.outline = (set.outline + d + 4) % 4; build(false); })],
      ]);
      plate(g, 0, 100, W / 3 - 4, 20, "CONTROLS", { act: () => say("Opens Settings at Controls.") });
      plate(g, W / 3 + 2, 100, W / 3 - 4, 20, "GAME OPTIONS", { act: () => say("Opens Settings at Game options.") });
      plate(g, 2 * W / 3 + 4, 100, W / 3 - 4, 20, "SYSTEM", { act: () => say("Opens Settings at System.") });
      T(g, 0, 138, "Teammate appearance applies only in team modes.", { "font-size": 7.4, "fill-opacity": .45 });
      ui.defaultFocus = 0;
    },
    (g, W) => {                                        // VOICE, with a live microphone meter
      settingsRows(g, W, [
        ["Send voice", (g2, x, y) => toggle2(g2, x, y, set.send, v => { set.send = v; build(false); })],
        ["Receive voice", (g2, x, y) => toggle2(g2, x, y, set.recv, v => { set.recv = v; build(false); })],
        ["Receive volume", (g2, x, y) => slider(g2, x, y, W - x - 34, set.rvol)],
        ["Mic input volume", (g2, x, y) => slider(g2, x, y, W - x - 34, set.mvol)],
        ["Voice echo", (g2, x, y) => toggle2(g2, x, y, set.echo, v => { set.echo = v; build(false); })],
        ["Mic level", (g2, x, y) => {
          svgEl("rect", { x, y: y - 8, width: W - x, height: 10, fill: "#fff", "fill-opacity": .05 }, g2);
          A.waveform?.(g2, { rect: [x - 6, y - 9, 70, 12], clip: `M${x + 2} ${y - 8}H${x + 44}V${y + 2}H${x + 2}Z`, color: NOTE, mode: "voice", source: "voice:self" });
          const meter = svgEl("rect", { x: x + 50, y: y - 6, width: 0, height: 6, fill: OK, "fill-opacity": .85 }, g2);
          svgEl("rect", { x: x + 50, y: y - 6, width: W - x - 56, height: 6, fill: "none", stroke: "#fff", "stroke-opacity": .2, "stroke-width": .5 }, g2);
          if (!reduce) A.tick?.(meter, now => meter.setAttribute("width", (W - x - 56) * Math.min(1, A.playbackPeaks("voice:self", 1)[0] * set.mvol * 1.4)));
        }],
      ]);
      T(g, 0, 110, `Push to talk: ${dev === "pad" ? "hold the left stick" : "hold V"}. Team voice: ${dev === "pad" ? "hold the right stick" : "hold B"}.`, { "font-size": 7.4, "fill-opacity": .5 });
      plate(g, 0, 120, W, 20, "TEST MICROPHONE", { act: () => say("Speak: the meter shows your input level and Voice echo plays it back.") });
      ui.defaultFocus = 0;
    },
    (g, W) => { serverPage(g, W); ui.defaultFocus = 0; },       // SERVER
    (g, W) => {                                        // ADMIN
      if (!admin) {
        T(g, 0, 8, "ENTER RCON PASSWORD", { "font-family": F_M, "font-weight": 400, "font-size": 7.4 });
        T(g, 0, 21, "Admin tools need the server's remote console password.", { "font-size": 8, "fill-opacity": .55 });
        svgEl("path", { d: `M0 30H${W * .6}V44H0Z`, fill: "#fff", "fill-opacity": .06, stroke: RAIL, "stroke-opacity": .5 }, g);
        T(g, 5, 40.5, "••••••••", { "font-size": 9, "letter-spacing": 1.5 }); svgEl("rect", { x: 5 + measure("••••••••", 9) + 13, y: 32.5, width: .9, height: 9, fill: "#fff" }, g);
        plate(g, W * .6 + 8, 30, W * .4 - 8, 14, "ACCEPT", { primary: true, act: () => { admin = true; build(false); say("Signed in as admin."); } });
      } else {
        const secs = ["Players", "Server", "Console"]; let x = 0;
        secs.forEach((s, i) => { const w = measure(s, 7.4) + 14, on = i === adminSub;
          const r = svgEl("path", { d: `M${x} 0H${x + w}V12H${x + 3}L${x} 9Z`, fill: on ? OLIVE : "#fff", "fill-opacity": on ? .55 : .06, style: "cursor:pointer" }, g);
          T(g, x + w / 2, 8.8, s, { "text-anchor": "middle", "font-size": 7.4, "fill-opacity": on ? 1 : .6, "pointer-events": "none" }); r.addEventListener("click", e => { e.stopPropagation(); adminSub = i; build(false); }); x += w + 3; });
        if (adminSub === 0) {
          const r = roster(); playerList(g, 0, 18, 200, "PLAYERS", OLIVE, [...r.m, ...r.s].slice(0, 9), { select: true });
          [["KICK PLAYER", "kicked"], ["BAN PLAYER", "banned"], ["FORCE TEAM SWITCH", "moved to the other team"], ["VIEW BAN LIST", null]].forEach(([l, verb], i) =>
            plate(g, 212, 18 + i * 26, W - 212, 20, l, { danger: i < 2, act: () => say(verb ? `${pickPlayer} ${verb}.` : "Opens the ban list in the stock modal.") }));
        } else if (adminSub === 1) {
          settingsRows(g, W, [["Map", (g2, x, y) => stepper(g2, x, y, W - x, MAPS[set.map], d => { set.map = (set.map + d + MAPS.length) % MAPS.length; build(false); })],
            ["Gametype", (g2, x, y) => stepper(g2, x, y, W - x, GTS[set.gt], d => { set.gt = (set.gt + d + GTS.length) % GTS.length; build(false); })],
            ["Team damage", (g2, x, y) => toggle2(g2, x, y, 0, () => {})], ["Auto balance teams", (g2, x, y) => toggle2(g2, x, y, 1, () => {})]], 26);
          plate(g, 0, 96, W, 20, "APPLY CHANGES", { primary: true, act: () => say("Server settings applied; the map restarts.") });
        } else {
          svgEl("rect", { x: 0, y: 18, width: W, height: 100, fill: "#000", "fill-opacity": .5, stroke: RAIL, "stroke-opacity": .3 }, g);
          ["] status", "map: mp/q4ctf1  players: 11/16", "] kick Bolt", "Bolt was kicked."].forEach((l, i) => T(g, 6, 32 + i * 11, l, { "font-family": F_P, "font-size": 7.2, fill: i % 2 ? SEC : "#fff" }));
          T(g, 6, 110, "]▏", { "font-family": F_P, "font-size": 7.2 });
        }
      }
      ui.defaultFocus = 0;
    },
  ],
};

/* ---------- The card ---------- */
let ui = {};
function drawTabs(g, w) {
  const labels = TABS[variant], base = HEAD + STRIP, top = base - 20;
  const kw = 18, x0 = IN + kw, avail = w - 2 * IN - 2 * kw;
  let widths = labels.map(t => measure(t, 6.2, F_M, 400) + 16);
  const total = widths.reduce((a, b) => a + b, 0) + (labels.length - 1) * 2;
  if (total > avail) widths = widths.map(v => v - (total - avail) / labels.length);
  const xs = []; let x = x0; widths.forEach(v => { xs.push(x); x += v + 2; });
  const a = xs[tab], b = xs[tab] + widths[tab];
  // wash below the baseline across the strip, fading over one tab height (stock 0.57 to 0.02)
  const wid = id("tw"), wg = svgEl("linearGradient", { id: wid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs);
  [[0, .57], [1, .02]].forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": TABFILL, "stop-opacity": v }, wg));
  svgEl("rect", { x: IN, y: base, width: w - 2 * IN, height: 20, fill: `url(#${wid})`, opacity: .6 }, g);
  // the active tab: rises 20 u with a small leading flare and a 12 u 45-degree trailing shoulder
  const fid = id("tf"), fg = svgEl("linearGradient", { id: fid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs);
  [[0, 1], [1, .6]].forEach(([o, v]) => svgEl("stop", { offset: o, "stop-color": TABFILL, "stop-opacity": v }, fg));
  const shape = `M${a - 3} ${base}Q${a} ${base} ${a} ${base - 3}V${top}H${b - 12}L${b} ${top + 12}V${base}`;
  svgEl("path", { d: shape + "Z", fill: `url(#${fid})` }, g);
  svgEl("path", { d: shape, fill: "none", stroke: RAIL_D, "stroke-opacity": .85, "stroke-width": 1 }, g);
  svgEl("path", { d: `M${IN} ${base}H${a - 3}M${b} ${base}H${w - IN}`, stroke: RAIL_D, "stroke-opacity": .85, "stroke-width": 1 }, g);
  labels.forEach((t, i) => {
    const on = i === tab, cx = xs[i] + widths[i] / 2 - (on ? 3 : 0);
    const hit = svgEl("rect", { x: xs[i], y: top - 2, width: widths[i], height: 24, fill: "transparent", style: "cursor:pointer", role: "tab", "aria-selected": on }, g);
    const lab = T(g, cx, base - 7, t, { "text-anchor": "middle", "font-family": F_M, "font-weight": 400, "font-size": 6.2, "fill-opacity": on ? 1 : .55, "pointer-events": "none" });
    hit.addEventListener("pointerenter", () => !on && lab.setAttribute("fill-opacity", .85));
    hit.addEventListener("pointerleave", () => !on && lab.setAttribute("fill-opacity", .55));
    hit.addEventListener("click", e => { e.stopPropagation(); setTab(i); });
  });
  // bumper glyphs at the strip's ends
  const pk = svgEl("g", { style: "cursor:pointer" }, g), nk = svgEl("g", { style: "cursor:pointer" }, g);
  key(pk, IN, base - 6, K.prev()); key(nk, w - IN - (dev === "pad" ? 15 : 10), base - 6, K.next());
  pk.addEventListener("click", e => { e.stopPropagation(); setTab(tab - 1); }); nk.addEventListener("click", e => { e.stopPropagation(); setTab(tab + 1); });
}
function drawFooter(g, w, h) {
  const y = h - 8;
  svgEl("path", { d: `M${IN} ${h - FOOT}H${w - IN}`, stroke: "#fff", "stroke-opacity": .12 }, g);
  let x = IN;
  const prompt = (keys, label, fn) => { const pg = svgEl("g", { style: "cursor:pointer" }, g); keys.forEach(k => { x += key(pg, x, y, k) + 2; }); T(pg, x + 1, y, label, { "font-size": 7.6, "fill-opacity": .8 }); x += measure(label, 7.6) + 12; pg.addEventListener("click", e => { e.stopPropagation(); fn(); }); };
  prompt([K.esc()], variant === "escape" ? "Resume" : "Spectate", () => close(variant === "escape" ? "Back in the match." : "Spectating. Open the menu to join."));
  prompt([K.prev(), K.next()], "Tabs", () => setTab(tab + 1));
  prompt([K.enter()], "Select", activate);
  // trailing actions: destructive last
  const acts = variant === "escape" ? [["MAIN MENU", false], ["DISCONNECT", true]] : [["LEAVE SERVER", true]];
  let tx = w - IN;
  [...acts].reverse().forEach(([label, danger]) => {
    const tw = measure(label, 6, F_M, 400) + 16; tx -= tw;
    const pg = svgEl("g", { style: "cursor:pointer" }, g);
    const body = svgEl("path", { d: `M${tx} ${y - 9}H${tx + tw}V${y + 3}H${tx + 3}L${tx} ${y}Z`, fill: danger ? ERR : OLIVE, "fill-opacity": .18 }, pg);
    T(pg, tx + tw / 2, y - .4, label, { "text-anchor": "middle", "font-family": F_M, "font-weight": 400, "font-size": 6, fill: danger ? "#F2A493" : "#fff", "fill-opacity": .85 });
    pg.addEventListener("pointerenter", () => body.setAttribute("fill-opacity", .4)); pg.addEventListener("pointerleave", () => body.setAttribute("fill-opacity", .18));
    pg.addEventListener("click", e => { e.stopPropagation(); if (danger) { modal = label; build(false); } else close("Main menu; you are still connected and the match continues."); });
    tx -= 6;
  });
}
let modalAt = 0;
function drawModal(lay) {                       // the stock modal over the soft-focused card and view: glow column, 0.70 silhouette, title slot
  // modal.enter: the card and the view beneath go into soft focus over 200 ms (a 5 u blur, 0.80 saturation, never dimmed)
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
  // the glow over the soft focus: full strength across the dialog's rectangle, whose inset around the silhouette is the
  // 6 dp (4.4 u) lit margin, fading out over a further 16 dp (10.7 u) at the sides; half strength at the rectangle's
  // edges and gone 40 dp (26.7 u) above and below
  const gid = id("mg"), gg = svgEl("linearGradient", { id: gid, x1: 0, x2: 0, y1: 0, y2: 1 }, ui.defs), e = 26.7 / (h + 53.4);
  [[0, 0], [e, .45], [.5, .9], [1 - e, .45], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#46501B", "stop-opacity": a }, gg));
  const mid = id("mm"), mk = svgEl("mask", { id: mid, maskUnits: "userSpaceOnUse", x: x - 15, y: y - 27, width: w + 30, height: h + 54 }, ui.defs);
  const hg = id("mh"), hl = svgEl("linearGradient", { id: hg, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs), fe = 10.7 / (w + 21.4);
  [[0, 0], [fe, 1], [1 - fe, 1], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#fff", "stop-opacity": a }, hl));
  svgEl("rect", { x: x - 10.7, y: y - 26.7, width: w + 21.4, height: h + 53.4, fill: `url(#${hg})` }, mk);
  svgEl("rect", { x: x - 10.7, y: y - 26.7, width: w + 21.4, height: h + 53.4, fill: `url(#${gid})`, mask: `url(#${mid})`, style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, g);
  // the silhouette, inset in its rectangle as the stock art is: 7/512 of the width at the sides, 3.56 dp (2.37 u) above
  // the raised sections and 5.34 dp (3.56 u) below the bottom edge
  const sd = 11.3, L = w * 7 / 512, x0 = x + L, x1 = x + w - L, y0 = y + 2.37, y1 = y + h - 3.56, sw = (x1 - x0) * .73;
  svgEl("path", { d: `M${x0} ${y0}H${x0 + 4}V${y0 + 4}H${x0 + 16}L${x0 + 16 + sd} ${y0 + sd}H${x0 + sw - sd}L${x0 + sw} ${y0}H${x1}V${y1}H${x0 + 25}L${x0} ${y1 - 25}Z`, fill: "#000", "fill-opacity": .7 }, g);
  // the title's capitals hang from the raised line into the lit slot; its baseline sits about 6 dp above the floor
  T(g, x0 + 16 + sd, y0 + 7.1, `${modal}?`, { "font-family": F_M, "font-weight": 400, "font-size": 9, "letter-spacing": -.6,   // the outline: 1 dp outside the strokes
    stroke: "#000", "stroke-opacity": .85, "stroke-width": 1.33, "stroke-linejoin": "round", "paint-order": "stroke fill" });
  // the first body baseline 87.5 dp (58.3 u) below the rectangle's top; the actions at its foot, about 16 dp above the bottom
  T(g, x + 22, y + 58.3, modal === "DISCONNECT" ? "Leave this match and return to the main menu?" : "Leave this server and return to the main menu?", { "font-size": 9 });
  T(g, x + 22, y + 72.3, "The match keeps going without you.", { "font-size": 9, "fill-opacity": .6 });
  const save = focusables; focusables = [];
  plate(g, x + 20, y + h - 32.7, 120, 22, "YES", { primary: true, act: () => { modal = null; closed = true; build(false); say("Disconnected. The main menu would open here."); } });
  plate(g, x + w - 142, y + h - 32.7, 120, 22, "NO", { act: () => { modal = null; build(false); } });
  setFocus(1); ui.modalFocus = true; void save;
}
function drawClosed(lay) {
  const g = svgEl("g", { style: "cursor:pointer" }, svg);
  svgEl("path", { d: "M250 440H390V456H253L250 453Z", fill: "#000", "fill-opacity": .6 }, g);
  T(g, 320, 451, variant === "escape" ? `${K.esc()} opens the menu` : "Menu closed · Open again", { "text-anchor": "middle", "font-size": 8, "fill-opacity": .8 });
  g.addEventListener("click", () => openMenu());
}

function build(animate = true) {
  svg.innerHTML = ""; ui = { defs: svgEl("defs", {}, svg) }; focusables = [];
  const lay = hud.fit(svg), { w, h } = CARD[variant];
  // the live view, softened while the menu is up (section 9): blur, a touch less saturation, no dimmer
  const fid = id("soft"), f = svgEl("filter", { id: fid, x: "-2%", y: "-2%", width: "104%", height: "104%", "color-interpolation-filters": "sRGB" }, ui.defs);
  ui.blur = svgEl("feGaussianBlur", { stdDeviation: 0 }, f); svgEl("feColorMatrix", { type: "saturate", values: .8 }, f);
  const scene = svgEl("g", {}, svg); A.levelshot?.(scene, "mp", lay);
  const softOn = soft && !closed && !closing;
  if (softOn) scene.setAttribute("filter", `url(#${fid})`);
  ui.scene = scene;
  hud.regionsLayer(svg, lay);
  if (closed) { drawClosed(lay); ui.blur.setAttribute("stdDeviation", 0); return; }
  const card = svgEl("g", {}, svg); ui.card = card;
  const outline = `M2 0H${w - 8}L${w} 8V${h - 2}L${w - 2} ${h}H8L0 ${h - 8}V2Z`;
  svgEl("path", { d: outline, fill: "#000", "fill-opacity": .94 }, card);
  const hw = id("hw"), hg = svgEl("linearGradient", { id: hw, x1: 0, x2: 1, y1: 0, y2: 0 }, ui.defs);
  [[0, .08], [.5, .08], [1, 0]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": "#fff", "stop-opacity": a }, hg));
  svgEl("path", { d: `M2.5 .5H${w - 8.2}L${w - .5} 7.8V${HEAD}H.5V2.5Z`, fill: `url(#${hw})` }, card);
  svgEl("path", { d: outline, fill: "none", stroke: RAIL, "stroke-width": 1 }, card);
  svgEl("path", { d: `M0 ${HEAD}H${w}`, stroke: RAIL, "stroke-opacity": .5, "stroke-width": .67 }, card);
  svgEl("path", { d: `M10 ${HEAD / 2 - 3.5}l5.3 3.5-5.3 3.5z`, fill: MARKER }, card);
  const title = variant === "welcome" ? "WELCOME TO OPENQ4 COMMUNITY" : mode === "team" ? "HEARTLESS" : mode === "dm" ? "OVER THE EDGE" : "Q4TOURNEY1";
  T(card, 20, HEAD / 2 + 3.3, title, { "font-family": F_M, "font-weight": 400, "font-size": 8, "letter-spacing": -.2, "fill-opacity": .9 });
  if (variant === "escape") {                     // match status in the header: mode, clock and score, never pausing
    const info = mode === "team" ? [["CTF", "#fff"], ["7:12", VALUE], ["3 – 2", "#fff"]] : mode === "dm" ? [["DM", "#fff"], ["7:12", VALUE], ["3RD · 19", "#fff"]] : [["TOURNEY", "#fff"], ["ROUND 2", VALUE]];
    let ix = w - 12; [...info].reverse().forEach(([s, c]) => { const e = T(card, ix, HEAD / 2 + 3, s, { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 9, fill: c }); ix -= measure(s, 9, F_C, 600) + 10; void e; });
  } else T(card, w - 12, HEAD / 2 + 3, mode === "team" ? "11 / 16" : mode === "dm" ? "9 / 16" : "8 / 16", { "text-anchor": "end", "font-family": F_C, "font-weight": 600, "font-size": 9, fill: SEC });
  drawTabs(card, w);
  const cy = HEAD + STRIP + 10, content = svgEl("g", { transform: `translate(${IN} ${cy})` }, card);
  PAGES[variant][tab](content, w - 2 * IN, h - cy - FOOT - 8);
  drawFooter(card, w, h);
  if (fresh) { focus = ui.defaultFocus ?? 0; fresh = false; }
  setFocus(focus);
  // portrait: the card keeps its dp size. A unit is 1.125 dp there (720 dp across 640 u) instead of 1.5 dp, so the
  // card scales by 4/3, capped at the width less 16 dp (14.2 u) each side
  const k = lay.a < 1 ? Math.min(4 / 3, (640 - 2 * 16 / 1.125) / w) : 1;
  const place = (rise, alpha) => { card.setAttribute("transform", `translate(${320 - w * k / 2} ${240 - h * k / 2 + rise}) scale(${k})`); card.setAttribute("opacity", alpha); };
  if (modal) { drawModal(lay); } else modalAt = 0;
  const now = performance.now();
  if (animate && !reduce) {
    openT = now; place(8, 0); content.setAttribute("opacity", 0);
    const step = n => { const t = n - openT, p = Math.min(1, t / 150), q = Math.min(1, t / 250);
      place(8 * (1 - p), p); content.setAttribute("opacity", Math.min(1, Math.max(0, (t - 60) / 90)));
      if (softOn) ui.blur.setAttribute("stdDeviation", (5 * q).toFixed(2));
      if (q < 1) requestAnimationFrame(step); };
    requestAnimationFrame(step);
  } else { place(0, 1); if (softOn) ui.blur.setAttribute("stdDeviation", 5); }
}
function setTab(i) {
  const n = TABS[variant].length; tab = (i + n) % n; sub = 0; fresh = true;
  build(false);
  if (!reduce) { const c = ui.card?.querySelector("g[transform^='translate(16']"); if (c) { c.setAttribute("opacity", 0); const t0 = performance.now(); const step = x => { const p = Math.min(1, (x - t0) / 150); c.setAttribute("opacity", p); if (p < 1) requestAnimationFrame(step); }; requestAnimationFrame(step); } }
  say(`${TABS[variant][tab]} tab.`);
}
function close(msg) {
  if (reduce) { closed = true; build(false); say(msg); return; }
  closing = true; const card = ui.card, t0 = performance.now(), from = Number(ui.blur.getAttribute("stdDeviation")) || 0;
  const step = n => { const t = n - t0, p = Math.min(1, t / 100), q = Math.min(1, t / 250);
    card?.setAttribute("opacity", 1 - p); ui.blur.setAttribute("stdDeviation", (from * (1 - q)).toFixed(2));
    if (q < 1) requestAnimationFrame(step); else { closing = false; closed = true; build(false); } };
  requestAnimationFrame(step); say(msg);
}
function join(team) { close(`Joined ${team}. You spawn in the next wave.`); }
function openMenu() { closed = false; modal = null; fresh = true; build(true); say(variant === "escape" ? "Escape menu: Resume leads; the match keeps running." : "Welcome menu: the join choices lead, and Auto join is focused."); }

svg.addEventListener("keydown", e => {
  if (closed) { if (e.key === "Escape" || e.key === "Enter") { e.preventDefault(); openMenu(); } return; }
  const k = e.key.toLowerCase();
  if (k === "q" || e.key === "ArrowLeft" || e.key === "PageUp") { e.preventDefault(); setTab(tab - 1); }
  else if (k === "e" || e.key === "ArrowRight" || e.key === "PageDown") { e.preventDefault(); setTab(tab + 1); }
  else if (e.key === "ArrowDown") { e.preventDefault(); setFocus(focus + 1); }
  else if (e.key === "ArrowUp") { e.preventDefault(); setFocus(focus - 1); }
  else if (e.key === "Enter" || e.key === " ") { e.preventDefault(); activate(); }
  else if (e.key === "Escape") { e.preventDefault(); if (modal) { modal = null; build(false); } else close(variant === "escape" ? "Back in the match." : "Spectating. Open the menu to join."); }
});
const segs = (hostId, attr, fn) => document.getElementById(hostId)?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return;
  b.parentNode.querySelectorAll(".seg").forEach(x => x.setAttribute("aria-pressed", x === b)); fn(b.dataset[attr]); });
segs("mmVariant", "v", v => { variant = v; tab = 0; focus = 0; openMenu(); });
segs("mmMode", "m", v => { mode = v; tab = 0; openMenu(); });
segs("mmDev", "d", v => { dev = v; build(false); });
document.getElementById("mmSoft")?.addEventListener("click", e => { e.stopImmediatePropagation(); const b = e.currentTarget, on = !b.classList.contains("on"); b.classList.toggle("on", on); b.setAttribute("aria-pressed", on); soft = on; build(false); }, true);
document.getElementById("mmOpen")?.addEventListener("click", openMenu);
const ctl = document.getElementById("mmLayoutCtl"); if (ctl) hud.controls(ctl, { scale: false, classic: false });
build(true);
hud.on(() => build(false));
})();
