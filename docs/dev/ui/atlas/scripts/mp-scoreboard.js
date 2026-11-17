
(() => {
const { svgEl } = window.__atlas;
const svg = document.getElementById("sbSvg");
if (!svg) return;
const F_L = "Archivo, sans-serif", NOTE = "#FFFF8D";
const LP = (size, weight = 500) => ({ "font-family": F_L, "font-weight": weight, "font-size": size });
let rem = false, dm = false;
const T1 = [["You", "DMP", 21, 14, 4, 12, 24, "me"], ["Kaz", "DMP", 18, 12, 6, 12, 38], ["Orin", "", 15, 10, 7, 11, 51], ["Bolt", "BOT", 9, 6, 8, 12, 0, "bot"], ["Pike", "RSQ", 7, 5, 9, 9, 88, "dead"], ["Reed", "", 4, 3, 10, 6, 140]];
const T2 = [["Vex", "STG", 24, 16, 5, 12, 31], ["Nyx", "STG", 19, 13, 6, 12, 47], ["Tal", "", 12, 8, 8, 10, 66], ["Juno", "", 10, 7, 9, 12, 95], ["Ash", "ASH", 6, 4, 10, 8, 120], ["Moss", "", 2, 1, 12, 3, 260]];
const SPEC = [["Quill", "", 0, 0, 0, 5, 44]];
const bars = ping => ping <= 60 ? 4 : ping <= 120 ? 3 : ping <= 200 ? 2 : 1;
const barCol = n => n >= 3 ? "#B5C784" : n === 2 ? "#E3AD36" : "#E46D56";
function shadow(parent, x, y, t, o) { svgEl("text", { ...o, x: x + 1, y: y + 1, fill: "#000" }, parent).textContent = t; svgEl("text", { ...o, x, y }, parent).textContent = t; }

function build() {
  svg.innerHTML = "";
  window.__atlas.hud.fit(svg);
  const defs = svgEl("defs", {}, svg);
  const f = svgEl("filter", { id: "sbSoft", x: "-10%", y: "-10%", width: "120%", height: "120%" }, defs); svgEl("feGaussianBlur", { stdDeviation: 9 }, f);
  svgEl("rect", { x: 72, y: 46, width: 496, height: 390, rx: 8, fill: "#000", "fill-opacity": .6, filter: "url(#sbSoft)" }, svg);
  shadow(svg, 98, 56, "openQ4 Community · " + (dm ? "Deathmatch" : "Capture the Flag"), { ...LP(10.6), fill: NOTE, "letter-spacing": -.3 });
  shadow(svg, 539, 56, "203.0.113.24:28004", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  if (rem) shadow(svg, 320, 70, "You are 3rd of " + (dm ? 12 : 13), { ...LP(10), fill: "#fff", "text-anchor": "middle" });
  const players = dm ? [...T1, ...T2].sort((a, b) => b[2] - a[2]) : null;
  const T = dm ? players.length + SPEC.length : T1.length + T2.length + SPEC.length;
  const h = dm ? (T <= 8 ? 35 : T <= 10 ? 28 : T <= 12 ? 23 : T <= 14 ? 20 : 17) : (T <= 8 ? 30 : T <= 10 ? 24 : T <= 12 ? 20 : T <= 14 ? 17 : 15);
  const cols = rem ? { name: 157, clan: 300, score: 372, kills: 408, deaths: 444, time: 480 } : { name: 157, clan: 336, score: 398, kills: 443, time: 480 };
  /* Stock panel (scoreboard.gui, mp_hud.md 6.2): the header band shows 3-24.5 u below the panel top, the column
     headings stand in the strip under it with their baseline on the first band's top edge, and the rule falls just
     inside that band. Rows start below the headings, so a heading never shares a row with an entry. */
  const LIST = 32;
  const panel = (y0, title, color, markKind, score, rows, isSpec) => {
    svgEl("path", { d: `M${93 + 7} ${y0 + 3}H557V${y0 + 24.5}H93V${y0 + 10}Z`, fill: color }, svg);
    const mg = svgEl("g", { transform: `translate(${102} ${y0 + 5.8}) scale(.667)` }, svg);
    svgEl("path", { d: markKind === "m" ? "M12 1L22 7V17L12 23L2 17V7ZM12 6L7 16H17Z" : markKind === "s" ? "M2 6L10 10L12 2L14 10L22 6L16 14L18 22L12 17L6 22L8 14Z" : "M12 2a10 10 0 1 0 .01 0zM12 7a5 5 0 1 1-.01 0z", fill: "#fff", "fill-rule": "evenodd" }, mg);
    svgEl("text", { x: 123, y: y0 + 19, ...LP(14.9), fill: "#fff" }, svg).textContent = title;
    if (score !== null) svgEl("text", { x: 539, y: y0 + 19, ...LP(14.9), fill: "#fff", "text-anchor": "end" }, svg).textContent = score;
    const hy = y0 + 33;
    const heads = [["NAME", cols.name], ["CLAN", cols.clan], ["SCORE", cols.score]];
    if (!isSpec) { if (!dm) heads.push(["KILLS", cols.kills]); if (rem) heads.push(["DEATHS", cols.deaths]); heads.push(["TIME", cols.time]); }
    heads.forEach(([t, x]) => shadow(svg, x, hy, t, { ...LP(7.7, 600), fill: "#fff", "fill-opacity": .4 }));
    shadow(svg, 537, hy, "PING", { ...LP(7.7, 600), fill: "#fff", "fill-opacity": .4, "text-anchor": "end" });
    svgEl("rect", { x: 95, y: y0 + 34.6, width: 446, height: 1.1, fill: "#fff", "fill-opacity": .1 }, svg);
    rows.forEach((r, i) => {
      const [name, clan, sc, kills, deaths, time, ping, flag] = r, y = y0 + LIST + i * h + 3;
      const me = flag === "me", dead = flag === "dead" && rem;
      svgEl("rect", { x: 93, y, width: 455, height: h - 3, fill: "#9A9C6E", "fill-opacity": me ? .29 : .08 }, svg);
      if (me && rem) svgEl("path", { d: `M85 ${y + h / 2 - 4}h7v7z`, fill: "#E38900" }, svg);
      const g = svgEl("g", { opacity: dead ? .5 : 1 }, svg), ty = y + (h - 3) / 2 + 4;
      svgEl("path", { d: `M${109} ${ty - 7}h6v5h-3l-3 3zM${111} ${ty - 5}c2 0 3-1 3-2`, fill: "none", stroke: "#8E8D70", "stroke-width": 1 }, g);
      if (dead) svgEl("path", { d: `M${128} ${ty - 9}a4 4 0 0 1 4 4c0 1.4-.7 2-1.3 2.7v1.3h-5.4V${ty - 2.3}c-.6-.7-1.3-1.3-1.3-2.7a4 4 0 0 1 4-4z`, fill: "#fff" }, g);
      shadow(g, cols.name, ty, name, { ...LP(10.6), fill: "#fff" });
      if (clan === "BOT" && rem) { svgEl("path", { d: `M${cols.clan} ${ty - 9}h22v11H${cols.clan + 4}l-4-4z`, fill: "#8B964B", "fill-opacity": .6 }, g);
        svgEl("text", { x: cols.clan + 11, y: ty - 1, "text-anchor": "middle", ...LP(7.5, 700), fill: "#fff" }, g).textContent = "BOT"; }
      else if (clan !== "BOT") shadow(g, cols.clan, ty, clan, { ...LP(10.6), fill: "#fff" });
      if (!isSpec) { shadow(g, cols.score, ty, String(sc), { ...LP(10.6), fill: "#fff" }); if (!dm) shadow(g, cols.kills, ty, String(kills), { ...LP(10.6), fill: "#fff" });
        if (rem) shadow(g, cols.deaths, ty, String(deaths), { ...LP(10.6), fill: "#fff" }); shadow(g, cols.time, ty, String(time), { ...LP(10.6), fill: "#fff" }); }
      shadow(g, 537, ty, clan === "BOT" ? "0" : String(ping), { ...LP(10.6), fill: "#fff", "text-anchor": "end" });
      if (rem && clan !== "BOT") { const n = bars(ping); for (let b = 0; b < 4; b++) svgEl("rect", { x: 502 + b * 3.4, y: ty - 3 - b * 2.2, width: 2.2, height: 3 + b * 2.2, fill: barCol(n), "fill-opacity": b < n ? 1 : .2 }, g); }
    });
    return y0 + LIST + rows.length * h + 5;       // stock: the next panel starts 37 u after the list's origin plus its rows
  };
  let y = rem ? 74 : 58;                          // stock panels start at 58; Remastered makes room for the rank line
  if (dm) { y = panel(y, "PLAYERS", "#8B964B", "q", null, players, false); panel(y, "SPECTATORS", "#999999", "q", null, SPEC, true); }
  else { y = panel(y, "MARINE", "#6AA42B", "m", 3, T1, false); y = panel(y, "STROGG", "#FF7B04", "s", 2, T2, false); panel(y, "SPECTATORS", "#999999", "q", null, SPEC, true); }
  svgEl("rect", { x: 95, y: 408, width: 446, height: 1.3, fill: "#fff", "fill-opacity": .1 }, svg);
  shadow(svg, 94, 421, dm ? "Deathmatch" : "Capture the Flag", { ...LP(10.6), fill: NOTE, "letter-spacing": -.3 });
  shadow(svg, 94, 433, dm ? "q4dm7" : "q4ctf1", { ...LP(9.6), fill: NOTE });
  shadow(svg, 539, 421, dm ? "Frag Limit: 30" : "Capture Limit: 5", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  shadow(svg, 539, 433, "Time Limit: 15", { ...LP(10.6), fill: NOTE, "text-anchor": "end" });
  svgEl("circle", { cx: 294, cy: 425, r: 7, fill: "none", stroke: NOTE, "stroke-width": 1.5 }, svg);
  svgEl("path", { d: "M294 425l3 3M291 416h6", stroke: NOTE, "stroke-width": 1.3 }, svg);
  shadow(svg, 305, 429, "7:12", { ...LP(12), fill: "#fff" });
}
const tog = (id, fn) => { const b = document.getElementById(id); b?.addEventListener("click", e => { e.stopImmediatePropagation(); const on = b.classList.toggle("on"); b.setAttribute("aria-pressed", on); fn(on); build(); }, true); };
tog("sbRem", on => { rem = on; }); tog("sbDm", on => { dm = on; });
build(); window.__atlas.hud.on(build);
})();
