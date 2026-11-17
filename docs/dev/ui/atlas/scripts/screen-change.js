
(() => {
const A = window.__atlas, { svgEl } = A;
const svg = document.getElementById("chg");
if (!svg) return;
const $ = id => document.getElementById(id);
const F_M = "Michroma, Eurostile, sans-serif", F_L = "Archivo, sans-serif";
const VALUE = "#FFBE23", ORANGE = "#E38900", OLIVE = "#8B964B";

/* Band origins (Appendix B.2) and openQ4's sub-page dock: only the top band moves on, one notch pitch. */
const PITCH = 97;
const STATES = { home: { top: [-400, 0], bot: [-399, 351] }, page: { top: [-77, -63], bot: [-26, 386] }, sub: { top: [-77 + PITCH, -63], bot: [-26, 386] } };
const accel = p => p < .5 ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);       // accel(d/2, d/2)

/* Plates, as on the home and settings screens. */
let uid = 0;
const plate = (parent, x, y, w, h, cut, light, label, fs, current, { dim = false, lock = false } = {}) => {
  const g = svgEl("g", {}, parent);
  const id = `cg${++uid}`;
  const lg = svgEl("linearGradient", { id, x1: 0, x2: 1 }, svgEl("defs", {}, g));
  if (light) { svgEl("stop", { offset: .5, "stop-color": OLIVE, "stop-opacity": .49 }, lg); svgEl("stop", { offset: 1, "stop-color": OLIVE, "stop-opacity": 0 }, lg); }
  else { svgEl("stop", { offset: .44, "stop-color": "#000" }, lg); svgEl("stop", { offset: .88, "stop-color": "#000", "stop-opacity": 0 }, lg); }
  const art = svgEl("g", { opacity: current ? 1 : dim ? .2 : .4 }, g);
  svgEl("path", { d: `M${x} ${y}H${x + w}V${y + h}H${x + cut}L${x} ${y + h - cut}Z`, fill: `url(#${id})` }, art);
  svgEl("path", { d: `M${x + .5} ${y}V${y + h - cut}L${x + cut} ${y + h - .5}H${x + w * .8}`, fill: "none", stroke: light ? OLIVE : "#CCCC51", "stroke-width": .9 }, art);
  if (label) {
    const mx = x + 9, my = y + h / 2 - 3;
    svgEl("path", { d: `M${mx} ${my}h5.5v5.5z`, fill: current ? ORANGE : "#909A49", opacity: current ? 1 : dim ? .25 : .4 }, g);
    const t = svgEl("text", { x: mx + 9, y: y + h / 2 + fs * .36, "font-family": F_M, "font-size": fs, fill: current ? ORANGE : "#fff", "fill-opacity": current ? 1 : dim ? .35 : .8 }, g);
    t.textContent = label;
    if (lock) lockGlyph(g, mx + 9 + measure(label, fs) + 6, y + h / 2 - 4.2, .45);
  }
  return g;
};
const ctx = document.createElement("canvas").getContext("2d");
const measure = (s, fs, font = F_M) => { ctx.font = `${fs}px ${font}`; return ctx.measureText(s).width; };
const lockGlyph = (g, x, y, a = .6) => {
  svgEl("path", { d: `M${x + 1.6} ${y + 3.4}V${y + 2.2}a2 2 0 014 0V${y + 3.4}`, fill: "none", stroke: "#fff", "stroke-opacity": a, "stroke-width": .9 }, g);
  svgEl("rect", { x, y: y + 3.4, width: 7.2, height: 5, fill: "#fff", "fill-opacity": a }, g);
};
const T = (parent, x, y, s, attrs = {}) => { const t = svgEl("text", { x, y, "font-family": F_L, "font-weight": 500, "font-size": 9.3, fill: "#fff", "fill-opacity": .8, ...attrs }, parent); t.textContent = s; return t; };
const heading = (parent, s) => T(parent, 259, 186, s, { "font-weight": 600, "letter-spacing": .6, "fill-opacity": .4 });
const row = (parent, i, label, value, { cur = false, dim = false, lockIt = false, y0 = 192 } = {}) => {
  const y = y0 + i * 24;
  plate(parent, 228, y + 2.25, 382, 19.5, 10.9, true, null, 0, cur);
  T(parent, 259, y + 15.6, label, { "font-size": 11.3, "fill-opacity": dim ? .35 : .8 });
  if (value) T(parent, 468, y + 15.6, value, { "font-size": 11.3, fill: dim ? "#fff" : VALUE, "fill-opacity": dim ? .35 : .8 });
  if (lockIt) lockGlyph(parent, 452, y + 6.6, .45);
};
const thumb = (parent, x, y, w, kind) => {
  const h = w * 480 / 854, cid = `cgc${++uid}`;
  svgEl("rect", { x, y, width: w, height: h }, svgEl("clipPath", { id: cid }, svgEl("defs", {}, parent)));
  const g = svgEl("g", { "clip-path": `url(#${cid})` }, parent);
  A.levelshot?.(svgEl("g", { transform: `translate(${x} ${y}) scale(${(w / 854).toFixed(4)}) translate(107 0)` }, g), kind, { x0: -107, y0: 0, W: 854, H: 480 });
  svgEl("rect", { x: x + .4, y: y + .4, width: w - .8, height: h - .8, fill: "none", stroke: OLIVE, "stroke-opacity": .6, "stroke-width": .8 }, parent);
  return h;
};
const progress = (parent, x, y, w, f) => {
  svgEl("rect", { x, y, width: w, height: 3, fill: "#E06C00", "fill-opacity": .3 }, parent);
  svgEl("rect", { x, y, width: w * f, height: 3, fill: "#E06C00", "fill-opacity": .5 }, parent);
};

/* Pages and sub-pages. Sections that lead deeper name their sub-page. */
const PAGES = {
  settings: { title: "SETTINGS - SYSTEM", from: "SETTINGS", backing: .6, items: ["GAME", "CONTROLS", "SYSTEM", "AUDIO", "NETWORK"], focus: 2 },
  sp: { title: "SINGLE PLAYER", from: "SINGLE PLAYER", backing: .6, items: [["CAMPAIGN", "campaign"], ["ARENA", "arena"]], focus: 0 },
  mp: { title: "MULTIPLAYER", from: "MULTIPLAYER", backing: 0, items: [["JOIN GAME", null], ["CREATE SERVER", null], ["DEMOS", "demos"], ["PLAYER SETUP", null]], focus: 2 },
};
const SUBS = {
  campaign: { parent: "sp", label: "CAMPAIGN", backing: .6, items: ["NEW CAMPAIGN", "CHAPTERS"], focus: 1 },
  arena: { parent: "sp", label: "ARENA", backing: .6, focus: 2 },
  demos: { parent: "mp", label: "DEMOS", backing: .9, focus: 0 },
};
const subTitle = k => k === "campaign" ? `CAMPAIGN - ${SUBS.campaign.items[SUBS.campaign.focus]}` : SUBS[k].label;

const pagesG = $("chgPages"), subsG = $("chgSubs");
const pageBody = {}, subBody = {};
Object.keys(PAGES).forEach(k => { pageBody[k] = svgEl("g", { style: "display:none" }, pagesG); });
Object.keys(SUBS).forEach(k => { subBody[k] = svgEl("g", { style: "display:none" }, subsG); });

function drawPage(k) {
  const g = pageBody[k], P = PAGES[k];
  g.innerHTML = "";
  if (k === "settings") {
    P.items.forEach((t, i) => plate(g, 14, 175 + i * 30, 208, 24.4, 13.7, true, t, 10.4, i === P.focus));
    heading(g, "DISPLAY");
    [["Screen mode", "Fullscreen"], ["Resolution", "2560 × 1440"], ["Refresh rate", "144 Hz"], ["Vertical sync", "On"], ["Brightness", "1.10"], ["Anti-aliasing", "4x"]].forEach(([l, v], i) => row(g, i, l, v));
    return;
  }
  P.items.forEach(([t, sub], i) => {
    const p = plate(g, 14, 175 + i * 30, 208, 24.4, 13.7, true, t, 10.4, i === P.focus);
    hit(p, () => { if (P.focus !== i) { P.focus = i; drawPage(k); } }, sub ? () => deeper(sub) : () => say(`${t} ${k === "mp" && i === 3 ? "edits in place on this page" : "opens its own sub-page in the full menu, as Demos does here"}.`));
  });
  const [name, sub] = P.items[P.focus];
  heading(g, name);
  if (k === "sp" && sub === "campaign") {
    thumb(g, 259, 197, 128, "sp");
    T(g, 400, 208, "AIR DEFENSE TRENCHES", { "font-family": F_M, "font-weight": 400, "font-size": 8.5, "fill-opacity": .9 });
    T(g, 400, 224, "Mission 2 of 30 · Corporal", { "fill-opacity": .6 });
    progress(g, 400, 236, 180, .07); T(g, 400, 252, "7% COMPLETE", { "font-size": 8, fill: VALUE, "letter-spacing": .4 });
  } else if (k === "sp") {
    thumb(g, 259, 197, 128, "mp");
    T(g, 400, 208, "TIER 3 · VETERAN", { "font-family": F_M, "font-weight": 400, "font-size": 8.5, "fill-opacity": .9 });
    T(g, 400, 224, "Next: Lockdown against six bots", { "fill-opacity": .6 });
    progress(g, 400, 236, 180, .4); T(g, 400, 252, "14 WINS · 3 LOSSES", { "font-size": 8, fill: VALUE, "letter-spacing": .4 });
  } else if (sub === "demos") {
    thumb(g, 259, 197, 128, "mp");
    T(g, 400, 208, "12 RECORDED DEMOS", { "font-family": F_M, "font-weight": 400, "font-size": 8.5, "fill-opacity": .9 });
    T(g, 400, 224, "Newest: Hangar Brawl · q4dm5", { "fill-opacity": .6 });
    T(g, 400, 240, "9:42 · recorded yesterday", { "fill-opacity": .6 });
  } else {
    const lines = { "JOIN GAME": ["248 SERVERS · 31 WITH PLAYERS", "Three favorites online"], "CREATE SERVER": ["HOST A MATCH", "Map, mode, limits and bots"],
      "PLAYER SETUP": ["VEX · MARINE", "Name, model and team color"] }[name];
    T(g, 259, 208, lines[0], { "font-family": F_M, "font-weight": 400, "font-size": 8.5, "fill-opacity": .9 }); T(g, 259, 224, lines[1], { "fill-opacity": .6 });
  }
  const blurb = { campaign: "Continue the campaign, start again at any difficulty, or replay a chapter you have reached.",
    arena: "Climb six tiers of Arena matches against bots; each tier ends in a boss match.", demos: "Watch recorded matches with the free, chase and player cameras." }[sub];
  if (blurb) { T(g, 259, 292, blurb, { "fill-opacity": .7 }); T(g, 259, 312, "Accept opens it one level down.", { "font-size": 8.5, "fill-opacity": .45 }); }
}

function drawSub(k) {
  const g = subBody[k], S = SUBS[k];
  g.innerHTML = "";
  if (k === "campaign") {
    S.items.forEach((t, i) => { const p = plate(g, 14, 175 + i * 30, 208, 24.4, 13.7, true, t, 10.4, i === S.focus); hit(p, () => { if (S.focus !== i) { S.focus = i; drawSub(k); setTitle(subTitle(k)); } }); });
    if (S.focus === 1) {
      heading(g, "CHAPTERS · 2 OF 30 REACHED");
      [["1   AIR DEFENSE BUNKER", "COMPLETE"], ["2   AIR DEFENSE TRENCHES", "IN PROGRESS"], ["3   HANGAR PERIMETER", "LOCKED"], ["4   INTERIOR HANGAR", "LOCKED"], ["5   MCC LANDING SITE", "LOCKED"], ["6   OPERATION: ADVANTAGE", "LOCKED"]]
        .forEach(([l, v], i) => row(g, i, l, v, { cur: i === 1, dim: i > 1, lockIt: i > 1 }));
    } else {
      heading(g, "DIFFICULTY");
      [["PRIVATE", "For newcomers"], ["CORPORAL", "The intended challenge"], ["SERGEANT", "For veterans"], ["LIEUTENANT", "Finish the campaign"]]
        .forEach(([l, v], i) => row(g, i, l, v, { cur: i === 1, dim: i === 3, lockIt: i === 3 }));
    }
  } else if (k === "arena") {
    // the tier ladder (section 13.7): locked tiers dimmed with the lock, the boss match on a larger plate
    [["TIER 1", 0], ["TIER 2", 0], ["TIER 3", 0], ["TIER 4", 1], ["TIER 5", 1]].forEach(([t, locked], i) => {
      const p = plate(g, 14, 175 + i * 30, 208, 24.4, 13.7, true, t, 10.4, i === S.focus, { dim: !!locked, lock: !!locked });
      if (!locked) { if (i < 2) T(g, 196, 175 + i * 30 + 16, "✓", { "font-size": 9, fill: "#B5C784", "fill-opacity": .9 }); hit(p, () => { if (S.focus !== i) { S.focus = i; drawSub(k); } }); }
    });
    plate(g, 14, 325, 208, 34, 15, true, "BOSS", 12, false, { dim: true, lock: true });
    const tier = S.focus + 1, won = tier < 3 ? 4 : 2;
    heading(g, `TIER ${tier} · ${["RECRUIT", "REGULAR", "VETERAN"][S.focus]} · ${won} OF 4 WON`);
    [["Proving Grounds · four bots"], ["The Edge · six bots"], ["Lockdown · six bots"], ["Bloodworks · eight bots"]].forEach(([l], i) =>
      row(g, i, `MATCH ${i + 1}   ${l}`, i < won ? "WON" : i === won ? "NEXT" : "LOCKED", { cur: i === won && tier === 3, dim: i > won, lockIt: i > won }));
    T(g, 259, 302, tier === 3 ? "Win the last two matches to open Tier 4." : "Tier complete.", { "fill-opacity": .6 });
  } else {
    // the demo list (section 13.7 lists): header-band filter tags, sortable columns, a detail column
    let tx = 14;
    ["ALL", "MINE", "DEATHMATCH", "TEAM"].forEach((t, i) => {
      const x = tx, w = measure(t, 6.6) + 18, on = i === 0; tx += w + 5;
      svgEl("path", { d: `M${x + 6} 156H${x + w}V170H${x}V162Z`, fill: OLIVE, "fill-opacity": on ? .49 : .18 }, g);
      svgEl("path", { d: `M${x + .5} 170V162L${x + 6} 156.5H${x + w * .7}`, fill: "none", stroke: OLIVE, "stroke-width": .9, opacity: on ? 1 : .5 }, g);
      T(g, x + 9, 165.6, t, { "font-family": F_M, "font-weight": 400, "font-size": 6.6, "fill-opacity": on ? .95 : .6 });
    });
    [["NAME", 22], ["MAP", 170], ["MODE", 242], ["LENGTH", 344]].forEach(([t, x]) => T(g, x, 186, t, { "font-weight": 600, "font-size": 7.5, "letter-spacing": .6, "fill-opacity": .4 }));
    const demos = [["Hangar Brawl", "q4dm5", "Deathmatch", "9:42"], ["Rail Only Nights", "q4dm3", "Tourney", "12:08"], ["Clan Scrim", "q4ctf1", "Capture the Flag", "20:00"],
      ["Low Grav Fun", "q4dm7", "Deathmatch", "6:31"], ["Arena Ladder", "q4dm1", "Tourney", "14:55"], ["Dead Zone Practice", "q4ctf4", "Dead Zone", "11:20"], ["Bloodworks Night", "q4dm2", "Team DM", "15:00"]];
    demos.forEach((d, i) => {
      const y = 192 + i * 22, on = i === S.focus;
      const r = svgEl("g", { class: "hit" }, g);
      svgEl("rect", { x: 14, y, width: 400, height: 20, fill: on ? "#FF9C00" : "#9A9C6E", "fill-opacity": on ? .41 : .08 }, r);
      [[d[0], 22], [d[1], 170], [d[2], 242], [d[3], 344]].forEach(([s, x], j) => T(r, x, y + 13.5, s, { "font-size": 9.3, "fill-opacity": on ? 1 : .8, fill: j === 3 ? VALUE : "#fff" }));
      r.addEventListener("pointerenter", () => { if (S.focus !== i) { S.focus = i; drawSub(k); } });
    });
    const d = demos[S.focus], h = thumb(g, 428, 192, 182, "mp");
    T(g, 428, 192 + h + 16, d[0].toUpperCase(), { "font-family": F_M, "font-weight": 400, "font-size": 8.5, "fill-opacity": .9 });
    T(g, 428, 192 + h + 32, `${d[1]} · ${d[2]}`, { "fill-opacity": .6 });
    T(g, 428, 192 + h + 46, `${d[3]} · eight players`, { "fill-opacity": .6 });
    ["PLAY", "RENAME", "DELETE"].forEach((t, i) => plate(g, 428, 192 + h + 58 + i * 26, 182, 22, 11, true, t, 8.6, i === 0));
  }
}

/* Home navigation: Single Player, Multiplayer and Settings lead to pages. */
const home = $("chgHome"), nav = $("chgNav");
const HOME = [["CONTINUE", null], ["SINGLE PLAYER", "sp"], ["LOAD GAME", null], ["MULTIPLAYER", "mp"], ["SETTINGS", "settings"]];
let homeFocus = 1;
function drawHome() {
  nav.innerHTML = "";
  HOME.forEach(([t, page], i) => {
    const y = 204.8 + i * 30, on = i === homeFocus;
    const g = svgEl("g", page ? { class: "hit" } : {}, nav);
    plate(g, -120, y, 376, 24.4, 0, false, null);
    svgEl("path", { d: `M32 ${y + 8.2}h6v6z`, fill: on ? ORANGE : "#909A49", opacity: on ? 1 : .4 }, g);
    T(g, 44, y + 16.3, t, { "font-family": F_M, "font-weight": 400, "font-size": 11.2, fill: on ? ORANGE : "#fff", "fill-opacity": on ? 1 : .8 });
    if (page) { g.addEventListener("pointerenter", () => { if (homeFocus !== i && level === "home") { homeFocus = i; drawHome(); } }); g.addEventListener("click", () => toPage(page)); }
  });
}
drawHome();
const links = $("chgLinks");
svgEl("path", { d: "M436 54H718L734 70V75H444L436 67Z", fill: "#3E4A21", "fill-opacity": .3 }, links);
[["MODS", 450], ["UPDATES", 506], ["CREDITS", 580], ["EXIT", 654]].forEach(([t, x]) => {
  svgEl("path", { d: `M${x} 59.5h4.5v4.5z`, fill: "#909A49", opacity: .4 }, links);
  T(links, x + 8, 65.5, t, { "font-family": F_M, "font-weight": 400, "font-size": 7.4, "fill-opacity": .5 });
});
const backG = $("chgBack");
const backPlate = plate(backG, 558, 58.3, 109, 23.4, 13.1, true, "BACK", 9.3, false);
backPlate.setAttribute("class", "hit"); backPlate.addEventListener("click", () => goBack());
const hit = (g, enter, click) => { g.setAttribute("class", "hit"); if (enter) g.addEventListener("pointerenter", () => enter()); g.addEventListener("click", () => click && click()); };

/* State. Level is home, page or sub; animation values live in cur. */
let level = "home", pageK = null, subK = null;
const cur = { topX: -400, topY: 0, botX: -399, botY: 351, homeOp: 1, linksOp: 1, homeX: 0, markOp: .85, pageOp: 0, pageX: 0, subOp: 0, subX: 0,
  titleOp: 0, crumbOp: 0, crumbY: 29, crumbS: 8.4, backingOp: 0, backOp: 0 };
const setTitle = s => { $("chgTitle").textContent = s; };
const apply = () => {
  $("chgTop").setAttribute("transform", `translate(${cur.topX} ${cur.topY})`);
  $("chgBot").setAttribute("transform", `translate(${cur.botX} ${cur.botY})`);
  nav.setAttribute("opacity", cur.homeOp); links.setAttribute("opacity", cur.linksOp);
  home.setAttribute("transform", `translate(${cur.homeX} 0)`);
  $("chgMark").setAttribute("opacity", cur.markOp);
  pagesG.setAttribute("opacity", cur.pageOp); pagesG.setAttribute("transform", `translate(${cur.pageX} 0)`);
  subsG.setAttribute("opacity", cur.subOp); subsG.setAttribute("transform", `translate(${cur.subX} 0)`);
  $("chgBackingRect").setAttribute("opacity", cur.backingOp);
  $("chgTitle").setAttribute("fill-opacity", cur.titleOp); backG.setAttribute("opacity", cur.backOp);
  const c = $("chgCrumb"); c.setAttribute("fill-opacity", cur.crumbOp); c.setAttribute("y", cur.crumbY.toFixed(2)); c.setAttribute("font-size", cur.crumbS.toFixed(2));
  [nav, links, backG, pagesG, subsG].forEach(g => g.style.pointerEvents = "");
  nav.style.pointerEvents = level === "home" ? "" : "none"; pagesG.style.pointerEvents = level === "page" ? "" : "none"; subsG.style.pointerEvents = level === "sub" ? "" : "none";
  backG.style.pointerEvents = level === "home" ? "none" : "";
};

/* The choreography of each change (section 8). Values are filled in per destination. */
const bands = s => [...STATES[s].top, ...STATES[s].bot];
const CHOREO = {
  page: P => [
    { label: "content.out", keys: ["homeOp"], to: [0], t0: 0, dur: 250 },
    { label: "links, plinth", keys: ["linksOp"], to: [0], t0: 0, dur: 50 },
    { label: "light.up (out)", keys: ["markOp"], to: [0], t0: 0, dur: 250 },
    { label: "frame.dock", keys: ["topX", "topY", "botX", "botY"], to: bands("page"), t0: 50, dur: 500, ease: "accel" },
    { label: "screen.depart", keys: ["homeX"], to: [640], t0: 50, dur: 500, ease: "accel" },
    { label: "page plates", keys: ["pageOp", "pageX"], to: [1, 0], t0: 550, dur: 0 },
    { label: "content.in", keys: ["titleOp", "backingOp", "backOp"], to: [.5, P.backing, 1], t0: 550, dur: 150 },
  ],
  home: () => [
    { label: "title", keys: ["titleOp", "crumbOp"], to: [0, 0], t0: 0, dur: 0 },
    { label: "Back action", keys: ["backOp"], to: [0], t0: 0, dur: 100 },
    { label: "backing", keys: ["backingOp"], to: [0], t0: 0, dur: 250 },
    { label: "screen.return", keys: ["pageX"], to: [-640], t0: 0, dur: 300 },
    { label: "frame.dock", keys: ["topX", "topY", "botX", "botY"], to: bands("home"), t0: 0, dur: 500, ease: "accel" },
    { label: "home re-placed", keys: ["homeX"], to: [0], t0: 0, dur: 0 },
    { label: "content.in", keys: ["homeOp", "linksOp", "pageOp"], to: [1, 1, 0], t0: 500, dur: 150 },
    { label: "light.up (in)", keys: ["markOp"], to: [.85], t0: 500, dur: 150 },
  ],
  deeper: S => [
    { label: "page plates out", keys: ["pageOp"], to: [0], t0: 0, dur: 150 },
    { label: "page sweep", keys: ["pageX"], to: [640], t0: 0, dur: 300 },
    { label: "title.crumb", keys: ["crumbOp", "crumbY", "crumbS"], to: [.4, 22, 6.6], t0: 0, dur: 150 },
    { label: "frame.step", keys: ["topX"], to: [STATES.sub.top[0]], t0: 50, dur: 300, ease: "accel" },
    { label: "sub-page plates", keys: ["subOp", "subX"], to: [1, 0], t0: 350, dur: 0 },
    { label: "content.in", keys: ["titleOp", "backingOp"], to: [.5, S.backing], t0: 350, dur: 150 },
  ],
  up: P => [
    { label: "title", keys: ["titleOp"], to: [0], t0: 0, dur: 0 },
    { label: "screen.return", keys: ["subX"], to: [-640], t0: 0, dur: 300 },
    { label: "frame.step", keys: ["topX"], to: [STATES.page.top[0]], t0: 0, dur: 300, ease: "accel" },
    { label: "title.crumb (back)", keys: ["crumbOp", "crumbY", "crumbS"], to: [.5, 29, 8.4], t0: 150, dur: 150 },
    { label: "page re-placed", keys: ["pageX"], to: [0], t0: 0, dur: 0 },
    { label: "content.in", keys: ["pageOp", "subOp", "backingOp"], to: [1, 0, P.backing], t0: 300, dur: 150 },
  ],
};
const CAPS = {
  page: "Home to page: the home plates and message fade over 250 ms, the links and plinth over 50 ms, and the wordmark goes to black. From 50 ms the bands dock and the home layer sweeps 640 u toward the trailing edge, both on accel(250, 250). At 550 ms the page appears with its plates at rest; its title, backing and Back fade in over 150 ms.",
  home: "Back to home: the title drops at once, Back fades over 100 ms and the backing over 250 ms while the page sweeps 640 u toward the leading edge in 300 ms. The bands start home at once on accel(250, 250). At 500 ms the home content fades in over 150 ms and the wordmark lights up from black.",
  deeper: "Page to sub-page: the page's plates fade over 150 ms while the page sweeps 640 u toward the trailing edge over 300 ms, as the stock did within the Multiplayer screen. The page title rises 7 u into the band's thin span, steps down to 80% and dims to 0.40. From 50 ms only the top band steps one notch pitch, 97 u, on accel(150, 150), and its step slides under the crumb. At 350 ms the sub-page appears with its title at the step's foot. The bottom band and Back never move.",
  up: "Sub-page to page: the sub-page's title drops at once and the sub-page sweeps 640 u toward the leading edge over 300 ms while the top band steps back. Once the step has passed back under it, the crumb returns to the title slot over 150 ms. At 300 ms the page fades in over 150 ms with the focus on the section that led down.",
};

let run = null, speed = 1, reduced = matchMedia("(prefers-reduced-motion: reduce)").matches, carryOn = false, running = null;
const tl = $("tl"), cap = $("chgCap");
function drawTimeline(kind, entries) {
  tl.innerHTML = "";
  entries.forEach(e => {
    const n = document.createElement("span"); n.textContent = e.label; tl.appendChild(n);
    const trk = document.createElement("span"); trk.className = "trk";
    const b = document.createElement("span"); b.className = "bar" + (e.ease === "accel" ? " trap" : "");
    b.style.left = (e.t0 / 700 * 100) + "%"; b.style.width = `max(2px, ${e.dur / 700 * 100}%)`;
    b.style.setProperty("--bc", e.label.startsWith("frame") ? "#CCCC51" : /content\.in|\(in\)|plates$|re-placed|crumb \(back\)/.test(e.label) ? "#B5C784" : OLIVE);
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
    const c = $("chgCarry");
    if (carry && carryOn && !reduced) {                 // title.carry: the activated label travels into the title slot
      const pc = accel(Math.min(1, Math.max(0, (t - carry.t0) / carry.dur)));
      c.textContent = carry.text;
      c.setAttribute("x", carry.x0 + (carry.x1 - carry.x0) * pc); c.setAttribute("y", carry.y0 + (carry.y1 - carry.y0) * pc);
      c.setAttribute("font-size", carry.s0 + (8.4 - carry.s0) * pc); c.setAttribute("fill", pc < .5 ? ORANGE : "#FFFFFF");
      c.setAttribute("opacity", t < carry.t0 + carry.dur ? 1 : Math.max(0, 1 - (t - carry.t0 - carry.dur) / 150));
    } else c.setAttribute("opacity", 0);
    head.style.opacity = 1; head.style.left = `calc(132px + (100% - 132px) * ${Math.min(t, 700) / 700})`;
    if (t < (reduced ? 80 : total)) run = requestAnimationFrame(frame);
    else { head.style.opacity = 0; run = null; done && done(); }
  };
  run = requestAnimationFrame(frame);
}

function toPage(k) {
  if (level !== "home") return;
  level = "page"; pageK = k; homeFocus = HOME.findIndex(h => h[1] === k); drawHome();
  Object.entries(pageBody).forEach(([n, g]) => g.style.display = n === k ? "" : "none");
  drawPage(k); setTitle(PAGES[k].title);
  cur.crumbOp = 0; cur.pageX = 0;
  play("page", CHOREO.page(PAGES[k]), { carry: { text: PAGES[k].from, x0: 44, y0: 204.8 + homeFocus * 30 + 16.3, s0: 11.2, x1: 39, y1: 29, t0: 50, dur: 500 } });
  sync();
}
function deeper(k) {
  if (level !== "page") return;
  level = "sub"; subK = k;
  const P = PAGES[pageK], i = P.items.findIndex(x => x[1] === k);
  Object.entries(subBody).forEach(([n, g]) => g.style.display = n === k ? "" : "none");
  drawSub(k);
  const crumb = $("chgCrumb"); crumb.textContent = P.title;                 // the title stays where it stands, as the crumb
  cur.crumbOp = Math.max(cur.titleOp, cur.crumbOp); cur.crumbY = 29; cur.crumbS = 8.4; cur.titleOp = 0; setTitle(subTitle(k)); cur.subX = 0;
  play("deeper", CHOREO.deeper(SUBS[k]), { carry: { text: SUBS[k].label, x0: 32, y0: 175 + i * 30 + 16, s0: 10.4, x1: 136, y1: 29, t0: 50, dur: 300 } });
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
function sync() {
  go.querySelectorAll("button").forEach(b => b.remove());
  const opts = level === "home" ? [["Single player", () => toPage("sp")], ["Multiplayer", () => toPage("mp")], ["Settings", () => toPage("settings")]]
    : level === "page" ? PAGES[pageK].items.filter(x => Array.isArray(x) && x[1]).map(([t, s]) => [t[0] + t.slice(1).toLowerCase(), () => deeper(s)]) : [];
  opts.forEach(([t, fn]) => { const b = document.createElement("button"); b.className = "seg"; b.textContent = t; b.addEventListener("click", fn); go.appendChild(b); });
  if (!opts.length) { const b = document.createElement("button"); b.className = "seg"; b.disabled = true; b.textContent = level === "sub" ? "Deepest level" : "No deeper level"; go.appendChild(b); }
  $("goBack").disabled = level === "home";
  const names = level === "home" ? ["Home"] : level === "page" ? ["Home", PAGES[pageK].from] : ["Home", PAGES[pageK].from, SUBS[subK].label];
  const n = names.length;
  $("chgWhere").innerHTML = `<b>Level ${n - 1}</b><span class="lvl">${[0, 1, 2].map(i => `<i class="${i < n ? "on" : ""}"></i>`).join("")}</span>` +
    names.map(s => s[0] + s.slice(1).toLowerCase()).join(" › ") + (level === "sub" ? " · the top band has stepped one notch pitch" : "");
}
const say = s => { cap.textContent = s; };
$("goBack").addEventListener("click", goBack);
const toggle = (box, fn) => box.addEventListener("click", e => { e.stopImmediatePropagation(); const on = box.classList.toggle("on"); box.setAttribute("aria-pressed", on); fn(on); }, true);
toggle($("slowBox"), on => { speed = on ? .25 : 1; });
toggle($("rmBox"), on => { reduced = on || matchMedia("(prefers-reduced-motion: reduce)").matches; });
toggle($("carryBox"), on => { carryOn = on; });
apply(); sync(); drawTimeline("page", CHOREO.page(PAGES.sp));
})();
