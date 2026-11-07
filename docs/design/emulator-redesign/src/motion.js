// ── Screen motion: keyframes for the bezel glow pulse and the glare sweep ──────────────
{
  const MW = 1100, MH = 820;
  const m = figma.createFrame(); m.name = 'Screen motion'; m.resize(MW, MH);
  m.fills = [solid(C.room)]; m.clipsContent = true; figma.currentPage.appendChild(m); m.x = 1440 + 120; m.y = 1560 + 80;
  rect(m, 0, 0, MW, MH, imgFill('grain', 'FILL', 0.05), { name: 'grain' });
  text(m, 'SCREEN MOTION', 48, 40, { font: F.d7, size: 20, ls: 14, color: C.ink });
  text(m, 'Thin 4 px maroon-red bezel with a subtle iridescent gradient. Two quiet loops: the bezel glow breathes, and now and then a soft glare passes over the glass.', 48, 72, { font: F.m4, size: 12, lh: 18, color: C.muted, w: MW - 96 });
  const plate = (y, h) => frame(m, 48, y, MW - 96, h, { fill: [solid('#18191b'), imgFill('speckle-device'), SHEEN], r: 14, stroke: '#07080a', fx: [inner('#ffffff', 0.12, 0, 1, 0)], clip: true, name: 'plastic plate' });
  const cap = (p, t, x, y, col) => text(p, t, x, y, { font: F.m4, size: 11, color: col || C.etch });
  // glow pulse
  text(m, 'GLOW PULSE', 48, 120, { font: F.d7, size: 12, ls: 20, color: C.dim });
  const p1 = plate(146, 290);
  [[0, 'rest · 0 s'], [1, 'peak · 1.2 s'], [0, 'rest · 2.4 s']].forEach(([g, l], i) => { crt(p1, 40 + i * 330, 30, 210, { glow: g }); cap(p1, l, 40 + i * 330, 252); });
  text(m, '2.4 s loop, ease-in-out · glow opacity 0.30 → 0.60 · blur 6 → 11 px · colour #B3202C', 48, 446, { font: F.m4, size: 11, color: C.muted });
  // glare
  text(m, 'GLARE SWEEP', 48, 486, { font: F.d7, size: 12, ls: 20, color: C.dim });
  const p2 = plate(512, 250);
  [[0.3, 'enter · 0.2 s'], [0.5, 'mid · 0.55 s'], [0.7, 'exit · 0.9 s']].forEach(([t, l], i) => { crt(p2, 40 + i * 330, 24, 190, { glow: 0.3, glare: t }); cap(p2, l, 40 + i * 330, 224); });
  text(m, 'Every 7–12 s (random) · 1.1 s sweep, top-left → bottom-right · band peak opacity 0.16 · nothing between sweeps', 48, 772, { font: F.m4, size: 11, color: C.muted });
}
