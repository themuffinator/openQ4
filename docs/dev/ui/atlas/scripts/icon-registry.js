
(() => {
const A = window.__atlas, { svgEl } = A;
/* Vector icon registry (section 14.15-14.16). Each entry: viewBox, markup authored in white, and whether the art
   keeps its own colors. A later part replaces these stand-ins with the full reconstructed set. */
A.ICONS = A.ICONS || {
  health: { vb: "0 0 24 24", svg: '<path d="M12 1.5a10.5 10.5 0 1 0 .01 0zM9.8 6h4.4v3.8H18v4.4h-3.8V18H9.8v-3.8H6V9.8h3.8z" fill="#FFFFFF" fill-rule="evenodd"/>' },
  armor: { vb: "0 0 24 24", svg: '<path d="M4 3h16v8c0 6-4 9-8 10.5C8 20 4 17 4 11z" fill="#FFFFFF"/>' },
  ammo: { vb: "0 0 24 24", svg: '<path d="M4 13h16v6H4zM6 8h12l2 5H4z" fill="#FFFFFF"/>' },
};
/* Draw an icon into an SVG parent at (x, y) with height h, tinted unless it keeps its own colors. */
/* Tintable art uses white or currentColor; baked multi-color art keeps its colors; additive emblems add to the scene and
   may carry their separate soft glow (section 14.16). */
let gid = 0;
/* Give every inserted drawing its own ids, so a gradient or mask never resolves to another copy's differently tinted one. */
A.uniqueIds = markup => { const k = ++gid; return markup.replace(/\bid="([^"]+)"/g, (m, id) => `id="${id}__${k}"`).replace(/url\(#([^)]+)\)/g, (m, id) => `url(#${id}__${k})`).replace(/href="#([^"]+)"/g, (m, id) => `href="#${id}__${k}"`); };
A.icon = (parent, name, { x = 0, y = 0, h = 16, color = "#FFFFFF", opacity = 1, anchor = "start", glow = false } = {}) => {
  const ic = A.ICONS[name] || A.ICONS.ammo;
  const [, , vw, vh] = ic.vb.split(/\s+/).map(Number), w = h * vw / vh;
  const ox = anchor === "middle" ? -w / 2 : anchor === "end" ? -w : 0;
  const s = svgEl("svg", { x: x + ox, y, width: w, height: h, viewBox: ic.vb, overflow: "visible", opacity, color }, parent);
  const art = A.uniqueIds(ic.multi ? ic.svg : ic.svg.replace(/#FFFFFF|#fff\b|white/gi, color));
  if (glow && ic.glow) {
    const id = "icglow" + (++gid), f = `<filter id="${id}" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="${ic.glow.sigma}"/><feColorMatrix values="0 0 0 0 ${parseInt(ic.glow.color.slice(1, 3), 16) / 255} 0 0 0 0 ${parseInt(ic.glow.color.slice(3, 5), 16) / 255} 0 0 0 0 ${parseInt(ic.glow.color.slice(5, 7), 16) / 255} 0 0 0 ${Math.min(1, ic.glow.gain * .35)} 0"/></filter>`;
    s.innerHTML = `<defs>${f}</defs><g filter="url(#${id})" style="mix-blend-mode:screen;mix-blend-mode:plus-lighter">${art}</g>${art}`;
  } else s.innerHTML = art;
  if (ic.add) s.setAttribute("style", "mix-blend-mode:screen;mix-blend-mode:plus-lighter");
  s.dataset.icon = name;
  return s;
};
})();
