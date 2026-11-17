
(() => {
const $ = id => document.getElementById(id);

/* Hover card: appears at once 6 dp from the pointer; near the trailing edge it moves to the leading side. */
const zone = $("hzone"), card = $("hcard"), kv = $("hcardKv");
if (zone) {
  let current = null;
  zone.querySelectorAll(".trow[data-card]").forEach(row => {
    row.addEventListener("pointerenter", () => {
      const [name, map, mode, players, ping] = row.dataset.card.split("|");
      kv.innerHTML = `<span>Server</span><b>${name}</b><span>Map</span><b>${map}</b><span>Mode</span><b>${mode}</b><span>Players</span><b>${players}</b><span>Ping</span><b>${ping}</b>`;
      card.classList.add("show"); current = row;
    });
    row.addEventListener("pointerleave", () => { if (current === row) { card.classList.remove("show"); current = null; } });
  });
  zone.addEventListener("pointermove", e => {
    if (!current) return;
    const z = zone.getBoundingClientRect(), x = e.clientX - z.left, y = e.clientY - z.top;
    const w = card.offsetWidth, h = card.offsetHeight;
    const left = x + 6 + w > z.width - 16 ? x - 6 - w : x + 6;
    card.style.left = Math.max(16, left) + "px";
    card.style.top = Math.min(Math.max(8, y + 6), z.height - h - 8) + "px";
  });
  /* Tooltip: 300 ms delay, 100 ms fade; 12 dp after and 15 dp below the pointer. */
  const info = $("infoBtn"), tip = $("tip"); let timer = null;
  const place = e => { const z = zone.getBoundingClientRect(); const x = e.clientX - z.left + 12, y = e.clientY - z.top + 15;
    tip.style.left = Math.min(x, z.width - tip.offsetWidth - 16) + "px"; tip.style.top = Math.min(y, z.height - tip.offsetHeight - 8) + "px"; };
  info.addEventListener("pointerenter", e => { place(e); clearTimeout(timer); timer = setTimeout(() => tip.classList.add("show"), 300); });
  info.addEventListener("pointermove", place);
  info.addEventListener("pointerleave", () => { clearTimeout(timer); tip.classList.remove("show"); });
  info.addEventListener("focus", () => { const r = info.getBoundingClientRect(); place({ clientX: r.right, clientY: r.bottom }); tip.classList.add("show"); });
  info.addEventListener("blur", () => tip.classList.remove("show"));
}

/* Modal choreography (section 8, modal.enter and modal.leave). */
const md = { scrim: $("mdScrim"), glow: $("mdGlow"), frame: $("mdFrame"), acts: $("mdActs"), title: $("mdTitle"), body: $("mdBody") };
if (md.scrim) {
  let raf = null;
  const tracks = spec => {
    cancelAnimationFrame(raf);
    const t0 = performance.now(), from = {};
    Object.keys(spec).forEach(k => from[k] = +getComputedStyle(md[k]).opacity);
    const step = now => {
      const t = now - t0; let busy = false;
      for (const [k, [start, dur, to]] of Object.entries(spec)) {
        const p = dur ? Math.min(1, Math.max(0, (t - start) / dur)) : (t >= start ? 1 : 0);
        md[k].style.opacity = from[k] + (to - from[k]) * p; if (p < 1) busy = true;
      }
      if (busy) raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
  };
  const enter = () => { ["scrim", "glow", "frame", "acts", "title", "body"].forEach(k => md[k].style.opacity = 0);
    tracks({ scrim: [0, 200, 1], glow: [0, 200, 1], frame: [0, 200, 1], title: [200, 0, 1], body: [200, 0, 1], acts: [200, 0, 1] }); };
  const leave = () => tracks({ title: [0, 0, 0], body: [0, 0, 0], acts: [0, 0, 0], scrim: [50, 250, 0], glow: [50, 250, 0], frame: [50, 200, 0] });
  $("mdEnter").addEventListener("click", enter);
  $("mdLeave").addEventListener("click", leave);
}

/* Dropdown list: opens below its value with popover.enter; selection closes it. */
const btn = $("ddBtn"), pop = $("ddPop");
if (btn) {
  const close = () => { pop.classList.remove("open"); btn.setAttribute("aria-expanded", "false"); };
  btn.addEventListener("click", e => { e.stopPropagation(); const open = pop.classList.toggle("open"); btn.setAttribute("aria-expanded", open); });
  pop.querySelectorAll(".opt").forEach(o => o.addEventListener("click", e => { e.stopPropagation();
    pop.querySelectorAll(".opt").forEach(x => x.classList.toggle("sel", x === o)); $("ddVal").textContent = o.textContent; close(); }));
  document.addEventListener("click", e => { if (!pop.contains(e.target)) close(); });
  document.addEventListener("keydown", e => { if (e.key === "Escape") close(); });
}
})();
