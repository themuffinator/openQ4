(() => {
const A = window.__atlas, { svgEl } = A;
const svg = document.getElementById("sbSvg");
if (!svg) return;
/* The scoreboard (section 14.14; Appendix C 1.20). Remastered is the in-game multiplayer card drawn from
   tools/ui/remastered_hud_scoreboard.py and idMultiplayerGame::PublishRemasteredScoreboard: everything inside the card
   is authored in dp, as the generator is, and the card is placed in stock units (1.5 dp a unit) at the view's center.
   Marine is Michroma, Lowpixel Archivo. Stock keeps the soft-backed board of scoreboard.gui. */
const F_L = "Archivo, sans-serif", F_M = "Michroma, Eurostile, sans-serif";
const NOTE = "#FFFF8D", VALUE = "#FFBE23", ORANGE = "#E38900", MARKER = "#909A49", RAIL = "#A0A040", ERR = "#E46D56";
const AQUA = "#5EB987", BLUE = "#3E57B7", OLIVE = "#8B964B";
const TEAM = { 0: OLIVE, 1: "#6AA42B", 2: "#FF7B04", 3: "#999999" }, TITLE = { 0: "PLAYERS", 1: "MARINE", 2: "STROGG", 3: "SPECTATORS" };
const LP = (size, weight = 500) => ({ "font-family": F_L, "font-weight": weight, "font-size": size });
let uid = 0; const nid = p => `sb${p}${++uid}`;
let rem = true, dm = false, tour = false, scroll = false, warm = false;

/* Rows: [name, clan, score, kills, deaths, minutes, ping, flags]; flags name the published bits
   (me, dead, bot, greyed, ready, muted, carrier, friend). Names are examples. */
const T1 = [["You", "DMP", 21, 14, 4, 12, 24, "me carrier"], ["Kaz", "DMP", 18, 12, 6, 12, 38, "friend"], ["Orin", "", 15, 10, 7, 11, 51, "muted ready"],
  ["Bolt", "", 9, 6, 8, 12, 0, "bot ready"], ["Pike", "RSQ", 7, 5, 9, 9, 88, "dead"], ["Reed", "", 4, 3, 10, 6, 140, "ready"]];
const T2 = [["Vex", "STG", 24, 16, 5, 12, 31, "ready"], ["Nyx", "STG", 19, 13, 6, 12, 47], ["Tal", "", 12, 8, 8, 10, 66, "ready"],
  ["Juno", "", 10, 7, 9, 12, 95, "dead"], ["Ash", "ASH", 6, 4, 10, 8, 120, "ready"], ["Moss", "", 2, 1, 12, 3, 260]];
const MORE1 = [["Rook", "", 3, 2, 9, 5, 72], ["Sarge", "RSQ", 3, 2, 7, 4, 44, "ready"], ["Wade", "", 2, 1, 6, 4, 33], ["Lynx", "", 1, 1, 5, 2, 180]];
const MORE2 = [["Hale", "", 5, 3, 8, 6, 58], ["Grit", "STG", 4, 2, 9, 7, 91, "ready"], ["Cole", "", 2, 2, 4, 3, 120], ["Brand", "", 1, 0, 6, 2, 210]];
const SPEC = [["Quill", "", 0, 0, 0, 5, 44], ["Rho", "", 0, 0, 0, 2, 64]];
const TOUR = [["You", "", 0, 0, 0, 9, 24, "me ready"], ["Anderson", "", 0, 0, 0, 9, 0, "bot ready"], ["Morris", "", 0, 0, 0, 9, 0, "bot ready"],
  ["Strauss", "", 0, 0, 0, 9, 0, "bot greyed"], ["Cortez", "", 0, 0, 0, 9, 0, "bot greyed"], ["Tetzlaff", "", 0, 0, 0, 9, 0, "bot ready"]];
const has = (r, f) => (" " + (r[7] || "") + " ").includes(" " + f + " ");
const bars = ping => ping <= 60 ? 4 : ping <= 120 ? 3 : ping <= 200 ? 2 : 1;
const barCol = n => n >= 3 ? "#B5C784" : n === 2 ? "#E3AD36" : ERR;
const ordinal = n => n + (n % 100 >= 11 && n % 100 <= 13 ? "TH" : ["TH", "ST", "ND", "RD"][n % 10] || "TH");
function shadow(parent, x, y, t, o) { svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000" }, parent).textContent = t; svgEl("text", { ...o, x, y }, parent).textContent = t; }
/* Text in dp: the baseline sits in the middle of its `top`..`top + h` line box. Marine sets lowercase as small caps. */
function T(g, x, top, h, s, { f = "l", size = 14, fill = "#fff", op = 1, anchor = "start", max } = {}) {
  const attrs = { x, y: top + h / 2 + size * .36, ...(f === "m" ? { "font-family": F_M, "font-weight": 400 } : LP(size)), "font-size": size, fill, "fill-opacity": op, "text-anchor": anchor };
  if (f === "m" && /[a-z]/.test(s)) attrs["font-variant"] = "small-caps";
  const e = svgEl("text", attrs, g); e.textContent = s;
  if (max && A.measureText && A.measureText(s, size, attrs["font-family"], attrs["font-weight"]) > max) {
    let t = s; while (t.length > 1 && A.measureText(t + "…", size, attrs["font-family"], attrs["font-weight"]) > max) t = t.slice(0, -1); e.textContent = t + "…"; }
  return e;
}
const P = (g, d, o = {}) => svgEl("path", { d, ...o }, g);
const len = (e, fallback) => { try { return e.getComputedTextLength() || fallback; } catch { return fallback; } };   // the rendered advance, small caps included
const lin = (defs, x1, x2, stops) => { const g = svgEl("linearGradient", { id: nid("g"), gradientUnits: "userSpaceOnUse", x1, x2, y1: 0, y2: 0 }, defs);
  stops.forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g)); return `url(#${g.id})`; };

/* ---------------- Remastered: the card (744 dp, Tourney 928 dp; at most 630 / 690 dp tall) ---------------- */
const HEADER_H = 40, INSET = 24, MATCH_TOP = 52, MATCH_H = 22, HEAD_H = 16, LIST_TOP = 98, GAP = 6, BAND_H = 22, BOTTOM = 36, PITCH = 24;
const TREE_TOP = 82, CHART_H = 224, T_LIST_TOP = TREE_TOP + CHART_H + 8 + HEAD_H;
const MARK_W = 12, ICON_W = 18, CLAN_W = 96, NUM_W = 52, BARS_GAP = 10, BARS_W = 22, PING_W = 40;
function cardFrame(g, W, H) {
  const outline = i => { const d = i * .4142;
    return `M${3 + d} ${i}H${W - 12 - d}L${W - i} ${12 + d}V${H - 3 - d}L${W - 3 - d} ${H - i}H${12 + d}L${i} ${H - 12 - d}V${3 + d}Z`; };
  P(g, outline(0), { fill: "#000", "fill-opacity": .94 });
  P(g, outline(.75), { fill: "none", stroke: RAIL, "stroke-width": 1.5 });
}
/* The rows the card shows: each listed panel opens with a 28 dp frame (gap and band), each row 24 dp; past the room
   the window keeps whole rows from the top, panels heading the rows they show, and counts the rest. */
function windowed(panels, view) {
  let y = 0, below = 0; const out = [];
  panels.forEach(p => {
    const rows = [];
    p.rows.forEach(r => { const need = PITCH + (rows.length ? 0 : GAP + BAND_H);
      if (below || y + need > view) { below++; return; } y += need; rows.push(r); });
    if (rows.length || (!below && p.always)) out.push({ ...p, rows });
  });
  return { panels: out, below };
}
function card(root, lay) {
  const defs = svgEl("defs", {}, root);
  const W = tour ? 928 : 744, inner = W - 2 * INSET, listTop = tour ? T_LIST_TOP : LIST_TOP, view = tour ? 324 : 496;
  const kills = !dm && !tour;                      // KILLS in the flag modes (here CTF)
  let panels;
  if (tour) panels = [{ kind: 0, score: "", rows: TOUR, always: true }];
  else if (dm) { const all = [...T1, ...T2, ...(scroll ? [...MORE1, ...MORE2] : [])].sort((a, b) => b[2] - a[2]);
    panels = [{ kind: 0, score: "", rows: all, always: true }, { kind: 3, score: "", rows: SPEC.slice(0, 1) }]; }
  else panels = [{ kind: 1, score: "3", rows: scroll ? [...T1, ...MORE1] : T1, always: true },
    { kind: 2, score: "2", rows: scroll ? [...T2, ...MORE2] : T2, always: true }, { kind: 3, score: "", rows: scroll ? SPEC : SPEC.slice(0, 1) }];
  const listed = panels.filter(p => p.kind !== 3).flatMap(p => p.rows), ranked = listed.length;
  const place = 1 + listed.filter(r => r[2] > (listed.find(q => has(q, "me")) || [0, 0, 0])[2]).length;
  const win = windowed(panels, view);
  const lists = win.panels.reduce((h, p) => h + GAP + BAND_H + p.rows.length * PITCH, 0);
  const H = listTop + lists + BOTTOM;
  // centred over the view, scaled as the HUD scale asks while it fits the view
  const s = Math.min(lay.s, (lay.W - 8) * 1.5 / W, (lay.H - 8) * 1.5 / H) / 1.5;
  const g = svgEl("g", { transform: `translate(${320 - W * s / 2} ${240 - H * s / 2}) scale(${s})` }, root);
  cardFrame(g, W, H);
  // header: the wash in the light plate's shape, the rule, the triangle, the title, your place and the clock
  P(g, `M1.5 3H${W - 1.5}V${HEADER_H - 3}H20.5L1.5 ${HEADER_H - 22}Z`, { fill: lin(defs, 1.5, W - 1.5, [[0, "#fff", .16], [.5, "#fff", .16], [1, "#fff", 0]]) });
  P(g, `M1.5 ${HEADER_H + .5}H${W - 1.5}`, { stroke: RAIL, "stroke-opacity": .8, "stroke-width": 1 });
  P(g, `M${INSET} 16h8v8z`, { fill: MARKER });
  T(g, INSET + 16, 0, HEADER_H, tour ? "TOURNAMENT BRACKETS" : "openQ4 Community", { f: "m", size: 20, op: .9 });
  const cw = len(T(g, W - INSET, 0, HEADER_H, "7:12", { size: 17, fill: VALUE, anchor: "end" }), 30);
  T(g, W - INSET - cw - 16, 0, HEADER_H, `${ordinal(place)} OF ${ranked}`, { size: 15, op: .8, anchor: "end" });
  // match line: the mode and map in Marine, a short rule, then the limits (Tourney: frags to win and the count)
  const mode = tour ? "Tourney" : dm ? "Deathmatch" : "Capture the Flag", map = tour || dm ? "The Fragging Yard" : "Heartless";
  const mx = INSET + len(T(g, INSET, MATCH_TOP, MATCH_H, mode, { f: "m", size: 16, op: .9 }), 120);
  svgEl("rect", { x: mx + 10, y: MATCH_TOP + MATCH_H / 2 - .75, width: 8, height: 1.5, fill: "#fff", "fill-opacity": .5 }, g);
  T(g, mx + 28, MATCH_TOP, MATCH_H, map, { f: "m", size: 16, op: .9 });
  const limits = tour ? ["10 frags to win a round", "Playing tournament 1 of 3"] : [dm ? "Frag Limit: 30" : "Capture Limit: 5", "Time Limit: 15"];
  let lx = W - INSET;
  [...limits].reverse().forEach(t => { lx -= len(T(g, lx, MATCH_TOP, MATCH_H, t, { size: 14, op: .6, anchor: "end" }), 90) + 16; });
  if (tour) bracketChart(svgEl("g", { transform: `translate(${INSET} ${TREE_TOP})` }, g), defs);
  // the column headings, once
  const L = svgEl("g", { transform: `translate(${INSET} 0)` }, g);
  const pingR = inner, barsX = inner - PING_W - BARS_W, timeR = barsX - BARS_GAP, deathsR = timeR - NUM_W, killsR = deathsR - NUM_W;
  const scoreR = kills ? killsR - NUM_W : deathsR - NUM_W, clanX = scoreR - NUM_W - CLAN_W, nameX = MARK_W + 3 * ICON_W;
  const hy = listTop - HEAD_H, hd = { size: 11, op: .4 };
  T(L, nameX, hy, HEAD_H, "NAME", hd); T(L, clanX, hy, HEAD_H, "CLAN", hd);
  [["SCORE", scoreR], ...(kills ? [["KILLS", killsR]] : []), ["DEATHS", deathsR], ["TIME", timeR], ["PING", pingR]].forEach(([t, x]) => T(L, x, hy, HEAD_H, t, { ...hd, anchor: "end" }));
  P(L, `M0 ${listTop - .5}H${inner}`, { stroke: "#fff", "stroke-opacity": .1, "stroke-width": 1 });
  // the panels: 6 dp down, the band in the team color with its title and score, then the rows
  let y = listTop;
  win.panels.forEach(p => {
    y += GAP;
    const tint = TEAM[p.kind];
    P(L, `M6 ${y}H${inner}V${y + BAND_H}H0V${y + 6}Z`, { fill: lin(defs, INSET, INSET + inner, [[0, tint, .49], [.5, tint, .49], [.75, tint, .25], [.95, tint, .05], [1, tint, 0]]) });
    P(L, `M.75 ${y + BAND_H}V${y + 6}L6 ${y + .75}H${inner * .6}`, { fill: "none", "stroke-width": 1.5, stroke: lin(defs, INSET, INSET + inner * .6, [[0, tint, 1], [.7, tint, 1], [1, tint, 0]]) });
    T(L, 16, y, BAND_H, TITLE[p.kind], { f: "m", size: 15, op: .95 });
    if (p.score) T(L, inner - 4, y, BAND_H, p.score, { size: 16, fill: VALUE, anchor: "end" });
    y += BAND_H;
    p.rows.forEach(r => { row(L, r, y, p.kind, { nameX, clanX, scoreR, killsR, deathsR, timeR, barsX, pingR, kills }); y += PITCH; });
  });
  // under the lists: the address, and while the board scrolls the rows below
  const sy = H - 10 - 18;
  T(L, 0, sy, 18, "203.0.113.24:28004", { size: 13, op: .45 });
  if (win.below) { const t = `${win.below} MORE`, tw = A.measureText ? A.measureText(t, 12, F_L, 500) : 40;
    T(L, inner, sy, 18, t, { size: 12, op: .5, anchor: "end" }); P(L, `M${inner - tw - 14} ${sy + 7}h8l-4 5z`, { fill: "#fff", "fill-opacity": .5 }); }
}
function row(g, r, y, kind, c) {
  const [name, clan, sc, kills, deaths, time, ping] = r, me = has(r, "me"), spec = kind === 3, bot = has(r, "bot");
  svgEl("rect", { x: 0, y: y + 1, width: c.pingR, height: PITCH - 2, fill: "#fff", "fill-opacity": me ? .29 : .08 }, g);
  if (me) P(g, `M0 ${y + 8}h8v8z`, { fill: ORANGE });
  const k = svgEl("g", { opacity: has(r, "dead") || has(r, "greyed") ? .5 : 1 }, g), iy = y + 5;
  // the speaker, crossed in red while muted; the friend symbol, 0.125 for someone who is not one
  const sp = svgEl("g", { transform: `translate(${MARK_W} ${iy})` }, k);
  P(sp, "M1.5 5H4.5L8 2V12L4.5 9H1.5Z", { fill: "#fff", "fill-opacity": .7 });
  P(sp, "M10 4L11.6 7L10 10", { fill: "none", stroke: "#fff", "stroke-opacity": .7, "stroke-width": 1.2 });
  if (has(r, "muted")) P(sp, "M1.5 12.5L12.5 1.5", { stroke: ERR, "stroke-width": 1.6 });
  const fr = svgEl("g", { transform: `translate(${MARK_W + ICON_W} ${iy})`, opacity: has(r, "friend") ? 1 : .125 }, k);
  svgEl("circle", { cx: 7, cy: 4.6, r: 2.6, fill: "#fff" }, fr); P(fr, "M1.5 13.5L2.2 10.4L4.6 8.6H9.4L11.8 10.4L12.5 13.5Z", { fill: "#fff" });
  // the state: a skull while dead, else the flag a carrier holds (the other team's), else the warm-up's ready check
  const st = svgEl("g", { transform: `translate(${MARK_W + 2 * ICON_W} ${y + 4})` }, k);
  if (has(r, "dead") && !spec) P(st, "M4 0a4 4 0 0 1 4 4c0 1.4-.7 2-1.3 2.7v1.3H1.3V6.7C.7 6 0 5.4 0 4a4 4 0 0 1 4-4zM2.4 3.4h1.2v1.2H2.4zM4.4 3.4h1.2v1.2H4.4z", { fill: "#fff", "fill-rule": "evenodd", transform: "matrix(1.5 0 0 1.5 1 1.5)" });
  else if (has(r, "carrier") && !dm && !tour) { svgEl("rect", { x: 2, y: 1.5, width: 1.4, height: 13, fill: "#fff", "fill-opacity": .85 }, st); P(st, "M3.4 1.5L13 4.6L3.4 7.8Z", { fill: TEAM[kind === 1 ? 2 : 1] }); }
  else if ((warm || tour) && !spec && has(r, "ready")) P(st, "M1.5 8L5 11.5L12 4", { fill: "none", stroke: "#B5C784", "stroke-width": 2.1, "stroke-linecap": "round", "stroke-linejoin": "round" });
  const ty = y + 4, white = { size: 14, op: .85 };
  T(k, c.nameX, ty, 16, name, { ...white, max: c.clanX - c.nameX - 6 });
  if (bot) { P(k, `M${c.clanX} ${ty}h33v16H${c.clanX + 5}l-5-5z`, { fill: OLIVE, "fill-opacity": .6 }); T(k, c.clanX + 16.5, ty, 16, "BOT", { size: 10.8, anchor: "middle" }); }
  else T(k, c.clanX, ty, 16, clan, { size: 14, op: .6 });
  if (!spec) [[sc, c.scoreR], ...(c.kills ? [[kills, c.killsR]] : []), [deaths, c.deathsR], [time, c.timeR]].forEach(([v, x]) => T(k, x, ty, 16, String(v), { ...white, anchor: "end" }));
  if (!bot) { const n = bars(ping); for (let b = 0; b < 4; b++) { const h = 4.5 + b * 3.3;
    svgEl("rect", { x: c.barsX + 2 + b * 5, y: ty + 15 - h, width: 3.3, height: h, fill: barCol(n), "fill-opacity": b < n ? 1 : .2 }, k); } }
  T(k, c.pingR, ty, 16, String(ping), { size: 14, op: .6, anchor: "end" });
}

/* Tourney's bracket chart: eight arenas, then four, two and the final, the halves converging on the final in the
   card's center; the second half mirrored so every leading edge faces out. */
const PLATE_W = 110, FINAL_W = 136, CGAP = 14, PLATE_H = 38, FINAL_H = 56, CUT = 10;
const colX = c => c <= 3 ? c * (PLATE_W + CGAP) : 3 * (PLATE_W + CGAP) + FINAL_W + CGAP + (c - 4) * (PLATE_W + CGAP);
const FIRST = [0, 1, 2, 3].map(k => 22 + k * 52 + PLATE_H / 2);
const arenaAt = (r, b) => { const col = r === 1 ? (b <= 4 ? 0 : 6) : r === 2 ? (b <= 2 ? 1 : 5) : r === 3 ? (b === 1 ? 2 : 4) : 3;
  const rowI = r === 1 ? (b - 1) % 4 : r === 2 ? (b - 1) % 2 : 0;
  const cy = r === 1 ? FIRST[rowI] : r === 2 ? (FIRST[2 * rowI] + FIRST[2 * rowI + 1]) / 2 : (FIRST[0] + FIRST[3]) / 2;
  const w = r === 4 ? FINAL_W : PLATE_W, h = r === 4 ? FINAL_H : PLATE_H; return { x: colX(col), y: cy - h / 2, w, h, col }; };
/* [round, bracket, name1, score1, name2, score2, state]; state: done1/done2 (winner), mine1 (your arena, you on row 1), empty */
const ARENAS = [[1, 1, "Anderson", 1, "Strauss", 0, "done1"], [1, 2, "Bidwell", 0, "Voss", 1, "done2"], [1, 3, "Morris", 1, "Cortez", 0, "done1"],
  [1, 4, "Rhodes", 0, "Tetzlaff", 1, "done2"], [1, 5, "You", 1, "Gunner", 0, "done1"], [1, 6, "Sorg", 0, "Hollenbeck", 1, "done2"],
  [1, 7, "", "", "", "", "empty"], [1, 8, "", "", "", "", "empty"], [2, 1, "Anderson", 1, "Voss", 0, "done1"], [2, 2, "Morris", 0, "Tetzlaff", 1, "done2"],
  [2, 3, "You", 0, "Hollenbeck", 0, "mine1"], [2, 4, "", "", "", "", "empty"], [3, 1, "Anderson", 2, "Tetzlaff", 1, ""], [3, 2, "", "", "", "", ""], [4, 1, "", "", "", "", ""]];
const isDone = (r, b) => (ARENAS.find(a => a[0] === r && a[1] === b) || [])[6]?.startsWith("done");
function openPlate(g, defs, a, tint, fill) {
  const m = x => a.col > 3 ? a.x + a.w - x : a.x + x, x0 = m(0), x1 = m(a.w), y = a.y, h = a.h;
  P(g, `M${x0} ${y}H${x1}V${y + h}H${m(CUT)}L${x0} ${y + h - CUT}Z`, { fill: lin(defs, x0, x1, [[0, tint, fill], [.6, tint, fill * .45], [1, tint, fill * .1]]) });
  P(g, `M${m(.75)} ${y}V${y + h - CUT}L${m(CUT)} ${y + h - .75}H${x1}`, { fill: "none", "stroke-width": 1.5, stroke: lin(defs, x0, x1, [[0, tint, 1], [.7, tint, .6], [1, tint, .12]]) });
}
function bracketChart(g, defs) {
  [0, 1, 2, 3, 4, 5, 6].forEach(c => { const r = [1, 2, 3, 4, 3, 2, 1][c], fin = r === 4, w = fin ? FINAL_W : PLATE_W;
    T(g, colX(c) + w / 2, 0, 16, ["Preliminaries", "Quarter Finals", "Semi-Finals", "Finals"][r - 1], { f: "m", size: fin ? 13 : 11, fill: fin ? VALUE : "#fff", op: fin ? 1 : .5, anchor: "middle" }); });
  // links: a stub from each feeder to the gap's middle, a riser, a stub in; lit to 0.70 once the feeders are decided
  const link = (pts, lit) => P(g, "M" + pts.map(p => p.join(" ")).join("L"), { fill: "none", stroke: "#fff", "stroke-width": 1.5, "stroke-opacity": lit ? .7 : .22 });
  [[2, 1], [2, 2], [2, 3], [2, 4], [3, 1], [3, 2], [4, 1]].forEach(([r, t]) => {
    const fd = [[r - 1, 2 * t - 1], [r - 1, 2 * t]], to = arenaAt(r, t), both = isDone(...fd[0]) && isDone(...fd[1]);
    if (r === 4) { fd.forEach(f => { const a = arenaAt(...f), cy = a.y + a.h / 2; link(a.x < to.x ? [[a.x + a.w, cy], [to.x, cy]] : [[a.x, cy], [to.x + to.w, cy]], isDone(...f)); }); return; }
    const f0 = arenaAt(...fd[0]), lead = f0.x < to.x, mid = lead ? f0.x + PLATE_W + CGAP / 2 : f0.x - CGAP / 2, cys = [];
    fd.forEach(f => { const a = arenaAt(...f), cy = a.y + a.h / 2; cys.push(cy); link([[lead ? a.x + a.w : a.x, cy], [mid, cy]], isDone(...f)); });
    link([[mid, cys[0]], [mid, cys[1]]], both); link([[mid, to.y + to.h / 2], [lead ? to.x : to.x + to.w, to.y + to.h / 2]], both);
  });
  ARENAS.forEach(([r, b, n1, s1, n2, s2, state]) => {
    const a = arenaAt(r, b), fin = r === 4, done = state.startsWith("done"), mine = state.startsWith("mine"), winner = done ? +state.slice(4) : 0;
    openPlate(g, defs, a, done ? BLUE : mine ? AQUA : OLIVE, done ? .4 : mine ? .22 : fin ? .4 : .3);
    if (state === "empty") { T(g, a.x + a.w / 2, a.y, a.h, "EMPTY", { size: 12, op: .35, anchor: "middle" }); return; }
    if (mine) svgEl("rect", { x: a.x + 1.5, y: a.y + 1, width: a.w - 1.5, height: a.h / 2 - 2, fill: AQUA, "fill-opacity": .34 }, g);
    P(g, `M${a.x + CUT} ${a.y + a.h / 2 - .5}H${a.x + a.w - 4}`, { stroke: "#fff", "stroke-opacity": .12, "stroke-width": 1 });
    const face = fin ? 16 : 13, lead = fin ? 18 : 8, half = a.h / 2;
    [[n1, s1, 1], [n2, s2, 2]].forEach(([n, sc, p]) => {
      const top = a.y + (p - 1) * half, lost = done && winner !== p, won = done && winner === p;
      T(g, a.x + lead, top, half, n, { size: face, op: lost ? .45 : .9, max: a.w - lead - 26 });
      T(g, a.x + a.w - 6, top, half, String(sc), { size: face + (fin ? 2 : 0), fill: won ? VALUE : "#fff", op: won ? 1 : lost ? .45 : .7, anchor: "end" });
      if (fin && won) P(g, `M${a.x + 6} ${top + half / 2 - 4}h7v7z`, { fill: VALUE });
    });
  });
}

/* ---------------- Stock: the soft-backed board of scoreboard.gui (mp_hud.md 6.2) ---------------- */
function stock(root) {
  const defs = svgEl("defs", {}, root);
  const f = svgEl("filter", { id: "sbSoft", x: "-10%", y: "-10%", width: "120%", height: "120%" }, defs); svgEl("feGaussianBlur", { stdDeviation: 9 }, f);
  svgEl("rect", { x: 72, y: 46, width: 496, height: 390, rx: 8, fill: "#000", "fill-opacity": .6, filter: "url(#sbSoft)" }, root);
  const sdm = dm || tour;
  shadow(root, 98, 56, "openQ4 Community · " + (tour ? "Tourney" : dm ? "Deathmatch" : "Capture the Flag"), { ...LP(10.6), fill: NOTE, "letter-spacing": -.3 });
  shadow(root, 539, 56, "203.0.113.24:28004", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  const players = tour ? TOUR : sdm ? [...T1, ...T2].sort((a, b) => b[2] - a[2]) : null;
  const T = sdm ? players.length + 1 : T1.length + T2.length + 1;
  const h = tour ? 26 : sdm ? (T <= 8 ? 35 : T <= 10 ? 28 : T <= 12 ? 23 : T <= 14 ? 20 : 17) : (T <= 8 ? 30 : T <= 10 ? 24 : T <= 12 ? 20 : T <= 14 ? 17 : 15);
  const cols = { name: 157, clan: 336, score: 398, kills: 443, time: 480 };
  const LIST = 32;
  const panel = (y0, title, color, markKind, score, rows, isSpec) => {
    svgEl("path", { d: `M${93 + 7} ${y0 + 3}H557V${y0 + 24.5}H93V${y0 + 10}Z`, fill: color }, root);
    const mg = svgEl("g", { transform: `translate(${102} ${y0 + 5.8}) scale(.667)` }, root);
    svgEl("path", { d: markKind === "m" ? "M12 1L22 7V17L12 23L2 17V7ZM12 6L7 16H17Z" : markKind === "s" ? "M2 6L10 10L12 2L14 10L22 6L16 14L18 22L12 17L6 22L8 14Z" : "M12 2a10 10 0 1 0 .01 0zM12 7a5 5 0 1 1-.01 0z", fill: "#fff", "fill-rule": "evenodd" }, mg);
    svgEl("text", { x: 123, y: y0 + 19, ...LP(14.9), fill: "#fff" }, root).textContent = title;
    if (score !== null) svgEl("text", { x: 539, y: y0 + 19, ...LP(14.9), fill: "#fff", "text-anchor": "end" }, root).textContent = score;
    const hy = y0 + 33, heads = [["NAME", cols.name], ["CLAN", cols.clan], ["SCORE", cols.score]];
    if (!isSpec) { if (!sdm) heads.push(["KILLS", cols.kills]); heads.push(["TIME", cols.time]); }
    heads.forEach(([t, x]) => shadow(root, x, hy, t, { ...LP(7.7, 600), fill: "#fff", "fill-opacity": .4 }));
    shadow(root, 537, hy, "PING", { ...LP(7.7, 600), fill: "#fff", "fill-opacity": .4, "text-anchor": "end" });
    svgEl("rect", { x: 95, y: y0 + 34.6, width: 446, height: 1.1, fill: "#fff", "fill-opacity": .1 }, root);
    rows.forEach((r, i) => {
      const [name, clan, sc, kills, , time, ping] = r, y = y0 + LIST + i * h + 3, me = has(r, "me"), grey = has(r, "greyed");
      svgEl("rect", { x: 93, y, width: 455, height: h - 3, fill: "#9A9C6E", "fill-opacity": me ? .29 : .08 }, root);
      const g = svgEl("g", { opacity: grey ? .5 : 1 }, root), ty = y + (h - 3) / 2 + 4;
      svgEl("path", { d: `M${109} ${ty - 7}h6v5h-3l-3 3zM${111} ${ty - 5}c2 0 3-1 3-2`, fill: "none", stroke: "#8E8D70", "stroke-width": 1 }, g);
      shadow(g, cols.name, ty, name, { ...LP(10.6), fill: "#fff" });
      shadow(g, cols.clan, ty, has(r, "bot") ? "" : clan, { ...LP(10.6), fill: "#fff" });
      if (!isSpec) { shadow(g, cols.score, ty, String(sc), { ...LP(10.6), fill: "#fff" }); if (!sdm) shadow(g, cols.kills, ty, String(kills), { ...LP(10.6), fill: "#fff" });
        shadow(g, cols.time, ty, String(time), { ...LP(10.6), fill: "#fff" }); }
      shadow(g, 537, ty, has(r, "bot") ? "0" : String(ping), { ...LP(10.6), fill: "#fff", "text-anchor": "end" });
    });
    return y0 + LIST + rows.length * h + 5;
  };
  let y = 58;
  if (sdm) { y = panel(y, "PLAYERS", OLIVE, "q", null, players, false); panel(y, "SPECTATORS", "#999999", "q", null, SPEC.slice(0, 1), true); }
  else { y = panel(y, "MARINE", "#6AA42B", "m", 3, T1, false); y = panel(y, "STROGG", "#FF7B04", "s", 2, T2, false); panel(y, "SPECTATORS", "#999999", "q", null, SPEC.slice(0, 1), true); }
  svgEl("rect", { x: 95, y: 408, width: 446, height: 1.3, fill: "#fff", "fill-opacity": .1 }, root);
  shadow(root, 94, 421, tour ? "Tourney" : dm ? "Deathmatch" : "Capture the Flag", { ...LP(10.6), fill: NOTE, "letter-spacing": -.3 });
  shadow(root, 94, 433, sdm ? "q4dm7" : "q4ctf1", { ...LP(9.6), fill: NOTE });
  shadow(root, 539, 421, sdm ? "Frag Limit: 30" : "Capture Limit: 5", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  shadow(root, 539, 433, "Time Limit: 15", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  svgEl("circle", { cx: 294, cy: 425, r: 7, fill: "none", stroke: NOTE, "stroke-width": 1.5 }, root);
  svgEl("path", { d: "M294 425l3 3M291 416h6", stroke: NOTE, "stroke-width": 1.3 }, root);
  shadow(root, 305, 429, "7:12", { ...LP(12), fill: "#fff" });
}

function build() {
  svg.innerHTML = "";
  const lay = A.hud.fit(svg);
  if (A.levelshot) A.levelshot(svgEl("g", { opacity: .55 }, svg), "mp", { x0: lay.x0, y0: lay.y0, W: lay.W, H: lay.H });
  if (rem) card(svgEl("g", {}, svg), lay); else stock(svgEl("g", {}, svg));
}
const tog = (id, fn) => { const b = document.getElementById(id); b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); fn(on); build(); }, true); };
tog("sbRem", on => { rem = on; }); tog("sbDm", on => { dm = on; }); tog("sbTour", on => { tour = on; }); tog("sbScroll", on => { scroll = on; }); tog("sbWarm", on => { warm = on; });
build(); A.hud.on(build); document.fonts?.ready.then(build);
})();
