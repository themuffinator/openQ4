
(() => {
const A = window.__atlas, { svgEl } = A;
const host = document.getElementById("icGroups");
if (!host || !A.ICON_META) return;
const META = A.ICON_META, names = Object.keys(META);
const CODE = { blaster: "#DDEBC3", machinegun: "#FFFF00", shotgun: "#FF8000", hyperblaster: "#0073FF", grenade: "#338F12", grenadelauncher: "#338F12", nailgun: "#99CCCC",
  rocket: "#FF3300", rocketlauncher: "#FF3300", railgun: "#00FF00", lightning: "#FFFFBA", lightninggun: "#FFFFBA", darkmatter: "#C400FF", gauntlet: "#00D9FF", fire: "#FFBF40", napalmgun: "#FFBF40", napalm: "#FFBF40",
  health: "#FFFFFF", armor: "#FFFFFF", armorshard: "#FFFFFF", mega: "#0080FF", telefrag: "#00FFA6",
  corecannon: "#FF5AC8", freeze: "#9EEBFF", freezegun: "#9EEBFF" };   // The Awakening's two new weapons (section 14.16)
const codeFor = n => { const k = n.replace(/^(gun|item|ammo|simple_item|simple_ammo|kill|veh)_/, ""); return CODE[k] || "#FFFFFF"; };
const GROUPS = [
  ["The Awakening · weapons, ammunition and powerups", n => A.AWAKENING?.icons.has(n)],
  ["Weapon glyphs · strip, wheel, pickups and kill feed", n => /^item_(blaster|machinegun|shotgun|hyperblaster|grenade|nailgun|rocket|railgun|lightning|darkmatter|gauntlet|fire)$/.test(n)],
  ["Weapon renders · behind the single-player clip count", n => /^gun_/.test(n)],
  ["Ammunition · multiplayer pickups", n => /^ammo_/.test(n)],
  ["Health and armor", n => /^item_(health|armor|armorshard|mega)/.test(n) || /health|armor/.test(n) && !/^simple/.test(n) && !/^(sb|icon)_/.test(n)],
  ["Powerups and runes", n => /^item_(quad|haste|regen|invis|ammoregen|doubler|guard|scout|deadzone|generic)/.test(n)],
  ["Flags, team and status", n => /^(sb_|icon_|flag|ctf|onectf|marine|strogg|quake|frowny|noammo|chat)/.test(n)],
  ["Kill feed marks", n => /^kill_/.test(n)],
  ["Vehicle weapons", n => /^veh_/.test(n)],
  ["World sprites · simple items", n => /^simple_/.test(n)],
];
let mode = "hud";
const tintOf = n => mode === "white" ? "#FFFFFF" : mode === "code" ? codeFor(n) : "#A8A360";
function render() {
  host.innerHTML = "";
  const used = new Set();
  GROUPS.concat([["Defaults the stock never shows", n => true]]).forEach(([title, test]) => {
    const list = names.filter(n => !used.has(n) && test(n));
    if (!list.length) return;
    list.forEach(n => used.add(n));
    const h = document.createElement("h3"); h.className = "h5"; h.textContent = `${title} · ${list.length}`; h.style.marginTop = "10px";
    const grid = document.createElement("div"); grid.className = "ic-grid";
    list.forEach(n => {
      const ic = A.ICONS[n], m = META[n] || {};
      const f = document.createElement("figure");
      const art = document.createElement("div"); art.className = "art";
      const [, , vw, vh] = ic.vb.split(/\s+/).map(Number); if (vw / vh > 1.6) art.classList.add("wide");
      const s = svgEl("svg", { viewBox: `0 0 ${36 * vw / vh} 36`, role: "img", "aria-label": m.depiction || n, style: `height:${vw / vh > 1.6 ? 30 : 36}px;width:auto;overflow:visible` });
      A.icon(s, n, { x: 0, y: 0, h: 36, color: tintOf(n), opacity: !ic.multi && mode === "hud" ? .6 : 1, glow: !!ic.glow });
      art.appendChild(s);
      const cap = document.createElement("figcaption"); cap.innerHTML = `<b>${n.replace(/_/g, " ")}</b>${(m.depiction || "").replace(/</g, "&lt;")}`;
      cap.title = m.uses || ""; f.append(art, cap); grid.appendChild(f);
    });
    host.append(h, grid);
  });
}
render();
document.getElementById("icTint")?.addEventListener("click", e => { const b = e.target.closest(".seg"); if (!b) return; mode = b.dataset.t;
  document.querySelectorAll("#icTint .seg").forEach(x => x.setAttribute("aria-pressed", x === b)); render(); });

/* Legibility: the strip glyphs at the kill feed's 16.5 dp, 24 dp and the strip's 27 dp. */
const sizes = document.getElementById("icSizes");
if (sizes) [16.5, 24, 27].forEach(px => {
  const f = document.createElement("figure"), row = svgEl("svg", { width: 12 * (px * 1.2 + 6), height: px + 4, viewBox: `0 0 ${12 * (px * 1.2 + 6)} ${px + 4}` });
  ["item_blaster", "item_machinegun", "item_shotgun", "item_hyperblaster", "item_grenade", "item_nailgun", "item_rocket", "item_railgun", "item_lightning", "item_darkmatter", "item_fire", "item_gauntlet"]
    .forEach((n, i) => { if (A.ICONS[n]) A.icon(row, n, { x: i * (px * 1.2 + 6) + px * .6, y: 2, h: px, anchor: "middle", color: codeFor(n) }); });
  const c = document.createElement("figcaption"); c.textContent = `${px} dp`; f.append(row, c); sizes.appendChild(f);
});
})();
