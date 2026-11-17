(() => {
const A = window.__atlas;
const { svgEl } = A;
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const FONT_M = "Michroma, Eurostile, sans-serif", FONT_C = "Chakra Petch, Arial Narrow, sans-serif";

/* Device glyphs in their 22 dp box, ported from tools/ui/build_retained_screens.py (face_glyph, shoulder_glyph,
   trigger_glyph, stick_glyph, menu_glyph, view_glyph) and retained_key_glyphs.py (_arrow, _mouse, paddle).
   Painted opaque white with the keycap ink; the prompt rests the whole glyph at 0.85. */
const INK = "#0A0D09", W = "#fff";
const FAMILIES = ["", "xbox", "playstation", "nintendo", "steamdeck", "generic"];
const LETTERS = { xbox: { south: "A", east: "B", west: "X", north: "Y" }, nintendo: { south: "B", east: "A", west: "Y", north: "X" },
  steamdeck: { south: "A", east: "B", west: "X", north: "Y" } };
const PS = { south: "cross", east: "circle", west: "square", north: "triangle" };
const SHOULDER = { xbox: ["LB", "RB"], playstation: ["L1", "R1"], nintendo: ["L", "R"], steamdeck: ["L1", "R1"] };
const TRIGGER = { xbox: ["LT", "RT"], playstation: ["L2", "R2"], nintendo: ["ZL", "ZR"], steamdeck: ["L2", "R2"] };
const STICK = { xbox: ["LS", "RS"], playstation: ["L3", "R3"], nintendo: ["LS", "RS"], steamdeck: ["L3", "R3"], generic: ["L3", "R3"] };
const st = (d, c, w) => `<path d="${d}" fill="none" stroke="${c}" stroke-width="${w}" stroke-linejoin="miter"/>`;
const circ = (cx, cy, r, fill, line, w) => `<circle cx="${cx}" cy="${cy}" r="${r}" fill="${fill || "none"}"${line ? ` stroke="${line}" stroke-width="${w}"` : ""}/>`;
const MARK = {
  A: "M7.4 15.5L11 6.5L14.6 15.5M8.8 12.3H13.2",
  B: "M8.2 15.5V6.5H11.3C13.7 6.5 13.7 10.9 11.3 10.9H8.2M8.2 10.9H11.8C14.5 10.9 14.5 15.5 11.8 15.5H8.2",
  X: "M7.6 6.6L14.4 15.4M14.4 6.6L7.6 15.4", Y: "M7.6 6.6L11 11.2L14.4 6.6M11 11.2V15.4",
  cross: "M7.3 7.3L14.7 14.7M14.7 7.3L7.3 14.7", square: "M7.3 7.3H14.7V14.7H7.3Z", triangle: "M11 6.3L15.5 14.5H6.5Z",
};
const mark = (m, c) => m === "circle" ? circ(11, 11, 4.5, null, c, 1.9) : st(MARK[m], c, 1.9);
/* small_letters: L R B S T Z 1 2 3, h tall, each 0.55 h wide and 0.25 h apart */
function letters(text, left, top, h, c, w) {
  const gw = h * .55, gap = h * .25; let x = left, out = "";
  for (const ch of text) {
    const b = top + h, m = top + h * .5, r = x + gw; let d = "";
    if (ch === "L") d = `M${x} ${top}V${b}H${r}`;
    else if (ch === "1") d = `M${x + gw * .2} ${top + h * .22}L${x + gw * .6} ${top}V${b}`;
    else if (ch === "2") d = `M${x} ${top + h * .25}C${x} ${top - h * .05} ${r} ${top - h * .05} ${r} ${top + h * .3}L${x} ${b}H${r}`;
    else if (ch === "3") d = `M${x} ${top + h * .1}C${x + gw * .3} ${top - h * .05} ${r} ${top} ${r} ${top + h * .25}C${r} ${m - h * .05} ${x + gw * .6} ${m} ${x + gw * .35} ${m}M${x + gw * .35} ${m}C${x + gw * .7} ${m} ${r} ${m + h * .05} ${r} ${b - h * .25}C${r} ${b} ${x + gw * .3} ${b + h * .05} ${x} ${b - h * .1}`;
    else if (ch === "S") d = `M${r} ${top + h * .15}C${r - gw * .2} ${top - h * .02} ${x} ${top} ${x} ${top + h * .25}C${x} ${m - h * .05} ${r} ${m + h * .05} ${r} ${b - h * .25}C${r} ${b} ${x + gw * .2} ${b + h * .02} ${x} ${b - h * .15}`;
    else if (ch === "T") d = `M${x} ${top}H${r}M${x + gw * .5} ${top}V${b}`;
    else if (ch === "Z") d = `M${x} ${top}H${r}L${x} ${b}H${r}`;
    else if (ch === "B" || ch === "R") {
      const bowl = r - gw * .15;
      d = `M${x} ${b}V${top}H${x + gw * .5}C${bowl} ${top} ${bowl} ${m} ${x + gw * .5} ${m}H${x}`;
      d += ch === "B" ? `M${x + gw * .5} ${m}C${r} ${m} ${r} ${b} ${x + gw * .5} ${b}H${x}` : `M${x + gw * .45} ${m}L${r} ${b}`;
    }
    out += st(d, c, w); x = r + gap;
  }
  return out;
}
const textW = (t, h) => t.length * h * .55 + (t.length - 1) * h * .25;
const topRound = (x0, y0, x1, y1, r) => `M${x0} ${y1}V${y0 + r}Q${x0} ${y0} ${x0 + r} ${y0}H${x1 - r}Q${x1} ${y0} ${x1} ${y0 + r}V${y1}Z`;
const pill = "M5.5 7Q5.5 1.5 11 1.5Q16.5 1.5 16.5 7V15Q16.5 20.5 11 20.5Q5.5 20.5 5.5 15Z";
const bars = (w, c) => [-3.2, 0, 3.2].map(dy => st(`M${11 - w / 2} ${11 + dy}H${11 + w / 2}`, c, 1.6)).join("");
function windows(span, c) {
  const o = span * .32, b = [11 - span / 2, 11 - span / 2, 11 + span / 2 - o, 11 + span / 2 - o], f = [11 - span / 2 + o, 11 - span / 2 + o, 11 + span / 2, 11 + span / 2];
  return st(`M${b[0]} ${b[3]}V${b[1]}H${b[2]}V${f[1]}M${b[0]} ${b[3]}H${f[0]}`, c, 1.4) + st(`M${f[0]} ${f[1]}H${f[2]}V${f[3]}H${f[0]}Z`, c, 1.4);
}
const chevron = (side, mid) => { const tip = side ? 11 + 2.6 : 11 - 2.6, back = side ? 11 - 1.4 : 11 + 1.4; return st(`M${back} ${mid - 3.2}L${tip} ${mid}L${back} ${mid + 3.2}`, W, 1.5); };
/* One family's button at a position: south east west north | lb rb | lt rt | ls rs | menu | view. */
function glyph(family, pos) {
  if (["south", "east", "west", "north"].includes(pos)) {
    if (family === "generic") return [["north", 11, 4.3], ["west", 4.3, 11], ["east", 17.7, 11], ["south", 11, 17.7]]
      .map(([p, x, y]) => p === pos ? circ(x, y, 3.6, W) : circ(x, y, 2.7, null, W, 1.3)).join("");
    if (family === "steamdeck") return circ(11, 11, 9.9, null, W, 1.5) + mark(LETTERS.steamdeck[pos], W);
    return circ(11, 11, 10.6, W) + mark(family === "playstation" ? PS[pos] : LETTERS[family][pos], INK);
  }
  const side = pos[1] === "b" || pos[1] === "t" || pos[1] === "s" ? (pos[0] === "r" ? 1 : 0) : 0;
  if (pos === "lb" || pos === "rb") {
    const plate = topRound(1, 4.5, 21, 17.5, 4);
    if (family === "generic") return st(plate, W, 1.3) + chevron(side, 11);
    const t = SHOULDER[family][side], l = (22 - textW(t, 6.6)) / 2, lt = 11 - 3.3;
    return family === "steamdeck" ? st(plate, W, 1.3) + letters(t, l, lt, 6.6, W, 1.4) : `<path d="${plate}" fill="${W}"/>` + letters(t, l, lt, 6.6, INK, 1.4);
  }
  if (pos === "lt" || pos === "rt") {
    const plate = topRound(2, 2, 20, 20, 7), lt = 20 - 3 - 6.6;
    if (family === "generic") return st(plate, W, 1.3) + chevron(side, lt + 3.3);
    const t = TRIGGER[family][side], l = (22 - textW(t, 6.6)) / 2;
    return family === "steamdeck" ? st(plate, W, 1.3) + letters(t, l, lt, 6.6, W, 1.4) : `<path d="${plate}" fill="${W}"/>` + letters(t, l, lt, 6.6, INK, 1.4);
  }
  if (pos === "ls" || pos === "rs") {
    const t = STICK[family][side], l = (22 - textW(t, 6.6)) / 2, rim = circ(11, 11, 10.4, null, W, 1.1);
    return ["steamdeck", "generic"].includes(family) ? rim + circ(11, 11, 7.6, null, W, 1.3) + letters(t, l, 7.7, 6.6, W, 1.3)
      : rim + circ(11, 11, 8.2, W) + letters(t, l, 7.7, 6.6, INK, 1.3);
  }
  if (pos === "menu") {
    if (family === "xbox") return circ(11, 11, 10.6, W) + bars(9, INK);
    if (family === "playstation") return `<path d="${pill}" fill="${W}"/>` + bars(6, INK);
    if (family === "nintendo") return circ(11, 11, 10.6, W) + st("M6 11H16M11 6V16", INK, 1.8);
    if (family === "steamdeck") return circ(11, 11, 9.9, null, W, 1.5) + bars(8, W);
    return st(pill, W, 1.3) + bars(6, W);
  }
  if (family === "xbox") return circ(11, 11, 10.6, W) + windows(9, INK);
  if (family === "playstation") return `<path d="${pill}" fill="${W}"/>` + st("M11 14.5V7.5M9.3 14.8L7.9 9.2M12.7 14.8L14.1 9.2", INK, 1.4);
  if (family === "nintendo") return circ(11, 11, 10.6, W) + st("M6 11H16", INK, 1.8);
  if (family === "steamdeck") return circ(11, 11, 9.9, null, W, 1.5) + windows(8, W);
  return st(pill, W, 1.3) + windows(7, W);
}
const svg22 = (inner, w = 22) => `<svg viewBox="0 0 ${w} 22" width="${w}" height="22" aria-hidden="true">${inner}</svg>`;
/* An arrow key's keycap and the mouse from above (retained_key_glyphs._arrow, _mouse). */
function arrowCap(dir) {
  const pts = [[0, -6.2], [5.2, -.6], [1.9, -.6], [1.9, 6.2], [-1.9, 6.2], [-1.9, -.6], [-5.2, -.6]];
  const turn = { up: (x, y) => [x, y], down: (x, y) => [-x, -y], left: (x, y) => [y, -x], right: (x, y) => [-y, x] }[dir];
  return `<path d="M0 0H22V22H6L0 16Z" fill="rgba(255,255,255,.85)"/><path d="M${pts.map(([x, y]) => turn(x, y)).map(([x, y]) => `${11 + x} ${11 + y}`).join("L")}Z" fill="${INK}"/>`;
}
function mouse(kind) {
  const body = `<rect x="2" y="1" width="12" height="20" rx="5.6" fill="rgba(255,255,255,.85)"/>`, seam = (d) => st(d, INK, 1.1);
  let out = body;
  if (kind === "left") out += `<path d="M7.2 2.4C5.6 2.4 3.4 4.4 3.4 7V8.6H7.2Z" fill="${INK}"/>`;
  if (kind === "right") out += `<path d="M8.8 2.4C10.4 2.4 12.6 4.4 12.6 7V8.6H8.8Z" fill="${INK}"/>`;
  out += seam("M3.2 9.6H12.8") + seam(kind === "left" || kind === "right" ? "M8 2.4V9.6" : "M8 2.4V3.6");
  if (kind === "middle") out += `<rect x="6.5" y="3.8" width="3" height="5.6" rx="1.4" fill="${INK}"/>`;
  return out;
}
const PADDLE = `<rect x="1" y="3" width="24" height="16" rx="5" fill="rgba(255,255,255,.85)"/>`;
A.mdGlyph = { glyph, svg22, letters, FAMILIES };

/* Prompt bars: the title's (SELECT, EXIT) and a page's (Accept, BACK), each prompt a button. */
let fam = 0, swap = 0;
const swapped = () => swap === 2 || (swap === 0 && fam === 3);
const KEY = { accept: "ENTER", back: "ESCAPE" };
function promptGlyph(which) {
  if (!fam) return `<span class="md-kc">${KEY[which]}</span>`;
  const f = FAMILIES[fam], pos = (which === "accept") !== swapped() ? "south" : "east";
  return svg22(glyph(f, pos));
}
const prompt = (which, verb, cls = "") => `<button class="md-pr ${cls}" tabindex="-1" title="Gives ${which === "accept" ? "Accept" : "Back"}"><span class="g">${promptGlyph(which)}</span><span class="v">${verb}</span>${cls.includes("plate") ? '<span class="u"></span>' : ""}</button>`;
// Band silhouettes traced from the 1280x720 captures (prompts-*-swapped for the title home, settings-system for a
// docked page): the backdrop shows above the bands' edges, the bands are black.
const BANDS = {
  title: [["M0 600H162L117 645H0Z", "M162 600L117 645H0"], ["M680 600H1280V652H1250L1237 665H745Z", "M680 600L745 665H1237L1250 652H1280"]],
  page: [["M0 600H1280V696L1237 653H722L677 698H385L370 685H0Z", "M0 685H370L385 698H677L722 653H1237L1280 696"]],
};
function band(svg, parts) {
  svg.innerHTML = `<defs><linearGradient id="moLight" x1="0" y1="600" x2="0" y2="700" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#1f2610"/><stop offset="1" stop-color="#0b0d07"/></linearGradient></defs>` +
    parts.map(([fill, edge]) => `<path d="${fill}" fill="url(#moLight)"/><path d="${edge}" fill="none" stroke="#626934" stroke-opacity=".45" stroke-width="2"/>`).join("");
}
const titleShot = document.querySelector("#moTitleBar"), pageShot = document.querySelector("#moPageBar");
function drawBars() {
  if (!titleShot || !pageShot) return;
  band(titleShot.querySelector("svg"), BANDS.title); band(pageShot.querySelector("svg"), BANDS.page);
  // title: the bar's right edge 18 dp in, each prompt followed by 22 dp; top 670.5 dp (the strip starts at 600)
  titleShot.querySelector(".md-in").innerHTML = `<div class="md-bar" style="right:calc(18 * var(--dp));top:calc(70.5 * var(--dp))">
    <span style="margin-right:calc(22 * var(--dp))">${prompt("accept", "Select")}</span><span style="margin-right:calc(22 * var(--dp))">${prompt("back", "Exit")}</span></div>`;
  // page: Back's 45 dp target from 958 dp (glyph at 966), Accept's target ending at 952 dp; top 661.5 dp
  pageShot.querySelector(".md-in").innerHTML = `<div class="md-bar" style="right:calc(328 * var(--dp));top:calc(61.5 * var(--dp));height:calc(45 * var(--dp))">${prompt("accept", "Change", "t45")}</div>
    <div class="md-bar" style="left:calc(958 * var(--dp));top:calc(61.5 * var(--dp));height:calc(45 * var(--dp))">${prompt("back", "Back", "t45 plate")}</div>`;
  pageShot.querySelector(".plate")?.setAttribute("tabindex", "0");
}
function bindSeg(id, set) {
  const seg = document.getElementById(id); if (!seg) return;
  seg.addEventListener("click", e => { const b = e.target.closest("button"); if (!b) return;
    seg.querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", x === b)); set(b); drawBars(); });
}
bindSeg("moFam", b => { fam = +b.dataset.f; });
bindSeg("moSwap", b => { swap = +b.dataset.s; });
drawBars();

/* The glyph table: every family at every position the menus use. */
const tbl = document.getElementById("moGlyphs");
if (tbl) {
  const cols = [["Accept", ["south"]], ["Back", ["east"]], ["West", ["west"]], ["North", ["north"]], ["Tabs", ["lb", "rb"]],
    ["Pages", ["lt", "rt"]], ["Menu", ["menu"]], ["View", ["view"]], ["Stick", ["ls"]]];
  let html = `<span class="hd">Family</span>` + cols.map(([h]) => `<span class="hd">${h}</span>`).join("");
  [["Xbox", "xbox"], ["PlayStation", "playstation"], ["Nintendo", "nintendo"], ["Steam Deck", "steamdeck"], ["Generic", "generic"]].forEach(([name, f]) => {
    html += `<span>${name}</span>` + cols.map(([, ps]) => `<span class="c">${ps.map(p => svg22(glyph(f, p))).join("")}</span>`).join("");
  });
  const kc = t => `<span class="kc2">${t}</span>`;
  html += `<span>Keyboard</span><span class="c">${kc("ENTER")}</span><span class="c">${kc("ESCAPE")}</span><span>—</span><span>—</span><span class="c">${kc("Q")}${kc("E")}</span><span>—</span><span>—</span><span>—</span><span>—</span>`;
  tbl.innerHTML = html;
}

/* Bound keys: rows as the Controls page shows them (subpages-controls capture). */
const bound = document.getElementById("moBound");
if (bound) {
  const k = t => `<span class="md-k">${t}</span>`, s = (inner, w = 22) => `<svg viewBox="0 0 ${w} 22" aria-hidden="true">${inner}</svg>`;
  // labelled kinds (stick, paddle) carry the engine's label in dark Lowpixel 10.5 dp over the art
  const named = t => `<text x="50%" y="14.8" text-anchor="middle" font-family="Archivo, Arial Narrow, sans-serif" font-weight="600" font-size="10.5" fill="${INK}">${t}</text>`;
  const stick = t => s(circ(11, 11, 10.4, null, "rgba(255,255,255,.85)", 1.1) + circ(11, 11, 8.2, "rgba(255,255,255,.85)") + named(t));
  const dpadUp = s(`<path d="M7.4 .8H14.6V7.4H21.2V14.6H14.6V21.2H7.4V14.6H.8V7.4H7.4Z" fill="rgba(255,255,255,.85)"/><path d="M8.7 2.1H13.3V8.7H8.7Z" fill="${INK}"/>`);
  const rows = [
    ["Move Forward", [k("W"), s(arrowCap("up"))], [], true],
    ["Jump", [k("SPACE"), k("INS")], [s(glyph("generic", "south")), s(PADDLE + named("P1"), 26)]],
    ["Crouch", [k("C")], [s(glyph("generic", "east")), s(PADDLE + named("P3"), 26)]],
    ["Walk", [k("LEFT SHIFT")], [stick("L3")]],
    ["Turn Left", [s(arrowCap("left"))], []],
    ["Attack", [s(mouse("left"), 16)], []],
    ["Quick Chat", [k("V")], [dpadUp]],
  ];
  bound.innerHTML = rows.map(([name, keys, pads, focus]) => {
    const chips = [...keys.map((c, i) => `<span${i === keys.length - 1 && pads.length ? ' class="gap"' : ""}>${c}</span>`), ...pads.map(c => `<span>${c}</span>`)].join("");
    return `<div class="md-row${focus ? " f" : ""}"><span>${name}</span><span class="md-chips">${chips}</span></div>`;
  }).join("");
}

/* On-screen keyboard (spec 13.5, not built): types into the field above it. */
const osk = document.querySelector(".osk");
if (osk) {
  const field = document.querySelector(".oskfield .field"), caret = field.querySelector(".caret");
  const rows = ["1234567890", "QWERTYUIOP", "ASDFGHJKLÉ", "ZXCVBNM,.-"];
  rows.forEach(r => { const row = document.createElement("div"); row.className = "r";
    [...r].forEach(ch => { const kk = document.createElement("button"); kk.className = "k" + (ch === "7" ? " f" : ""); kk.textContent = ch; kk.dataset.ch = ch; row.appendChild(kk); });
    osk.appendChild(row); });
  const last = document.createElement("div"); last.className = "r";
  [["◀", "w2", "left"], ["Space", "w5", " "], ["Del", "w2", "del"], ["Done", "w2", "done"]].forEach(([t, w, a]) => {
    const kk = document.createElement("button"); kk.className = "k " + w; kk.innerHTML = `<small>${t}</small>`; kk.dataset.act = a; last.appendChild(kk); });
  osk.appendChild(last);
  osk.addEventListener("click", e => {
    const kk = e.target.closest(".k"); if (!kk) return;
    osk.querySelectorAll(".k.f").forEach(x => x.classList.remove("f")); kk.classList.add("f");
    const text = field.firstChild;
    if (kk.dataset.ch) text.textContent = (text.textContent + kk.dataset.ch.toLowerCase()).slice(0, 18);
    else if (kk.dataset.act === " ") text.textContent = (text.textContent + " ").slice(0, 18);
    else if (kk.dataset.act === "del") text.textContent = text.textContent.slice(0, -1);
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
  const [sx, sy] = P.stick, stk = svgEl("g", { opacity: .35 }, touch);
  svgEl("circle", { cx: sx, cy: sy, r: 92, fill: "rgba(0,0,0,.35)", stroke: RAIL, "stroke-width": 1.5 }, stk);
  svgEl("circle", { cx: sx, cy: sy, r: 124, fill: "none", stroke: RAIL, "stroke-width": 1, "stroke-dasharray": "2 6" }, stk);
  svgEl("circle", { cx: P.knob[0], cy: P.knob[1], r: 36, fill: "rgba(0,0,0,.5)", stroke: "#fff", "stroke-width": 1.5 }, stk);
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
void reduce;
})();
