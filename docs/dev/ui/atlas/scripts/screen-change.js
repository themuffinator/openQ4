
(() => {
const A = window.__atlas, { svgEl } = A;
/* Built once every script has run: the home sheet's kit (scripts/home.js, A.homeKit) draws the title's furniture,
   and the levelshots (scripts/loading.js) load after this file. */
function init() {
const svg = document.getElementById("chg"), K = A.homeKit;
if (!svg || !K) return;
const $ = id => document.getElementById(id);
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif";
const VALUE = "#FFBE23", ORANGE = "#E38900", OLIVE = "#8B964B", RAIL = "#CCCC51", PICTURE = "#414624", DONE = "#A3EB61";
const defs = $("chgDefs");
let uid = 0; const nid = p => `cg${p}${++uid}`;
const grad = (stops, { v = true, user = null } = {}) => {
  const gid = nid("g"), g = svgEl("linearGradient", { id: gid, x1: 0, y1: 0, x2: v ? 0 : 1, y2: v ? 1 : 0,
    ...(user ? { gradientUnits: "userSpaceOnUse", x1: user[0], y1: user[1], x2: user[2], y2: user[3] } : {}) }, defs);
  stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${gid})`;
};
const T = (parent, x, y, s, t, extra = {}) => K.text(parent, s, x, y, t, extra);
const LOW = (size, alpha = .8, fill = "#fff", weight = 600) => ({ family: F_L, size, weight, spacing: .2, fill, alpha });
const MAR = (size, alpha = .8, fill = "#fff") => ({ family: F_M, size, spacing: size * .2, fill, alpha });
const wrap = (s, t, width) => { const out = []; let line = "";
  s.split(" ").forEach(w => { const next = line ? `${line} ${w}` : w; if (line && K.measure(next, t) > width) { out.push(line); line = w; } else line = next; });
  if (line) out.push(line); return out; };

/* Band origins (Appendix B.2) and openQ4's sub-page dock: only the top band moves on, one notch pitch. Each band is
   drawn at its home and moved by a translation: page (323,-63) and (373,35) u, the sub-page's top band 97 u further. */
const PITCH = 97;
const STATES = { home: { top: [0, 0], bot: [0, 0] }, page: { top: [323, -63], bot: [373, 35] }, sub: { top: [323 + PITCH, -63], bot: [373, 35] } };
const accel = p => p < .5 ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);       // accel(d/2, d/2)
function band(parent, pts, top) {
  const a = 1 - (1 - .37) ** .1, d = "M" + pts.map(p => p.join(" ")).join("L");
  for (let i = 10; i > 0; i--) svgEl("path", { d, fill: "none", stroke: "#626934", "stroke-opacity": a.toFixed(4), "stroke-width": (3.8 * i).toFixed(1), "stroke-linejoin": "round" }, parent);
  svgEl("path", { d: d + (top ? "L1400 -300L-900 -300Z" : "L1400 800L-900 800Z"), fill: "#000" }, parent);
}
const ext = (pts, a, b) => [[-900, pts[0][1]], ...pts.slice(1, -1), [1400, pts[pts.length - 1][1]]];
band($("chgTopArt"), ext(K.MARINE_TOP), true);
band($("chgBot"), ext(K.MARINE_BTM), false);

/* The field: a page's lit field (vignette, light band at 0.7) and the grid; the title's levelshot, its 0.30 dimming and
   its signal over it while the title shows. */
svgEl("rect", { x: -400, y: 0, width: 1700, height: 480, fill: "#000" }, $("chgField"));
K.vignette($("chgField"), defs); K.lightBand($("chgField"), defs, "#616F26", .7);
const SV = K.signalView($("chgTitleView"), defs, ["mp"]);
SV.shots[0].setAttribute("opacity", 1);
K.grid($("chgGrid"), defs);
$("chgBackingRect").setAttribute("fill", grad([[.0, "#000", 0], [.06, "#000", 1], [.94, "#000", 1], [1, "#000", 0]], { v: false }));

/* The home: QUAKE 4 and the emblem, the plates from 212.2 u with CONTINUE (a save), the message line, the plinth's
   links and the prompt bar. */
const EM = K.emblem($("chgStage"), defs); K.wordmark($("chgStage"), defs);
/* The crumb is clipped at the band's step: the thin span runs 83 u from 39 u. */
{ const cid = nid("crumb"); svgEl("rect", { x: 39, y: 0, width: 83, height: 40 }, svgEl("clipPath", { id: cid }, defs)); $("chgCrumb").setAttribute("clip-path", `url(#${cid})`); }
const home = $("chgHome"), nav = $("chgNav"), links = $("chgLinks");
const HOME = [["CONTINUE", null, null], ["SINGLE PLAYER", "sp", "Start the campaign or play the Arena."], ["LOAD GAME", null, "Continue from any saved game."],
  ["MULTIPLAYER", "mp", "Find a server, host a match or play with bots."], ["SETTINGS", "settings", "Controls, display, audio and game options."]];
let homeFocus = 0;
function drawHome() {
  nav.innerHTML = "";
  HOME.forEach(([t, page], i) => {
    const g = K.navPlate(nav, defs, 212.2 + i * 30, t, { focused: i === homeFocus });
    g.addEventListener("pointerenter", () => { if (homeFocus !== i && level === "home") { homeFocus = i; drawHome(); } });
    if (page) g.addEventListener("click", () => toPage(page));
  });
  const [, , msg] = HOME[homeFocus];
  if (msg) T(nav, 44, 375.4, msg, K.T.message);
  else { T(nav, 108, 375.5, "Trenches checkpoint", K.T.saveTitle); T(nav, 108, 390.5, "Air Defense Trenches · Corporal · 3:12:05", K.T.saveDetail);
    const cid = nid("th"); svgEl("rect", { x: 44, y: 365, width: 56, height: 31.5 }, svgEl("clipPath", { id: cid }, defs));
    A.levelshot?.(svgEl("g", { "clip-path": `url(#${cid})` }, nav), "sp", { x0: 44, y0: 365, W: 56, H: 31.5 });
    svgEl("rect", { x: 44.5, y: 365.5, width: 55, height: 30.5, fill: "none", stroke: PICTURE }, nav); }
}
drawHome();
K.plinth(links, ["MODS", "UPDATES", "CREDITS", "EXIT"]);
K.promptBar(links, [["ENTER", "SELECT"], ["ESCAPE", "EXIT"]]);

/* A page's prompt bar in the docked bottom band (page_prompts): Back's keycap at 537 u, where the stock BACK plate
   stood, and SELECT's verb ending 15 u before it, centred in the 30 u target from 441 u. */
function keycap(parent, x, key, verb) {
  const kw = K.measure(key, K.T.key) + 10.67;
  svgEl("path", { d: `M${x} 448.67h${kw}v14.67H${x + 4}l-4 -4Z`, fill: "#fff", opacity: .85 }, parent);
  T(parent, x + 5.33, 459, key, K.T.key, { opacity: .85 });
  T(parent, x + kw + 4, 458.3, verb, K.T.verb);
  return kw + 4 + K.measure(verb, K.T.verb);
}
const back = $("chgBack");
const selW = K.measure("ENTER", K.T.key) + 10.67 + 4 + K.measure("SELECT", K.T.verb);
keycap(back, 522 - selW, "ENTER", "SELECT"); keycap(back, 537, "ESCAPE", "BACK");

/* Pages and sub-pages. A page's plates stand from its first row (Single Player 190 u, Multiplayer and Settings 181 u);
   each section that leads deeper names its sub-page; every page ends in BACK. */
const PAGES = {
  sp: { title: "SINGLE PLAYER", top: 190, backing: 0, focus: 0, text: "Play a story campaign or take on the Arena tier ladder.",
    items: [["CAMPAIGN", "campaign", "Play Quake 4 or The Awakening: start a new game, or return to any level you have reached.", "sp"],
      ["ARENA", "arena", "Fight bots up the Arena's tier ladder, match by match, to each tier's champion.", "mp"], ["BACK"]] },
  mp: { title: "MULTIPLAYER", top: 181, backing: 0, focus: 2, text: "Join a match on a server, host your own, watch your demos or set up your player.",
    items: [["JOIN GAME", "-", "Find servers on the internet or your network, sort and filter them, then join or spectate."],
      ["CREATE SERVER", "-", "Host a match of your own: choose the map, game type, rules and bots.", "mp"],
      ["DEMOS", "demos", "Watch the demos you have recorded, and follow any player in a multi-view demo."],
      ["PLAYER SETUP", null, "Set your name, clan tag, player model and rail color."], ["BACK"]] },
  settings: { title: "SETTINGS", top: 181, backing: 0, focus: 1, text: "Change your controls, game options, display and audio, or restore the defaults.",
    items: [["CONTROLS", "-", "Bind keys and buttons for moving, weapons, attacking, looking and the rest."],
      ["GAME OPTIONS", "game", "Set gameplay, mouse, controller, HUD, crosshair and virtual reality options."],
      ["DISPLAY", "-", "Choose the performance preset, renderer, screen and image quality, then apply your changes."],
      ["AUDIO", "-", "Set the volumes, sound device, speakers, effects and subtitles."], ["LOAD DEFAULTS"], ["BACK"]] },
};
const SUBS = {
  campaign: { parent: "sp", label: "CAMPAIGN", backing: 0, focus: 0 },
  arena: { parent: "sp", label: "ARENA", backing: .6, focus: 2 },
  demos: { parent: "mp", label: "DEMOS", backing: .6, focus: 0 },
  game: { parent: "settings", label: "GAME OPTIONS", backing: .6, focus: 0 },
};
const pagesG = $("chgPages"), subsG = $("chgSubs");
const pageBody = {}, subBody = {};
Object.keys(PAGES).forEach(k => { pageBody[k] = svgEl("g", { style: "display:none" }, pagesG); });
Object.keys(SUBS).forEach(k => { subBody[k] = svgEl("g", { style: "display:none" }, subsG); });

/* The section summary's levelshot (SUMMARY_SHOT, picture_frame): 140x105 u at 395,77 u, black 0.60 behind it, its
   upper-leading and lower-trailing corners cut 8 and 12 u, the #414624 border, #CCCC51 rails fading from the cut corners
   and the marker in the upper trailing corner. */
function frame(parent, x, y, w, h, scene) {
  const shape = `M${x + 8} ${y}H${x + w}V${y + h - 12}L${x + w - 12} ${y + h}H${x}V${y + 8}Z`, cid = nid("f");
  svgEl("path", { d: shape, fill: "#000", "fill-opacity": .6 }, parent);
  svgEl("path", { d: shape }, svgEl("clipPath", { id: cid }, defs));
  A.levelshot?.(svgEl("g", { "clip-path": `url(#${cid})` }, parent), scene, { x0: x, y0: y, W: w, H: h });
  svgEl("path", { d: shape, fill: "none", stroke: PICTURE, "stroke-width": 1 }, parent);
  svgEl("path", { d: `M${x + .5} ${y + h / 2}V${y + 8.3}L${x + 8.3} ${y + .5}H${x + w / 2}`, fill: "none", stroke: grad([[0, RAIL, .85], [.34, RAIL, 0]], { user: [x, y, x + w / 2, y + h * .9] }), "stroke-width": 1.5 }, parent);
  svgEl("path", { d: `M${x + w - .5} ${y + h / 2}V${y + h - 12.3}L${x + w - 12.3} ${y + h - .5}H${x + w / 2}`, fill: "none", stroke: grad([[0, RAIL, .85], [.34, RAIL, 0]], { user: [x + w, y + h, x + w / 2, y + h * .1] }), "stroke-width": 1.5 }, parent);
  svgEl("path", { d: `M${x + w - 7.67} ${y + 3}h4.67v4.67z`, fill: RAIL, "fill-opacity": .8 }, parent);
}
/* The content column (reflow_details): Lowpixel 18 dp in #8B964B on 26 dp lines across 210 u from 395,190 u. */
function column(parent, s, top = 190) { wrap(s, LOW(11.4, 1, OLIVE), 210).forEach((l, i) => T(parent, 395, top + 12.6 + i * 17.33, l, LOW(11.4, 1, OLIVE))); }
const hit = (g, enter, click) => { g.classList.add("hit"); if (enter) g.addEventListener("pointerenter", enter); if (click) g.addEventListener("click", click); };

function drawPage(k) {
  const g = pageBody[k], P = PAGES[k];
  g.innerHTML = "";
  P.items.forEach(([t, sub], i) => {
    const p = K.navPlate(g, defs, P.top + i * 30, t, { focused: i === P.focus });
    hit(p, () => { if (P.focus !== i) { P.focus = i; drawPage(k); } },
      t === "BACK" ? () => goBack() : sub && sub !== "-" ? () => deeper(sub) : () => say(sub === "-" ? `${t} opens its own sub-page in the full menu, as ${k === "settings" ? "GAME OPTIONS" : "DEMOS"} does here.`
        : t === "LOAD DEFAULTS" ? "LOAD DEFAULTS asks first, in the stock dialog's words." : `${t} edits in place on this page.`));
  });
  const [, , summary, shot] = P.items[P.focus];
  if (shot) frame(g, 395, 77, 140, 105, shot);
  column(g, summary || P.text);
}

/* The campaign choice tiles (choice_tile): a dark tile, 153.25x44 u, the #CCCC51 rail on its leading edge, 9 u cut and
   foot, the emblem 17.3 u tall over its Marine 17 dp label; the chosen one lit with an orange wash, edge and marker. */
function tile(parent, x, y, w, h, label, chosen, rising) {
  const shape = `M${x} ${y}H${x + w}V${y + h}H${x + 9}L${x} ${y + h - 9}Z`, g = svgEl("g", {}, parent);
  svgEl("path", { d: shape, fill: grad([[0, "#000", .9], [1, "#000", .6]]), opacity: chosen ? .9 : .5 }, g);
  svgEl("path", { d: `M${x + .5} ${y + .5}V${y + h - 9.2}L${x + 9.2} ${y + h - .5}H${x + w - .5}`, fill: "none", stroke: grad([[0, RAIL], [.6, RAIL], [1, RAIL, .25]], { user: [x, 0, x + w, 0] }), "stroke-width": 1, opacity: chosen ? .9 : .5 }, g);
  if (chosen) { svgEl("path", { d: shape, fill: grad([[0, ORANGE, .04], [1, ORANGE, .32]]) }, g);
    svgEl("path", { d: `M${x + .5} ${y + .5}H${x + w - .5}V${y + h - .5}H${x + 9.2}L${x + .5} ${y + h - 9.2}Z`, fill: "none", stroke: ORANGE, "stroke-width": 1 }, g);
    svgEl("path", { d: `M${x + w - 10.67} ${y + 4.67}h6v6z`, fill: ORANGE }, g); }
  const tint = chosen ? ORANGE : "#fff", s = 17.3 / 253.78, cx = x + w / 2, top = y + 14.67 - 8.67;
  const art = svgEl("g", { opacity: chosen ? 1 : .75 }, g);
  svgEl("path", { d: K.EMBLEM, fill: tint, transform: `translate(${cx - 129.7 * s} ${top - 3.55 * s}) scale(${s})` }, art);
  if (rising) { const r = 17.3 * .78, my = y + 14.67 + 17.3 * .36, pts = []; for (let a = -70; a <= 70; a += 10) pts.push(`${(cx + r * Math.sin(a * Math.PI / 180)).toFixed(2)} ${(my - r * Math.cos(a * Math.PI / 180)).toFixed(2)}`);
    svgEl("path", { d: "M" + pts.join("L"), fill: "none", stroke: tint, "stroke-width": 1 }, art); }
  T(g, cx, y + h - 9, label, MAR(7.2, chosen ? 1 : .8, chosen ? "#fff" : "#fff"), { "text-anchor": "middle" });
}
/* An Arena ladder or match plate (section plate): the dark band, rail and corner marker, a Marine label and the status
   under it; locked dimmed with the lock, complete with the check and a green edge, chosen lit over an orange wash. */
function arenaPlate(parent, x, y, w, h, label, status, { chosen = false, locked = false, done = false, boss = false, size = 7.6 } = {}) {
  const g = svgEl("g", { opacity: locked ? .45 : 1 }, parent), shape = `M${x} ${y}H${x + w}V${y + h}H${x + 9}L${x} ${y + h - 9}Z`;
  svgEl("path", { d: shape, fill: grad([[0, "#000", .85], [.7, "#000", .7], [1, "#000", .35]], { v: false }) }, g);
  if (chosen) svgEl("path", { d: shape, fill: grad([[0, ORANGE, .3], [1, ORANGE, .05]], { v: false }) }, g);
  svgEl("path", { d: `M${x + .5} ${y + .5}V${y + h - 9.2}L${x + 9.2} ${y + h - .5}H${x + w}`, fill: "none", stroke: chosen || boss ? ORANGE : done ? DONE : RAIL, "stroke-width": 1, "stroke-opacity": chosen || boss || done ? .9 : .6 }, g);
  svgEl("path", { d: `M${x + 7} ${y + 6}h4.5v4.5z`, fill: chosen ? ORANGE : "#909A49", "fill-opacity": chosen ? 1 : .6 }, g);
  T(g, x + 15, y + h / 2 - 1, label, MAR(size, chosen ? 1 : .85, chosen ? ORANGE : "#fff"));
  T(g, x + 15, y + h / 2 + 10, status, LOW(7.4, .7, done ? DONE : chosen ? VALUE : "#fff"));
  if (done) svgEl("path", { d: `M${x + w - 16} ${y + h / 2 - 1}l3 3.2 5.6-6.6`, fill: "none", stroke: DONE, "stroke-width": 1.4, "stroke-linecap": "round" }, g);
  if (locked) { const lx = x + w - 15, ly = y + h / 2 - 5; svgEl("path", { d: `M${lx + 1.6} ${ly + 3.4}V${ly + 2.2}a2 2 0 014 0V${ly + 3.4}`, fill: "none", stroke: "#C7CCB3", "stroke-width": .9 }, g);
    svgEl("rect", { x: lx, y: ly + 3.4, width: 7.2, height: 5, fill: "#C7CCB3" }, g); }
  return g;
}

function drawSub(k) {
  const g = subBody[k], S = SUBS[k];
  g.innerHTML = "";
  if (k === "campaign") {
    // the campaign choice over NEW GAME, CHAPTERS and BACK; the focused section's summary and levelshot beside them
    tile(g, 44, 190, 153.25, 44, "QUAKE 4", true, false); tile(g, 203.75, 190, 153.25, 44, "THE AWAKENING", false, true);
    const items = [["NEW GAME", "Start the chosen campaign from its first level, at the difficulty you choose, with or without Turbo Mode."],
      ["CHAPTERS", "Return to any level of the chosen campaign you have reached, from its start autosave where it has one."], ["BACK"]];
    items.forEach(([t], i) => { const p = K.navPlate(g, defs, 242 + i * 30, t, { focused: i === S.focus });
      hit(p, () => { if (S.focus !== i) { S.focus = i; drawSub(k); } }, t === "BACK" ? () => goBack() : () => say(`${t} goes one level deeper again: to the difficulty or to the chosen campaign's levels.`)); });
    if (items[S.focus][1]) { frame(g, 395, 77, 140, 105, "sp"); column(g, items[S.focus][1]); }
    else column(g, "Choose a story campaign. The Awakening is an unfinished alpha; some missions and assets remain incomplete.");
  } else if (k === "arena") {
    // retained_arena: the tiers at 16,72 u (170x40 u, 44 u pitch), the chosen tier's matches at 196,72 u (36 u plates on a
    // 40 u pitch, the boss on a 50 u plate at 194 u), the briefing at 254 u, progress at 298 u; the detail column at 404 u
    T(g, 16, 66.5, "TIERS", LOW(9, .4)); T(g, 196, 66.5, "MATCHES", LOW(9, .4));
    [["TIER 1", "COMPLETE"], ["TIER 2", "COMPLETE"], ["TIER 3", "AVAILABLE"], ["TIER 4", "LOCKED"], ["TIER 5", "LOCKED"]].forEach(([t, st], i) => {
      const p = arenaPlate(g, 16, 72 + i * 44, 170, 40, t, st, { chosen: i === S.focus, done: st === "COMPLETE" && i !== S.focus, locked: st === "LOCKED", size: 8.4 });
      if (st !== "LOCKED") hit(p, () => { if (S.focus !== i) { S.focus = i; drawSub(k); } });
    });
    const won = S.focus < 2 ? 3 : 1;
    ["QUALIFIER 1", "QUALIFIER 2", "QUALIFIER 3"].forEach((t, i) => arenaPlate(g, 196, 72 + i * 40, 202, 36, t, i < won ? "COMPLETE" : i === won ? "AVAILABLE" : "LOCKED",
      { done: i < won, chosen: i === won, locked: i > won }));
    arenaPlate(g, 196, 194, 202, 50, "BOSS MATCH", won === 3 ? "BOSS READY" : "LOCKED", { boss: won === 3, locked: won < 3, size: 9.2 });
    T(g, 196, 266, `TIER ${S.focus + 1}`, MAR(7.4, .9)); wrap("Win the qualifiers to face the tier's champion.", LOW(9.3, .7), 202).forEach((l, i) => T(g, 196, 280 + i * 12, l, LOW(9.3, .7)));
    T(g, 16, 306, "CAMPAIGN PROGRESS", LOW(9, .4)); T(g, 186, 306, "7 / 20", LOW(9, .8, VALUE), { "text-anchor": "end" });
    svgEl("rect", { x: 16, y: 312, width: 170, height: 3, fill: "#E06C00", "fill-opacity": .3 }, g); svgEl("rect", { x: 16, y: 312, width: 170 * .35, height: 3, fill: "#E06C00", "fill-opacity": .6 }, g);
    frame(g, 404, 58, 226, 112, "mp");
    T(g, 404, 188, "LOCKDOWN", MAR(7.6, .9));
    [["MAP", "Lockdown", 236], ["GAME TYPE", "Deathmatch", 249]].forEach(([l, v, y]) => { T(g, 404, y, l, LOW(8.8, .4)); T(g, 480, y, v, LOW(8.8, .8)); });
    T(g, 404, 325, "DIFFICULTY", LOW(8.8, .4)); T(g, 404, 342, "–   VETERAN   +", MAR(7, .85, VALUE));
    arenaPlate(g, 404, 354, 226, 40, "START MATCH", "", { chosen: true, size: 9.6 });
  } else if (k === "demos") {
    // retained_demos: the filter tags at 43 u, headings at 63 u over the 394 u list at 217,78 u (20 u rows), the
    // selected demo's map 183x137 u at 25,78 u with its details from 215 u, PLAY, DELETE and REFRESH from 256 u (37 u)
    let tx = 217;
    ["MULTI-VIEW", "RENDER", "LEGACY", "INCOMPLETE"].forEach((t, i) => { const w = K.measure(t, MAR(6.2)) + 17;
      svgEl("path", { d: `M${tx + 8} 43H${tx + w}V58H${tx}V51Z`, fill: OLIVE, "fill-opacity": i === 0 ? .6 : .28 }, g);
      svgEl("path", { d: `M${tx + .5} 58V51L${tx + 8} 43.5H${tx + w * .7}`, fill: "none", stroke: OLIVE, "stroke-width": .9 }, g);
      T(g, tx + 9, 53.6, t, MAR(6.2, i === 0 ? .95 : .6)); tx += w + 4; });
    [["NAME", 0], ["MAP", 150], ["DATE", 250], ["DURATION", 324]].forEach(([t, x]) => T(g, 217 + x, 71, t, LOW(9, .4)));
    svgEl("path", { d: "M217 75.5H611", stroke: OLIVE, "stroke-opacity": .6 }, g);
    const demos = [["Hangar Brawl", "q4dm5", "Nov 16", "9:42"], ["Rail Only Nights", "q4dm3", "Nov 14", "12:08"], ["Clan Scrim", "q4ctf1", "Nov 12", "20:00"],
      ["Low Grav Fun", "q4dm7", "Nov 11", "6:31"], ["Arena Ladder", "q4dm1", "Nov 9", "14:55"], ["Dead Zone Practice", "q4ctf4", "Nov 7", "11:20"], ["Bloodworks Night", "q4dm2", "Nov 2", "15:00"]];
    demos.forEach((d, i) => {
      const y = 78 + i * 20, on = i === S.focus, r = svgEl("g", {}, g);
      svgEl("rect", { x: 217, y, width: 378, height: 19, fill: on ? "#FF9C00" : "#9A9C6E", "fill-opacity": on ? .41 : .08 }, r);
      [[d[0], 0], [d[1], 150], [d[2], 250], [d[3], 324]].forEach(([s, x], j) => T(r, 221 + x, y + 13, s, LOW(10, j ? .65 : .9)));
      hit(r, () => { if (S.focus !== i) { S.focus = i; drawSub(k); } });
    });
    frame(g, 25, 78, 183, 137, "mp");
    const d = demos[S.focus];
    // the kind, status and note lines from 215, 230 and 245 u
    T(g, 37, 224.5, "MULTI-VIEW DEMO", LOW(10, .9)); T(g, 37, 239.5, `${d[1]} · ${d[3]}`, LOW(10, .65)); T(g, 37, 251.5, "Ready to play", LOW(9, .65, VALUE));
    [["PLAY", 256], ["DELETE", 293], ["REFRESH", 330]].forEach(([t, y], i) => arenaPlate(g, 21, y, 181, 33, t, "", { chosen: i === 0, size: 8.4 }));
  } else {
    // retained_settings_rows: the tab strip at 46 u across 44-631 u, the headings at 70 u, two columns of 287 u from 87 u
    // on the 24 u pitch, values from 100 u into the row
    const tabs = [["GENERAL", 44 + 30, true], ["HUD", 290], ["VIRTUAL REALITY", 470]];
    svgEl("path", { d: "M44 66H631", stroke: "#fff", "stroke-opacity": .5 }, g);
    svgEl("path", { d: "M63 66V48H215L227 66Z", fill: grad([[0, OLIVE, .7], [1, OLIVE, .25]]), stroke: "#fff", "stroke-opacity": .5 }, g);
    [["Q", 51], ["E", 619]].forEach(([q, x]) => { svgEl("path", { d: `M${x - 5} 49h11v12H${x - 2}l-3 -3Z`, fill: "#fff", opacity: .85 }, g); T(g, x - 2.5, 58.4, q, K.T.key); });
    tabs.forEach(([t, x, on]) => T(g, on ? 139 : x, 59.5, t, MAR(6.4, on ? 1 : .5), { "text-anchor": on ? "middle" : "start" }));
    svgEl("rect", { x: 44, y: 66, width: 587, height: 16, fill: "#fff", "fill-opacity": .05 }, g);
    T(g, 50, 77.5, "GENERAL", LOW(9.3, .4)); T(g, 350, 77.5, "GAMEPLAY", LOW(9.3, .4));
    const rows = [[["Auto Weapon Reload", "box"], ["Auto Weapon Switch", "box"], ["Show Decals", "box"], ["Show Gun Model", "box"], ["Gun Position", "Lower Right"], ["Pickup Style", "Original"], ["View Kick", "1.0"]],
      [["Corpse Sink", "Off"], ["Corpse Time", "Stock"], ["Language", "English"], ["Console Access", "Tilde"], ["Screenshot Format", "TGA"], ["Auto Skip Cinematics", "box"], ["Enhanced Physics", "off"]]];
    rows.forEach((col, c) => col.forEach(([l, v], i) => {
      const x = 44 + c * 300, y = 87 + i * 24, on = c === 0 && i === S.focus, r = svgEl("g", {}, g);
      svgEl("path", { d: `M${x} ${y}H${x + 287}V${y + 22}H${x + 6}L${x} ${y + 16}Z`, fill: on ? "#8B964B" : "#000", "fill-opacity": on ? .35 : .45, stroke: "#fff", "stroke-opacity": .1 }, r);
      if (on) svgEl("rect", { x, y, width: 2.5, height: 22, fill: ORANGE }, r);
      T(r, x + 14, y + 15, l, LOW(9.8, on ? 1 : .8, on ? ORANGE : "#fff"));
      if (v === "box" || v === "off") { svgEl("rect", { x: x + 133, y: y + 7, width: 8, height: 8, fill: "none", stroke: VALUE, "stroke-width": .9 }, r);
        if (v === "box") svgEl("rect", { x: x + 135, y: y + 9, width: 4, height: 4, fill: VALUE }, r); }
      else { T(r, x + 133, y + 15, v, LOW(9.8, .85, VALUE)); if (!/^\d/.test(v)) svgEl("path", { d: `M${x + 272} ${y + 9}l3.5 3.5 3.5-3.5`, fill: "none", stroke: VALUE, "stroke-width": 1 }, r); }
      hit(r, () => { if (c === 0 && S.focus !== i) { S.focus = i; drawSub(k); } });
    }));
  }
}
const subTitle = k => SUBS[k].label;

/* State. Level is home, page or sub; animation values live in cur. */
let level = "home", pageK = null, subK = null;
const cur = { topX: 0, topY: 0, botX: 0, botY: 0, homeOp: 1, linksOp: 1, homeX: 0, markOp: 1, picsOp: 1, pageOp: 0, pageX: 0, subOp: 0, subX: 0,
  titleOp: 0, crumbOp: 0, crumbY: 31.6, crumbS: 8.4, backingOp: 0, backOp: 0 };
const setTitle = s => { $("chgTitle").textContent = s; };
const apply = () => {
  $("chgTop").setAttribute("transform", `translate(${cur.topX} ${cur.topY})`);
  $("chgBot").setAttribute("transform", `translate(${cur.botX} ${cur.botY})`);
  nav.setAttribute("opacity", cur.homeOp); links.setAttribute("opacity", cur.linksOp);
  home.setAttribute("transform", `translate(${cur.homeX} 0)`);
  $("chgStage").setAttribute("opacity", cur.markOp); $("chgTitleView").setAttribute("opacity", cur.picsOp);
  pagesG.setAttribute("opacity", cur.pageOp); pagesG.setAttribute("transform", `translate(${cur.pageX} 0)`);
  subsG.setAttribute("opacity", cur.subOp); subsG.setAttribute("transform", `translate(${cur.subX} 0)`);
  $("chgBackingRect").setAttribute("opacity", cur.backingOp);
  $("chgTitle").setAttribute("fill-opacity", cur.titleOp); back.setAttribute("opacity", cur.backOp);
  const c = $("chgCrumb"); c.setAttribute("fill-opacity", cur.crumbOp); c.setAttribute("y", cur.crumbY.toFixed(2)); c.setAttribute("font-size", cur.crumbS.toFixed(2));
  nav.style.pointerEvents = level === "home" ? "" : "none"; pagesG.style.pointerEvents = level === "page" ? "" : "none"; subsG.style.pointerEvents = level === "sub" ? "" : "none";
};

/* The choreography of each change (section 8; band_motion, retained_menu_levels). */
const bands = s => [...STATES[s].top, ...STATES[s].bot];
const CHOREO = {
  page: P => [
    { label: "content.out", keys: ["homeOp"], to: [0], t0: 0, dur: 250 },
    { label: "plinth, prompts", keys: ["linksOp"], to: [0], t0: 0, dur: 50 },
    { label: "light.up (out)", keys: ["markOp"], to: [0], t0: 0, dur: 250 },
    { label: "title signal", keys: [], to: [], t0: 0, dur: 550, burst: "depart" },
    { label: "frame.dock", keys: ["topX", "topY", "botX", "botY"], to: bands("page"), t0: 50, dur: 500, ease: "accel" },
    { label: "screen.depart", keys: ["homeX"], to: [640], t0: 50, dur: 500, ease: "accel" },
    { label: "page plates", keys: ["pageOp", "pageX", "picsOp"], to: [1, 0, 0], t0: 550, dur: 0 },
    { label: "content.in", keys: ["titleOp", "backingOp", "backOp"], to: [.5, P.backing, 1], t0: 550, dur: 150 },
  ],
  home: () => [
    { label: "title", keys: ["titleOp", "crumbOp", "picsOp"], to: [0, 0, 1], t0: 0, dur: 0 },
    { label: "prompt bar", keys: ["backOp"], to: [0], t0: 0, dur: 100 },
    { label: "backing", keys: ["backingOp"], to: [0], t0: 0, dur: 250 },
    { label: "screen.return", keys: ["pageX"], to: [-640], t0: 0, dur: 300 },
    { label: "frame.dock", keys: ["topX", "topY", "botX", "botY"], to: bands("home"), t0: 0, dur: 500, ease: "accel" },
    { label: "title signal", keys: [], to: [], t0: 0, dur: 480, burst: "returnHome" },
    { label: "home re-placed", keys: ["homeX"], to: [0], t0: 0, dur: 0 },
    { label: "content.in", keys: ["homeOp", "linksOp", "pageOp"], to: [1, 1, 0], t0: 500, dur: 150 },
    { label: "light.up (in)", keys: ["markOp"], to: [1], t0: 500, dur: 150 },
  ],
  deeper: S => [
    { label: "page plates out", keys: ["pageOp"], to: [0], t0: 0, dur: 150 },
    { label: "page sweep", keys: ["pageX"], to: [640], t0: 0, dur: 300 },
    { label: "title.crumb", keys: ["crumbOp", "crumbY", "crumbS"], to: [.4, 24.6, 6.72], t0: 0, dur: 150 },
    { label: "frame.step", keys: ["topX"], to: [STATES.sub.top[0]], t0: 50, dur: 300, ease: "accel" },
    { label: "sub-page plates", keys: ["subOp", "subX"], to: [1, 0], t0: 350, dur: 0 },
    { label: "content.in", keys: ["titleOp", "backingOp"], to: [.5, S.backing], t0: 350, dur: 150 },
  ],
  up: P => [
    { label: "title", keys: ["titleOp"], to: [0], t0: 0, dur: 0 },
    { label: "screen.return", keys: ["subX"], to: [-640], t0: 0, dur: 300 },
    { label: "content.out", keys: ["backingOp"], to: [P.backing], t0: 0, dur: 250 },
    { label: "frame.step", keys: ["topX"], to: [STATES.page.top[0]], t0: 0, dur: 300, ease: "accel" },
    { label: "title.crumb (back)", keys: ["crumbOp", "crumbY", "crumbS"], to: [.5, 31.6, 8.4], t0: 150, dur: 150 },
    { label: "page re-placed", keys: ["pageX"], to: [0], t0: 0, dur: 0 },
    { label: "content.in", keys: ["pageOp", "subOp"], to: [1, 0], t0: 300, dur: 150 },
  ],
};
const CAPS = {
  page: "Home to page: the home plates and message line fade over 250 ms, the plinth and prompt bar over 50 ms, and QUAKE 4 and the emblem go to black. From 50 ms the bands dock and the home layer sweeps 640 u toward the trailing edge, both on accel(250, 250), while the title's signal breaks up, peaking at 0.85 at 400 ms. At 550 ms the page presents with its plates at rest; its title and prompt bar fade in over 150 ms.",
  home: "Back to home: the title drops at once, the prompt bar fades over 100 ms while the page sweeps 640 u toward the leading edge in 300 ms. The bands start home at once on accel(250, 250) and the title's signal rises to 0.70 by 120 ms and settles by 480 ms. At 500 ms the home content fades in over 150 ms and QUAKE 4 and the emblem light up from black.",
  deeper: "Page to sub-page: the page's plates fade over 150 ms while it sweeps 640 u toward the trailing edge over 300 ms. The page title rises 7 u into the band's thin span, steps down to 80% and dims to 0.40. From 50 ms only the top band steps one notch pitch, 97 u, on accel(150, 150), and its step slides under the crumb while the section's label carries to the step's foot. At 350 ms the sub-page presents and its backing fades in. The bottom band and the prompt bar never move.",
  up: "Sub-page to page: the sub-page's title drops at once and the sub-page sweeps 640 u toward the leading edge over 300 ms while the top band steps back. Once the step has passed back under it, the crumb returns to the title slot over 150 ms. At 300 ms the page fades in over 150 ms with the focus on the section that led down.",
};

let run = null, speed = 1, reduced = matchMedia("(prefers-reduced-motion: reduce)").matches, carryOn = true;
const tl = $("tl"), cap = $("chgCap");
function drawTimeline(kind, entries) {
  tl.innerHTML = "";
  entries.forEach(e => {
    const n = document.createElement("span"); n.textContent = e.label; tl.appendChild(n);
    const trk = document.createElement("span"); trk.className = "trk";
    const b = document.createElement("span"); b.className = "bar" + (e.ease === "accel" ? " trap" : "");
    b.style.left = (e.t0 / 700 * 100) + "%"; b.style.width = `max(2px, ${e.dur / 700 * 100}%)`;
    b.style.setProperty("--bc", e.label.startsWith("frame") ? "#CCCC51" : e.burst ? "#E06C00" : /content\.in|\(in\)|plates$|re-placed|crumb \(back\)/.test(e.label) ? "#B5C784" : OLIVE);
    trk.appendChild(b); tl.appendChild(trk);
  });
  const ax = document.createElement("span"); ax.className = "axis";
  [0, 100, 200, 300, 400, 500, 600, 700].forEach(ms => { const s = document.createElement("span"); s.style.left = (ms / 7) + "%"; s.textContent = ms; ax.appendChild(s); });
  tl.appendChild(document.createElement("span")); tl.appendChild(ax);
  const head = document.createElement("span"); head.className = "head"; head.id = "tlHead"; tl.appendChild(head);
  cap.textContent = CAPS[kind];
}

/* Play a change. Input during a change starts the next one from the current values; band moves then take a
   duration proportional to their remaining distance (section 8, band motion). */
let faultKey = "rest";
function play(kind, entriesIn, { carry, done } = {}) {
  const interrupted = run !== null;
  const entries = entriesIn.map(e => {
    const from = e.keys.map(k => cur[k]);
    let { t0, dur } = e;
    if (interrupted && e.ease === "accel") {
      const full = e.keys[0] === "topX" && e.dur === 300 ? PITCH : Math.abs(STATES.page.top[0] - STATES.home.top[0]);
      const frac = Math.min(1, Math.abs(e.to[0] - from[0]) / full);
      t0 = 0; dur = Math.min(e.dur, Math.max(150, e.dur * frac));
    }
    return { ...e, from, t0, dur };
  });
  drawTimeline(kind, entries);
  const start = performance.now(), total = Math.max(...entries.map(e => e.t0 + e.dur)), head = $("tlHead");
  cancelAnimationFrame(run);
  const burst = entries.find(e => e.burst);
  const frame = now => {
    const t = (now - start) * speed;
    entries.forEach(e => {
      let t0e = e.t0, dur = e.dur;
      if (reduced) { t0e = 0; dur = e.keys.some(k => /Op$/.test(k)) ? 80 : 0; }
      const p = dur === 0 ? (t >= t0e ? 1 : 0) : Math.min(1, Math.max(0, (t - t0e) / dur));
      const k = e.ease === "accel" && !reduced ? accel(p) : p;
      e.keys.forEach((key, i) => { cur[key] = e.from[i] + (e.to[i] - e.from[i]) * k; });
    });
    apply();
    // the title's signal faults with the menu's own moves; reduced motion drops it
    const s = burst && !reduced ? K.lerpKeys(K.BURSTS[burst.burst], t) : 0, ph = Math.floor(t / K.SIGNAL_STEP), key = s > 0 ? `${ph}:${s.toFixed(2)}` : "rest";
    if (key !== faultKey) { faultKey = key; K.drawFault(SV, defs, s, 400 + ph); }
    const c = $("chgCarry");
    if (carry && carryOn && !reduced) {                 // title.carry: the activated label travels into the title slot
      const pc = accel(Math.min(1, Math.max(0, (t - carry.t0) / carry.dur)));
      c.textContent = carry.text;
      c.setAttribute("x", carry.x0 + (carry.x1 - carry.x0) * pc); c.setAttribute("y", carry.y0 + (carry.y1 - carry.y0) * pc);
      c.setAttribute("font-size", carry.s0 + (8.4 - carry.s0) * pc); c.setAttribute("letter-spacing", 2.3 + (1.75 - 2.3) * pc);
      c.setAttribute("fill", pc < .5 ? ORANGE : "#FFFFFF");
      c.setAttribute("opacity", t < carry.t0 + carry.dur ? (pc < .5 ? 1 : 1 - .5 * (pc - .5) * 2) : Math.max(0, .5 - (t - carry.t0 - carry.dur) / 300));
    } else c.setAttribute("opacity", 0);
    head.style.opacity = 1; head.style.left = `calc(132px + (100% - 132px) * ${Math.min(t, 700) / 700})`;
    if (t < (reduced ? 80 : total)) run = requestAnimationFrame(frame);
    else { head.style.opacity = 0; run = null; if (faultKey !== "rest") { faultKey = "rest"; K.drawFault(SV, defs, 0, 0); } done && done(); }
  };
  run = requestAnimationFrame(frame);
}

function toPage(k) {
  if (level !== "home") return;
  level = "page"; pageK = k; homeFocus = HOME.findIndex(h => h[1] === k); drawHome();
  Object.entries(pageBody).forEach(([n, g]) => g.style.display = n === k ? "" : "none");
  drawPage(k); setTitle(PAGES[k].title);
  cur.crumbOp = 0; cur.pageX = 0;
  play("page", CHOREO.page(PAGES[k]), { carry: { text: PAGES[k].title, x0: 44, y0: 212.2 + homeFocus * 30 + 19.6, s0: 11.2, x1: 39, y1: 31.6, t0: 50, dur: 500 } });
  sync();
}
function deeper(k) {
  if (level !== "page") return;
  level = "sub"; subK = k;
  const P = PAGES[pageK], i = P.items.findIndex(x => x[1] === k);
  Object.entries(subBody).forEach(([n, g]) => g.style.display = n === k ? "" : "none");
  drawSub(k);
  const crumb = $("chgCrumb"); crumb.textContent = P.title;                 // the title stays where it stands, as the crumb
  cur.crumbOp = Math.max(cur.titleOp, cur.crumbOp); cur.crumbY = 31.6; cur.crumbS = 8.4; cur.titleOp = 0; setTitle(subTitle(k)); cur.subX = 0;
  play("deeper", CHOREO.deeper(SUBS[k]), { carry: { text: SUBS[k].label, x0: 44, y0: P.top + i * 30 + 19.6, s0: 11.2, x1: 136, y1: 31.6, t0: 50, dur: 300 } });
  sync();
}
function goBack() {
  if (level === "sub") {
    level = "page";
    const P = PAGES[pageK];
    drawPage(pageK);
    play("up", CHOREO.up(P), { done: () => { if (level === "page") { setTitle(P.title); cur.titleOp = .5; cur.crumbOp = 0; apply(); } } });
  } else if (level === "page") {
    level = "home";
    play("home", CHOREO.home());
  }
  sync();
}

/* Controls: the destinations of the current level, Back, and where the menu stands. */
const go = $("chgGo");
const nice = t => t[0] + t.slice(1).toLowerCase();
function sync() {
  go.querySelectorAll("button").forEach(b => b.remove());
  const opts = level === "home" ? [["Single player", () => toPage("sp")], ["Multiplayer", () => toPage("mp")], ["Settings", () => toPage("settings")]]
    : level === "page" ? PAGES[pageK].items.filter(x => x[1] && x[1] !== "-").map(([t, s]) => [nice(t), () => deeper(s)]) : [];
  opts.forEach(([t, fn]) => { const b = document.createElement("button"); b.className = "seg"; b.textContent = t; b.addEventListener("click", fn); go.appendChild(b); });
  if (!opts.length) { const b = document.createElement("button"); b.className = "seg"; b.disabled = true; b.textContent = level === "sub" ? "Deepest level" : "No deeper level"; go.appendChild(b); }
  $("goBack").disabled = level === "home";
  const names = level === "home" ? ["Home"] : level === "page" ? ["Home", PAGES[pageK].title] : ["Home", PAGES[pageK].title, SUBS[subK].label];
  const n = names.length;
  $("chgWhere").innerHTML = `<b>Level ${n - 1}</b><span class="lvl">${[0, 1, 2].map(i => `<i class="${i < n ? "on" : ""}"></i>`).join("")}</span>` +
    names.map(nice).join(" › ") + (level === "sub" ? " · the top band has stepped one notch pitch" : "");
}
const say = s => { cap.textContent = s; };
$("goBack").addEventListener("click", goBack);
const toggle = (box, fn) => box.addEventListener("click", e => { e.stopImmediatePropagation(); const on = box.classList.toggle("on"); box.setAttribute("aria-pressed", on); fn(on); }, true);
toggle($("slowBox"), on => { speed = on ? .25 : 1; });
toggle($("rmBox"), on => { reduced = on || matchMedia("(prefers-reduced-motion: reduce)").matches; });
toggle($("carryBox"), on => { carryOn = on; });
apply(); sync(); drawTimeline("page", CHOREO.page(PAGES.sp));
/* QUAKE 4's emblem keeps its charge and reticle at home. */
const t0 = performance.now();
if (A.tick) A.tick(svg, now => { if (level === "home" && !reduced) K.animateEmblem(EM, now - t0); });
}
if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init); else init();
})();
