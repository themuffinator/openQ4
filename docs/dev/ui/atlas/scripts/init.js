
(() => {
const A = window.__atlas, { svgEl, hud } = A;
const svg = document.getElementById("inSvg");
if (!svg || !hud) return;
const F_M = "Michroma, Eurostile, sans-serif", F_P = "IBM Plex Mono, monospace";
const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
const ADD = "mix-blend-mode:screen;mix-blend-mode:plus-lighter";
let uid = 0; const id = p => `in${p}${++uid}`;

/* The initializing screen (section 14.17). The stock draws gfx/splashScreen: splash.tga, 512x512, stretched over the
   640x480 canvas. Its art is drawn pre-squashed, so the rings are circles on the canvas. Everything here is measured on
   the canvas, in u. The rings are concentric about the emblem's own ring. */
const CX = 319, CY = 214.5;
const TONE = { 26: "#171A09", 27: "#181B09", 28: "#181C09", 29: "#191D0A", 30: "#1A1E0A", 31: "#1C1F0A", 33: "#1D210B",
  32: "#1C200B", 35: "#1E230C", 36: "#1F240C", 37: "#1F250C", 38: "#20260C", 39: "#21270C" };
const CLEAR = "#181A08";          // openQ4's pillarbox fill under the aspect-corrected picture: 24,26,8
const INK = "#0E1000", LETTER = "#F2F0EC", GLOW = "#FF8340", STATUS = "#F09E0D";
const BAND_TURN = 180000, TEETH_TURN = 120000;   // ms per turn: the band counterclockwise, the teeth clockwise
/* The stock's startup messages (#str_104343-104351) with illustrative durations; LOADING is printed twice. */
const STEPS = [["LOADING...", 0, 700], ["LOADING...", 0, 500], ["INITIALIZING SOUND...", 1, 900], ["INITIALIZING NETWORK...", 2, 650],
  ["INITIALIZING RENDER SYSTEM...", 3, 1300], ["INITIALIZING USER INTERFACE...", 4, 900], ["LOADING GAME...", 5, 1200],
  ["INITIALIZING MENUS...", 6, 1100]];
const NSTEP = 7, HOLD = 1800, TOTAL = STEPS.reduce((s, x) => s + x[2], 0);
/* QUAKE 4 in the q4text letterforms (1024x256), traced from the texture's white core; the splash draws the same
   logotype at 0.367 u per texel from 129.4,148.8 u. */
const GLYPHS = {"Q":"M108.69 85.06 103.81 89.31 100.19 93.81 98.06 97.69 96.19 102.56 95.19 106.06 94.44 110.56 94.56 111.44 94.19 112.31 94.19 117.94 95.19 124.19 97.19 129.81 100.06 135.06 103.06 138.94 106.56 142.44 110.81 145.69 114.69 147.81 120.44 149.69 123.44 150.06 124.06 150.56 124.19 159.44 126.94 170.06 128.06 176.44 128.69 177.56 129.06 177.31 129.81 174.56 131.06 165.69 131.94 163.06 132.56 159.44 132.44 151.56 132.69 150.56 133.69 150.06 135.56 149.94 139.31 148.94 142.31 147.81 147.69 144.69 152.19 140.94 156.81 135.44 159.81 130.06 161.94 124.19 162.81 118.94 162.81 111.31 162.06 108.31 161.81 105.56 159.94 99.81 156.69 93.56 152.94 88.94 148.56 85.19 147.94 85.06 147.81 85.69 148.44 85.94 151.94 89.56 154.81 93.31 157.56 98.81 158.94 103.69 159.19 106.06 158.94 115.31 158.06 119.06 155.94 124.31 151.81 130.06 146.31 135.06 141.06 137.94 133.94 139.81 132.94 139.81 132.44 139.44 132.44 130.56 133.06 126.94 135.31 124.44 122.31 124.44 122.69 125.44 123.69 126.56 124.19 128.06 124.19 139.56 123.19 140.06 115.94 137.94 111.69 135.69 109.31 133.94 103.06 127.31 100.06 122.06 99.06 119.44 98.06 115.19 97.94 105.31 99.19 99.69 102.19 93.44 106.19 88.31 109.19 85.69 109.31 85.19Z","U":"M316.44 81.81 300.06 81.81 299.44 82.31 299.69 82.81 300.56 83.06 302.06 84.44 303.06 86.56 303.06 136.31 302.06 141.31 299.94 144.94 297.44 147.94 295.06 149.69 293.69 150.19 293.44 150.56 293.69 150.94 295.31 150.94 298.31 149.94 303.69 146.69 306.94 143.94 308.81 141.81 310.69 139.06 311.94 136.44 312.81 133.44 313.19 130.31 313.19 88.19 314.19 85.19 315.81 83.31 316.81 82.69 316.94 82.19ZM253.19 81.81 252.69 82.06 252.69 82.56 254.94 84.81 256.06 88.19 256.06 129.69 257.19 135.06 258.19 137.56 260.31 141.06 264.31 145.69 267.44 147.94 270.69 149.69 272.81 150.19 274.19 150.94 275.81 150.94 276.19 150.69 276.06 150.31 274.56 149.94 272.69 148.56 270.19 146.06 268.06 143.19 267.06 141.19 266.19 136.06 266.19 86.69 267.06 84.81 268.56 83.31 269.69 82.81 270.06 82.19 269.44 81.81Z","A":"M444.31 81.94 442.19 86.69 441.94 88.06 437.94 98.19 424.69 128.06 420.69 136.06 417.06 142.31 413.94 146.69 411.06 149.19 410.94 149.69 411.69 150.06 421.44 150.06 422.31 149.56 420.81 148.31 419.94 146.69 420.19 143.56 422.06 137.81 425.06 131.56 426.69 129.94 429.56 128.31 432.31 127.94 432.81 127.56 431.44 126.81 430.06 125.44 429.81 122.44 430.19 120.81 430.94 119.56 433.31 113.06 438.19 102.94 439.94 100.44 441.19 99.81 443.06 101.56 444.94 104.94 451.94 121.69 451.94 124.19 450.31 126.06 447.44 127.56 449.44 128.06 451.44 127.94 453.69 129.06 456.56 131.69 457.94 133.56 463.06 145.56 463.06 146.69 462.44 148.19 460.94 149.31 460.94 149.69 461.31 149.94 477.69 149.94 477.94 149.31 476.69 148.56 473.19 144.69 470.06 139.19 465.94 129.69 457.31 112.06 444.81 82.06Z","K":"M572.19 81.81 571.69 82.06 571.69 82.56 573.94 84.81 574.56 87.56 574.56 143.44 573.69 146.56 570.94 149.69 571.69 149.94 587.19 149.81 587.56 149.44 585.19 147.31 584.19 144.44 584.19 123.56 585.06 120.19 585.94 118.69 588.56 116.31 590.56 115.69 590.31 115.06 587.81 113.81 585.19 111.31 584.19 108.81 584.19 87.94 585.19 85.19 586.06 83.81 587.44 82.56 587.44 82.19 586.94 81.81ZM634.94 81.06 634.31 81.19 598.06 115.44 598.44 115.94 601.94 117.06 608.19 120.06 613.31 123.19 618.19 126.81 623.56 131.94 626.81 136.06 629.94 141.69 632.06 147.94 632.44 150.56 632.94 148.81 631.94 137.81 630.81 133.56 628.81 128.44 625.94 123.69 623.44 120.56 617.44 115.19 612.69 112.69 612.19 112.19 612.06 111.44 632.69 84.81 634.19 82.44 634.94 81.81Z","E":"M788.56 128.69 787.19 129.81 784.81 134.31 779.81 141.31 775.06 145.94 772.31 147.94 770.31 148.94 768.56 149.19 768.31 149.44 768.56 149.81 769.81 150.06 772.94 149.81 777.56 148.94 778.94 148.19 781.19 147.81 785.06 147.81 788.44 149.19ZM763.69 81.44 772.44 85.06 776.06 87.69 780.81 92.69 783.06 96.69 783.81 98.69 784.19 101.44 784.44 101.69 784.81 101.44 785.06 99.94 784.81 81.94 784.44 81.31 764.56 81.06ZM729.44 81.19 729.31 81.69 730.56 82.69 732.69 85.69 733.56 89.06 733.44 130.44 734.19 133.56 736.19 137.56 738.19 140.31 742.81 144.81 747.94 147.81 750.94 148.94 752.56 148.94 752.56 148.19 751.31 147.81 748.19 145.06 746.19 142.69 745.19 140.94 744.06 136.94 743.81 133.56 743.19 130.81 743.19 122.19 744.06 119.69 745.69 117.94 746.94 117.19 749.81 116.19 753.31 115.56 751.81 114.94 747.94 114.81 745.44 114.06 744.19 112.69 743.19 109.81 743.19 87.19 744.19 84.19 747.06 81.44 746.44 81.06Z","4":"M921.69 82.06 921.31 82.06 920.06 83.44 872.19 149.31 872.19 149.94 872.56 150.19 912.06 150.31 912.56 151.56 912.56 170.44 912.19 172.19 911.06 174.56 909.69 176.06 906.94 177.81 904.44 178.44 905.56 178.94 930.94 178.94 931.56 178.69 931.44 178.19 927.94 176.94 924.56 174.94 922.19 172.06 921.81 170.31 921.81 150.81 922.69 150.06 934.31 150.06 934.81 150.44 935.06 151.44 935.69 151.69 944.81 134.81 944.81 134.31 944.44 134.19 942.44 136.31 940.19 137.94 937.94 138.94 934.44 139.81 922.44 140.06 921.69 139.31 921.94 83.06ZM912.31 101.81 912.56 102.56 912.56 139.44 911.56 140.06 885.06 139.94 884.69 139.44 889.69 132.69 911.06 102.44Z"};
const WORDMARK = "translate(129.42 148.76) scale(.3667)";

/* Geometry: angles in degrees from the top, clockwise. */
const r2 = v => Math.round(v * 100) / 100;
const pt = (r, a) => { const t = a * Math.PI / 180; return [r2(CX + r * Math.sin(t)), r2(CY - r * Math.cos(t))]; };
const disc = r => `M${r2(CX - r)} ${CY}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0Z`;
const ring = (g, r0, r1, fill) => svgEl("path", { d: disc(r1) + disc(r0), fill, "fill-rule": "evenodd" }, g);
const sector = (r0, r1, a0, a1) => {
  const [x0, y0] = pt(r1, a0), [x1, y1] = pt(r1, a1), [x2, y2] = pt(r0, a1), [x3, y3] = pt(r0, a0);
  return `M${x0} ${y0}A${r1} ${r1} 0 0 1 ${x1} ${y1}L${x2} ${y2}A${r0} ${r0} 0 0 0 ${x3} ${y3}Z`;
};
const arcOpen = (r, a0, a1) => { const [x0, y0] = pt(r, a0), [x1, y1] = pt(r, a1); return `M${x0} ${y0}A${r} ${r} 0 0 1 ${x1} ${y1}`; };
/* Remastered accents: the sweep turns clockwise once every 10 s; the halo breathes over 6 s; the glint crosses the
   lettering in 900 ms every 9 s; embers rise for 6-11 s each. */
const SWEEP_TURN = 10000, BREATHE = 6000, GLINT_EVERY = 9000, GLINT_DUR = 900, KEY_LIGHT = -38;
const MOTES = Array.from({ length: 14 }, (_, k) => {
  const h = n => { const x = Math.sin((k + 1) * 12.9898 + n * 78.233) * 43758.5453; return x - Math.floor(x); };
  const a = h(1) * 2 * Math.PI, r = 170 + h(2) * 120;
  return [r2(CX + r * Math.cos(a)), r2(CY + r * Math.sin(a) * .85 + 30), r2((h(3) - .5) * 30), r2(-40 - h(4) * 70), r2(.6 + h(5) * .6), Math.round(6000 + h(6) * 5000), Math.round(h(7) * 9000)];
});
const radial = (defs, r, stops) => {
  const gid = id("rg"), g = svgEl("radialGradient", { id: gid, cx: CX, cy: CY, r, gradientUnits: "userSpaceOnUse" }, defs);
  stops.forEach(([o, c, a = 1]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, g));
  return `url(#${gid})`;
};

/* The layers, bottom to top. Each draws alone, so the tiles below and the screen share one construction. */
const LAYERS = [
  { key: "field", name: "Field", note: "#171A09, rising in soft steps to #1C1F0A by 141 u from the rings' center.",
    draw(g, defs, box) {
      svgEl("rect", { ...box, fill: TONE[26] }, g);
      svgEl("circle", { cx: CX, cy: CY, r: 169, fill: radial(defs, 169, [[0, TONE[27]], [.089, TONE[27]], [.225, TONE[28]], [.503, TONE[29]],
        [.71, TONE[30]], [.917, TONE[31]], [1, TONE[31]]]) }, g);
    } },
  { key: "inner", name: "Inner ring", note: "169–175 u, over a soft edge that lifts from 161 u.",
    draw(g, defs) {
      svgEl("path", { d: disc(169) + disc(161), "fill-rule": "evenodd", fill: radial(defs, 169, [[.953, TONE[36], 0], [1, TONE[36], 1]]) }, g);
      ring(g, 169, 175.5, TONE[36]);
    } },
  { key: "band", name: "Plate band", spin: "band", note: "175–204 u. Two framed plates, 90° wide with an 8 u frame, turn counterclockwise once every 180 s.",
    draw(g) {
      ring(g, 175.5, 204.5, TONE[37]);
      const s = svgEl("g", { "data-spin": "band" }, g);
      [0, 180].forEach(c => {
        svgEl("path", { d: sector(175.5, 183.5, c - 45, c + 45), fill: TONE[39] }, s);
        svgEl("path", { d: sector(183.5, 204.5, c - 45, c - 43), fill: TONE[39] }, s);
        svgEl("path", { d: sector(183.5, 204.5, c + 43, c + 45), fill: TONE[39] }, s);
      });
    } },
  { key: "rim", name: "Rim", note: "204–210 u, one step brighter than the band, fading into the teeth ring by 222 u.",
    draw(g, defs) {
      ring(g, 204.5, 210.5, TONE[38]); ring(g, 210.5, 222, TONE[30]);
      svgEl("path", { d: disc(222) + disc(210.5), "fill-rule": "evenodd", fill: radial(defs, 222, [[.948, TONE[37], 1], [.962, TONE[37], .85],
        [.973, TONE[37], .3], [.982, TONE[37], .14], [1, TONE[37], 0]]) }, g);
    } },
  { key: "teeth", name: "Teeth", spin: "teeth", note: "211–247 u. Eight teeth, 22.5° wide on a 45° pitch, turn clockwise once every 120 s.",
    draw(g) {
      ring(g, 222, 247, TONE[30]); ring(g, 236.5, 245, TONE[31]);
      const s = svgEl("g", { "data-spin": "teeth" }, g);
      for (let k = 0; k < 8; k++) {
        const a0 = k * 45 - 11.25, a1 = k * 45 + 11.25;
        [[204.5, 210.5, 39], [210.5, 213.5, 38], [213.5, 216.5, 36], [216.5, 233.5, 35], [233.5, 237, 36], [237, 244, 33], [244, 247.5, 32]]
          .forEach(([r0, r1, tn]) => svgEl("path", { d: sector(r0, r1, a0, a1), fill: TONE[tn] }, s));
      }
    } },
  { key: "grooves", name: "Grooves", note: "247–300 u: a dark groove at 273–283 u between two lighter rings.",
    draw(g) {
      [[247.5, 264.8, 30], [264.8, 271.8, 29], [271.8, 273, 28], [273, 283.5, 27], [283.5, 285.5, 28], [285.5, 295.5, 29], [295.5, 297.5, 28],
        [297.5, 300.5, 27]].forEach(([r0, r1, tn]) => ring(g, r0, r1, TONE[tn]));
    } },
  { key: "clamps", name: "Clamps", note: "Fixed shade at 3 and 9 o'clock, 80 u tall from 183 u, darker again over the grooves from 246 u.",
    draw(g) {
      [[183, 272, 174.5, 80, .03], [246, 272, 177, 75, .035]].forEach(([r0, r1, y, h, o]) => [1, -1].forEach(side =>
        svgEl("rect", { x: side > 0 ? CX + r0 : CX - r1, y, width: r1 - r0, height: h, fill: "#000", "fill-opacity": o }, g)));
    } },
  { key: "sweep", name: "Light sweep", remastered: true, note: "A warm light turns clockwise round the rings once every 10 s. Its leading edge catches the rims; its 60° tail fades out behind it, added in #5E6E24 at up to 0.26.",
    draw(g) {
      const s = svgEl("g", { "data-sweep": "1", style: ADD, transform: `rotate(${KEY_LIGHT} ${CX} ${CY})` }, g);
      // the light rises over its first 7.5 degrees, then fades through a 60-degree tail; the grooves take half of it
      const amp = k => k < 3 ? (k + 1) / 4 : Math.pow(1 - (k - 3) / 24, 1.6);
      for (let k = 0; k < 27; k++) {
        svgEl("path", { d: sector(161, 250, -(k + 1) * 2.5, -k * 2.5 + .02), fill: "#5E6E24", "fill-opacity": r2(.26 * amp(k)) }, s);
        svgEl("path", { d: sector(250, 300, -(k + 1) * 2.5, -k * 2.5 + .02), fill: "#5E6E24", "fill-opacity": r2(.13 * amp(k)) }, s);
      }
      [[175.5, .5], [204.5, .7], [210.5, .45], [300, .25]].forEach(([r, a]) => svgEl("path", { d: arcOpen(r, -14, -5), fill: "none", stroke: "#C9D98A", "stroke-opacity": a, "stroke-width": 1.2, "stroke-linecap": "round" }, s));
    } },
  { key: "embers", name: "Embers", remastered: true, note: "Fourteen sparks, 0.6–1.2 u in #FFB070, rise slowly through the rings, each fading in and out over 6–11 s.",
    draw(g) {
      const m = svgEl("g", { style: ADD }, g);
      MOTES.forEach(([x, y, dx, dy, r, dur, ph]) => svgEl("circle", { cx: r2(x + dx * .5), cy: r2(y + dy * .5), r, fill: "#FFB070", "fill-opacity": .4, "data-m": [x, y, dx, dy, dur, ph].join(",") }, m));
    } },
  { key: "halo", name: "Halo", remastered: true, note: "A warm halo behind the emblem, #FF8340 added at up to 0.10 and breathing over 6 s, so the dark emblem stands clear of the rings.",
    draw(g, defs) {
      const h = svgEl("g", { "data-breathe": "1", style: ADD }, g);
      svgEl("circle", { cx: CX, cy: CY, r: 168, fill: radial(defs, 168, [[0, GLOW, .1], [.45, GLOW, .07], [.8, GLOW, .02], [1, GLOW, 0]]) }, h);
    } },
  { key: "emblem", name: "Emblem", note: "The traced q4logo in #0E1000, in its 260 u box centered on the canvas.",
    draw(g) { if (A.EMBLEM) svgEl("path", { d: A.EMBLEM, fill: INK }, svgEl("g", { transform: "translate(190.07 110.19) scale(1.0015)" }, g)); } },
  { key: "glow", name: "Glow", lit: true, view: "150 160 340 72", note: "The letters dilated 5.5 u, blurred with a 2.25 u sigma and added in #FF8340 at 0.10.",
    draw(g, defs) {
      const fid = id("gl");
      const f = svgEl("filter", { id: fid, x: -80, y: -80, width: 1184, height: 416, filterUnits: "userSpaceOnUse", "color-interpolation-filters": "sRGB" }, defs);
      svgEl("feMorphology", { in: "SourceAlpha", operator: "dilate", radius: 15, result: "d" }, f);          // 5.5 u in texels
      svgEl("feGaussianBlur", { in: "d", stdDeviation: 6.14, result: "b" }, f);        // 2.25 u
      svgEl("feFlood", { "flood-color": GLOW, "flood-opacity": .1 }, f);
      svgEl("feComposite", { in2: "b", operator: "in" }, f);
      const w = svgEl("g", { transform: WORDMARK, filter: `url(#${fid})`, style: ADD }, g);
      Object.values(GLYPHS).forEach(d => svgEl("path", { d, fill: "#fff", "fill-rule": "evenodd" }, w));
    } },
  { key: "lettering", name: "Lettering", lit: true, view: "150 160 340 72", note: "QUAKE 4 in the q4text logotype, #F2F0EC, cap height 26 u.",
    draw(g) {
      const w = svgEl("g", { transform: WORDMARK }, g);
      Object.values(GLYPHS).forEach(d => svgEl("path", { d, fill: LETTER, "fill-rule": "evenodd" }, w));
    } },
  { key: "glint", name: "Glint", lit: true, remastered: true, view: "150 160 340 72", note: "A narrow light crosses QUAKE 4 left to right in 900 ms every 9 s, clipped to the letters.",
    draw(g, defs) {
      const cid = id("gc"), cp = svgEl("clipPath", { id: cid }, defs);
      Object.values(GLYPHS).forEach(d => svgEl("path", { d, transform: WORDMARK, "fill-rule": "evenodd" }, cp));
      const lg = id("gb"), grad = svgEl("linearGradient", { id: lg, x1: 0, x2: 1, y1: 0, y2: 0 }, defs);
      [[0, "#FFFFFF", 0], [.5, "#FFFFFF", .85], [1, "#FFFFFF", 0]].forEach(([o, c, a]) => svgEl("stop", { offset: o, "stop-color": c, "stop-opacity": a }, grad));
      svgEl("rect", { "data-glint": "1", x: 330, y: 140, width: 30, height: 120, fill: `url(#${lg})`, transform: "skewX(-22)" }, svgEl("g", { "clip-path": `url(#${cid})`, style: ADD }, g));
    } },
  { key: "status", name: "Status", lit: true, view: "215 395 210 48", note: "Marine 16 dp in #F09E0D at 418 u, and a rule with one segment per step.",
    draw(g, defs, box, classic) { return statusLine(g, classic); } },
];

function statusLine(g, classic) {
  const text = svgEl("text", classic
    ? { x: 320, y: 422.6, "text-anchor": "middle", "font-family": F_P, "font-weight": 600, "font-size": 13, "letter-spacing": .2, fill: STATUS }
    : { x: 320, y: 422.5, "text-anchor": "middle", "font-family": F_M, "font-size": 10.7, "letter-spacing": 1.3, fill: STATUS }, g);
  const segs = [];
  if (!classic) {
    const W = 168, gap = 2, sw = (W - gap * (NSTEP - 1)) / NSTEP;
    for (let i = 0; i < NSTEP; i++) segs.push(svgEl("rect", { x: r2(236 + i * (sw + gap)), y: 431, width: r2(sw), height: 1.5, fill: STATUS, "fill-opacity": .15 }, g));
  }
  return { text, segs, shown: -1 };
}

/* One clock for every turning layer on the sheet: angle = base + (now - ref) x speed, so a speed change never jumps. */
const clock = { base: 0, ref: performance.now(), speed: 1 };
const turning = now => reduce ? 0 : clock.base + (now - clock.ref) * clock.speed;
const elapsed = now => hud.classic ? 0 : turning(now);          // Classic never turns; the tiles are always Remastered
const spinTo = (root, t) => root.querySelectorAll("[data-spin]").forEach(s => {
  const turn = s.dataset.spin === "band" ? -t / BAND_TURN : t / TEETH_TURN;
  s.setAttribute("transform", `rotate(${r2((turn % 1) * 360)} ${CX} ${CY})`);
});

/* The screen. */
let status = null, seq0 = performance.now(), last = 0;
function build() {
  svg.innerHTML = "";
  const lay = hud.fit(svg), classic = hud.classic, defs = svgEl("defs", {}, svg);
  const scene = svgEl("g", {}, svg);
  if (document.getElementById("inReveal")?.getAttribute("aria-pressed") === "true") scene.setAttribute("filter", "url(#inRevealF)");
  if (classic) svgEl("rect", { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H, fill: CLEAR }, scene);
  const box = classic ? { x: 0, y: 0, width: 640, height: 480 } : { x: lay.x0, y: lay.y0, width: lay.W, height: lay.H };
  const pic = svgEl("g", classic ? { "clip-path": `url(#${clip(defs)})` } : {}, scene);
  LAYERS.forEach(L => { if (L.remastered && classic) return;             // Classic keeps the stock picture
    const out = L.draw(svgEl("g", { "data-layer": L.key }, L.lit ? svg : pic), defs, box, classic); if (L.key === "status") status = out; });
  hud.regionsLayer(svg, lay);
  spinTo(svg, elapsed(performance.now())); last = 0;
  document.getElementById("inStatus").textContent = classic
    ? "Classic: the stock picture on the 4:3 canvas, pillarboxed in #181A08 by openQ4's aspect correction, with the console-font status line; nothing turns."
    : reduce ? "Remastered with reduced motion: every layer holds still." : "Remastered: the band and the teeth turn in opposite directions under the fixed emblem and lettering.";
}
function clip(defs) {
  const cid = id("cl"); svgEl("rect", { x: 0, y: 0, width: 640, height: 480 }, svgEl("clipPath", { id: cid }, defs)); return cid;
}
function accents(root, now) {
  root.querySelectorAll("[data-sweep]").forEach(s => s.setAttribute("transform", `rotate(${reduce ? KEY_LIGHT : r2(((now % SWEEP_TURN) / SWEEP_TURN) * 360)} ${CX} ${CY})`));
  root.querySelectorAll("[data-breathe]").forEach(h => h.setAttribute("opacity", reduce ? .8 : r2(.7 + .3 * Math.sin(now / BREATHE * 2 * Math.PI))));
  root.querySelectorAll("[data-glint]").forEach(r => { const p = (now % GLINT_EVERY) / GLINT_DUR;
    r.setAttribute("opacity", reduce || p > 1 ? 0 : 1); if (!reduce && p <= 1) r.setAttribute("x", r2(150 + 450 * p)); });
  root.querySelectorAll("[data-m]").forEach(c => { const [x, y, dx, dy, dur, ph] = c.dataset.m.split(",").map(Number), p = ((now + ph) % dur) / dur;
    c.setAttribute("cx", r2(x + dx * p)); c.setAttribute("cy", r2(y + dy * p)); c.setAttribute("fill-opacity", reduce ? 0 : r2(Math.sin(p * Math.PI) * .5)); });
}
function update(now) {
  if (now - last >= 33) { last = now; spinTo(svg, elapsed(now)); if (!hud.classic) accents(svg, now); }   // 30 updates a second are plenty
  if (!status) return;
  if (!hud.classic && !reduce && status.shown >= 0) {          // the active segment pulses; a new step flickers in
    const s = status.segs[STEPS[status.shown][1]]; if (s) s.setAttribute("fill-opacity", r2(.62 + .33 * (.5 + .5 * Math.sin(now / 800 * 2 * Math.PI))));
    status.text.setAttribute("opacity", r2(Math.min(1, .35 + (now - (status.at || 0)) / 120 * .65)));
  }
  let t = (now - seq0) % (TOTAL + HOLD), i = 0;
  while (i < STEPS.length - 1 && t >= STEPS[i][2]) t -= STEPS[i++][2];
  if (i === status.shown) return;
  status.shown = i; status.at = now; status.text.textContent = STEPS[i][0];
  status.segs.forEach((s, k) => s.setAttribute("fill-opacity", k < STEPS[i][1] ? .55 : k === STEPS[i][1] ? .95 : .15));
}

/* The layer tiles: each layer alone, in place on the Remastered 16:9 view. */
const host = document.getElementById("inLayers");
const tiles = LAYERS.map((L, n) => {
  const fig = document.createElement("figure");
  const t = svgEl("svg", { viewBox: L.view || "19 -85.5 600 600", role: "img", "aria-label": `${L.name} layer` }, fig);
  const defs = svgEl("defs", {}, t), g = svgEl("g", L.lit ? {} : { filter: "url(#inRevealT)" }, t);
  if (L.key !== "field") svgEl("rect", { x: -107, y: -90, width: 854, height: 660, fill: TONE[26] }, g);
  const out = L.draw(svgEl("g", {}, g), defs, { x: -107, y: -90, width: 854, height: 660 }, false);
  if (out && out.text) { out.text.textContent = "INITIALIZING SOUND..."; out.segs.forEach((s, k) => s.setAttribute("fill-opacity", k < 1 ? .55 : k === 1 ? .95 : .15)); }
  const cap = document.createElement("figcaption");
  cap.innerHTML = `<b>${String(n + 1).padStart(2, "0")} · ${L.name}</b>${L.note}`;
  fig.appendChild(cap); host?.appendChild(fig);
  return t;
});
tiles.forEach(t => { if (t.querySelector("[data-spin]")) { let tl = 0; A.tick(t, now => { if (now - tl >= 33) { tl = now; spinTo(t, turning(now)); } }); } });

build();
hud.on(build);
A.tick(svg, update);
const press = (b, on) => b.setAttribute("aria-pressed", on);
const fast = document.getElementById("inFast");
if (reduce && fast) { fast.disabled = true; fast.title = "Reduced motion holds the rings still"; }
fast?.addEventListener("click", () => {
  const now = performance.now(), on = fast.getAttribute("aria-pressed") !== "true";
  clock.base = clock.base + (now - clock.ref) * clock.speed; clock.ref = now; clock.speed = on ? 8 : 1; press(fast, on);
});
document.getElementById("inReveal")?.addEventListener("click", e => { press(e.currentTarget, e.currentTarget.getAttribute("aria-pressed") !== "true"); build(); });
document.getElementById("inReplay")?.addEventListener("click", () => { clock.base = 0; clock.ref = performance.now(); seq0 = performance.now(); build(); });
const ctl = document.getElementById("inLayoutCtl"); if (ctl) hud.controls(ctl, { scale: false });
})();
