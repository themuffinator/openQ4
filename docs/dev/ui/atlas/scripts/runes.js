
(() => {
const A = window.__atlas, { svgEl } = A;
/* Vector runes after the measured rune face (section 5): one full-height spine per glyph with one to three
   components (flag, branch, kick, hook), straight edges, horizontal cuts, no closed counters, stroke 0.18 of the
   cap height, backslanted 11.7 degrees (slope 0.208, top toward the leading edge), 0.09-cap gaps, word space
   0.72 cap. These are original glyphs drawn to those rules, not the stock face. Units: cap height 1, y up. */
const T = .18, R = .26;
const stem = x => [[x, 0], [x + T, 0], [x + T, 1], [x, 1]];
const flag = (y, x0, x1) => [[x0, y], [x1, y], [x1, y + T], [x0, y + T]];
const bar = (x0, y0, x1, y1) => [[x0, y0], [x0 + T, y0], [x1 + T, y1], [x1, y1]];   // horizontal-cut diagonal
const G = {
  A: [stem(R), bar(0, 0, R, .5), flag(.82, .04, R)],
  B: [stem(0), flag(.82, 0, .44), bar(.26, .41, .08, .82), flag(0, 0, .4)],
  C: [stem(R), flag(.82, 0, R), bar(0, 0, R, .34)],
  D: [stem(0), bar(.26, 0, .05, .6), flag(.82, .1, .44)],
  E: [stem(0), flag(.82, 0, .44), flag(.41, 0, .34), bar(.26, 0, .1, .3)],
  F: [stem(R), flag(.41, 0, R), bar(0, .64, R, 1)],
  G: [stem(0), flag(0, 0, .44), bar(.26, .18, .26, .6), flag(.82, .1, .36)],
  H: [stem(0), stem(R + .02), flag(.5, .1, .3)],
  I: [stem(.13), flag(.82, 0, .3), bar(.13, .3, 0, 0)],
  J: [stem(R), bar(0, 0, .1, .4), flag(.82, 0, .3)],
  K: [stem(0), bar(.26, 1, .06, .5), bar(.06, .5, .26, 0)],
  L: [stem(0), flag(0, 0, .44), bar(.26, .82, .16, .6)],
  M: [stem(0), bar(.13, 1, .2, .45), stem(R)],
  N: [stem(0), bar(.08, 1, .26, 0), flag(.82, .26, .44)],
  O: [stem(0), flag(.82, 0, .44), bar(.26, .82, .26, .3)],
  P: [stem(R), flag(.82, 0, R), bar(0, .82, 0, .45), flag(.41, 0, .2)],
  Q: [stem(0), flag(0, .1, .44), bar(.26, .5, .3, 0)],
  R: [stem(0), bar(.08, .55, .26, 1), bar(.08, .5, .26, 0)],
  S: [stem(.13), bar(0, .82, .13, 1), bar(.13, 0, .26, .18)],
  T: [stem(.13), flag(.82, 0, .44)],
  U: [stem(0), stem(R), flag(0, .1, .3)],
  V: [stem(R), bar(0, 1, .12, .4)],
  W: [stem(0), stem(R), bar(.08, .2, .2, .7)],
  X: [bar(0, 0, .26, 1), bar(.26, 0, 0, 1)],
  Y: [stem(.13), bar(.13, .55, 0, 1), bar(.13, .55, .28, 1)],
  Z: [stem(R), flag(0, 0, R), bar(0, .5, R, .82)],
  0: [stem(0), flag(.82, 0, .44), flag(0, .12, .44)],
  1: [stem(.13), bar(0, .6, .13, .82)],
  2: [stem(R), flag(0, 0, .44), bar(0, .82, R, .6)],
  3: [stem(R), flag(.82, 0, R), flag(.41, .06, R), flag(0, 0, R)],
  4: [stem(R), bar(0, .4, .2, 1), flag(.41, 0, .44)],
  5: [stem(0), flag(.82, 0, .44), bar(.26, .5, .26, 0)],
  6: [stem(0), flag(.41, 0, .38), flag(0, .1, .44)],
  7: [stem(R), flag(.82, 0, R), bar(0, 0, .1, .35)],
  8: [stem(0), stem(R), flag(.41, 0, .44)],
  9: [stem(R), flag(.82, 0, R), flag(.41, 0, R), bar(0, .5, 0, 1)],
};
const SLANT = .208, GAP = .09, SPACE = .72;
const width = ch => { const g = G[ch.toUpperCase()]; if (!g) return SPACE; let mx = 0; g.forEach(p => p.forEach(([x]) => { mx = Math.max(mx, x); })); return mx + SLANT; };
A.runeWidth = (text, cap) => [...text].reduce((w, ch, i) => w + (G[ch.toUpperCase()] ? width(ch) + (i ? GAP : 0) : SPACE), 0) * cap;
/* Draw text in runes: baseline at y, height cap, aligned by anchor. */
A.runes = (parent, text, x, y, cap, { anchor = "start", fill = "#FFCC00", opacity = 1 } = {}) => {
  const g = svgEl("g", { opacity }, parent);
  const total = A.runeWidth(text, cap);
  let cx = anchor === "middle" ? x - total / 2 : anchor === "end" ? x - total : x;
  let d = "";
  [...text].forEach((ch, i) => {
    const glyph = G[ch.toUpperCase()];
    if (!glyph) { cx += SPACE * cap; return; }
    if (i) cx += GAP * cap;
    glyph.forEach(poly => { d += "M" + poly.map(([px, py]) => `${(cx + (px + SLANT * (1 - py)) * cap).toFixed(2)} ${(y - py * cap).toFixed(2)}`).join("L") + "Z"; });
    cx += width(ch) * cap;
  });
  svgEl("path", { d, fill }, g);
  return g;
};
/* The credits' scan bar: an additive ramp brightening to a rounded head, with soft top and bottom edges.
   Drawn with its head at x = 0 and its tail running toward -len; y is its center. */
let bid = 0;
A.creditsBar = (parent, len, h, color = "#FF8000") => {
  const g = svgEl("g", { style: "mix-blend-mode:screen;mix-blend-mode:plus-lighter" }, parent), id = "cbar" + (++bid);
  const lg = svgEl("linearGradient", { id, gradientUnits: "userSpaceOnUse", x1: -len, x2: 0 }, svgEl("defs", {}, g));
  [[0, 0], [.25, .24], [.5, .55], [.75, .84], [.9, .96], [.955, 1], [1, .7]].forEach(([o, a]) => svgEl("stop", { offset: o, "stop-color": color, "stop-opacity": a }, lg));
  const cap = (hh, k) => `M${-len} ${-hh}H${-hh * k}A${hh * k} ${hh} 0 0 1 ${-hh * k} ${hh}H${-len}Z`;
  svgEl("path", { d: cap(h / 2, .6), fill: `url(#${id})`, opacity: .55 }, g);          // the soft edges
  svgEl("path", { d: cap(h * .29, .9), fill: `url(#${id})` }, g);                       // the solid core
  return g;
};
/* accel(a, d): constant acceleration then deceleration, the stock transition profile, as a fraction of distance. */
A.accelDecel = (t, a, d) => { const T2 = a + d; if (t <= 0) return 0; if (t >= T2) return 1; const v = 2 / T2;
  return t <= a ? v / (2 * a) * t * t : 1 - v / (2 * d) * (T2 - t) * (T2 - t); };
})();
