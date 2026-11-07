// ── Desktop · 1440 ───────────────────────────────────────────────────────────
const W = 1440, X0 = 64, CW = 1312;
let H = 2400;
const page = figma.createFrame(); page.name = 'Emulator — Desktop 1440'; page.resize(W, H);
const grainLayer = null;
page.fills = [solid(C.room)]; page.clipsContent = true; figma.currentPage.appendChild(page); page.x = 0; page.y = 0;
const GRAIN = rect(page, 0, 0, W, H, imgFill('grain', 'FILL', 0.05), { name: 'grain' });

// ── Header: dark brushed-aluminium nameplate ──────────────────────────────────────────────
{
  const h = frame(page, 0, 0, W, 88, { fill: [solid(C.metal), imgFill('metal-header'), SHEEN], stroke: '#000000', fx: [inner('#cfe0ff', 0.14, 0, 1, 0), drop('#000000', 0.7, 0, 4, 14)], clip: true, name: 'Header · nameplate' });
  rect(h, 0, 86, W, 2, solid('#000000', 0.55), { name: 'lower lip' });
  [[18, 18], [W - 18, 18], [18, 70], [W - 18, 70]].forEach(([x, y]) => screw(h, x, y, 11));
  const logo = rect(h, X0 - 6, 24, 189, 34.5, imgFill('ravel-logo-light', 'FIT', 0.92), { name: 'Ravel wordmark' });
  rect(h, X0 + 196, 26, 1, 36, solid('#000000', 0.6), { name: 'divider' }); rect(h, X0 + 197, 26, 1, 36, solid('#cfe0ff', 0.08), { name: 'divider lip' });
  etched(h, 'FM-1 EMULATOR', X0 + 212, 30, { size: 12, ls: 20 });
  text(h, 'firmware 1.22.2 · runs in your browser', X0 + 212, 48, { font: F.m4, size: 11, color: '#7b848b' });
  // sunken status window
  const st = readout(h, 560, 22, 380, 44, null, 'READY · PRESS PLAY OR TAP A KEY', { vsize: 12, vls: 4, name: 'status readout' });
  lamp(st, 362, 22, C.ph, true, 9);
  // nav
  let nx = 968;
  for (const [l, w] of [['Manual', 88], ['Software', 98], ['Source', 84]]) { wbtn(h, nx, 26, w, 36, l); nx += w + 8; }
  wbtn(h, nx, 26, 148, 36, 'Install on FM-1', { state: 'prime', lamp: C.ph }); nx += 156;
}
// panic: a maroon mushroom under the header's right end
{
  const p = frame(page, W - X0 - 128, 100, 128, 30, { fill: lin([[0, '#7a161c'], [1, '#4a0d11']]), r: 15, stroke: '#a3161c', fx: [inner('#ffffff', 0.18, 0, 1, 0), drop('#000000', 0.6, 0, 2, 4)], name: 'Panic · stop all' });
  ctext(p, 'PANIC · ALL OFF', 64, 7, { font: F.d7, size: 11, ls: 14, color: '#f2dede' });
  text(page, 'Esc lets go of every held key and note', W - X0 - 128 - 240, 107, { font: F.m4, size: 10, color: C.dim });
}

// ── Left rack ────────────────────────────────────────────────────────────────
const RX = X0, RW = 288, TOP = 144;
let RY0 = TOP;
{
  // SESSION
  const s = module(page, RX, RY0, RW, 296, 'Session', { lamp: C.ph, right: 'PLAYING' });
  readout(s, 14, 44, RW - 28, 62, 'Project', 'RAVEL', { vsize: 24, right: 'DEMO · UNSAVED', name: 'project readout' });
  const sel = frame(s, 14, 116, 164, 34, { fill: solid('#0f1110'), r: 5, stroke: C.line2, name: 'select · demo' });
  text(sel, 'Demo: Ravel', 10, 9, { font: F.d6, size: 13, color: C.ink });
  path(sel, 'M 0 0 L 5 5 L 10 0', 142, 14, { stroke: C.muted, sw: 1.5 });
  wbtn(s, 186, 116, 88, 34, 'Load');
  wbtn(s, 14, 162, 126, 52, 'Play', { state: 'prime', lamp: C.ph, size: 17 });
  wbtn(s, 148, 162, 126, 52, 'Record', { lamp: C.marHi, lampOn: false, size: 17 });
  readout(s, 14, 226, 126, 56, 'Position', '012.3.2', { vsize: 16 });
  readout(s, 148, 226, 126, 56, 'Tempo', '112.0', { vsize: 16, right: 'BPM' });

  // TRACK
  RY0 += 296 + 16;
  const t = module(page, RX, RY0, RW, 168, 'Track', { right: 'T1 · SYNTH · VA' });
  const kinds = ['SYN', 'SYN', 'SYN', 'SYN', 'STK', 'STK', 'PRC', 'PRC'];
  const lv = [0.82, 0.55, 0.30, 0.64, 0.40, 0.22, 0.90, 0.48];
  for (let i = 0; i < 8; i++) {
    const cx = 14 + (i % 4) * 66, cy = 44 + Math.floor(i / 4) * 60;
    const on = i === 0;
    const c = frame(t, cx, cy, 60, 52, { fill: lin([[0, on ? '#2a1013' : '#1d201e'], [1, on ? '#1a0a0c' : '#151716']]), r: 6, stroke: on ? C.marHi : C.line2, sw: on ? 1.5 : 1, fx: [drop('#000000', 0.5, 0, 1, 2)], name: `track T${i + 1}` });
    text(c, `T${i + 1}`, 8, 6, { font: F.d7, size: 15, color: on ? '#ffffff' : C.ink });
    text(c, kinds[i], 8, 25, { font: F.m4, size: 9, color: i >= 6 ? '#c7555b' : C.muted });
    rect(c, 8, 41, 44, 3, solid('#0a0b0a'), { r: 1.5, name: 'meter bg' });
    rect(c, 8, 41, 44 * lv[i], 3, solid(C.ph), { r: 1.5, fx: [glow(C.ph, 0.6, 4)], name: 'meter' });
    lamp(c, 50, 13, C.ph, lv[i] > 0.5, 5);
  }

  // SOUND
  RY0 += 168 + 16;
  const so = module(page, RX, RY0, RW, 204, 'Sound', { right: 'BANK A' });
  readout(so, 14, 44, 126, 48, 'Kind', 'SYNTH', { vsize: 14 });
  readout(so, 148, 44, 126, 48, 'Engine', 'VA', { vsize: 14 });
  readout(so, 14, 100, RW - 28, 52, 'Sound', 'GLASS PAD 03', { vsize: 16, right: '07 / 32' });
  wbtn(so, 14, 162, 62, 30, '‹ Prev', { size: 11 }); wbtn(so, 81, 162, 62, 30, 'Next ›', { size: 11 });
  wbtn(so, 148, 162, 62, 30, 'Save…', { size: 11 }); wbtn(so, 215, 162, 59, 30, 'Delete', { size: 11 });

  // CONNECTIONS
  RY0 += 204 + 16;
  const io = module(page, RX, RY0, RW, 284, 'Connections');
  const row = (y, title, note, btn, lampColor, on) => {
    lamp(io, 22, y + 8, lampColor, on, 7);
    text(io, title, 34, y, { font: F.d7, size: 12, ls: 10, color: C.ink, upper: true });
    text(io, note, 34, y + 18, { font: F.m4, size: 10, lh: 14, color: C.muted, w: 150 });
    wbtn(io, 192, y - 2, 82, 28, btn, { size: 11 });
  };
  row(44, 'Audio in', 'a file rings strike bodies via EXT', 'Choose…', C.ph, false);
  row(96, 'MIDI in', 'notes, clock, start/stop', 'Listen', C.ph, true);
  rect(io, 14, 144, RW - 28, 1, solid(C.line), { name: 'rule' });
  text(io, 'PROJECTS & SOUNDS', 14, 156, { font: F.d7, size: 10, ls: 16, color: C.dim });
  wbtn(io, 14, 176, 126, 30, 'Export file', { size: 11 }); wbtn(io, 148, 176, 126, 30, 'Import file', { size: 11 });
  wbtn(io, 14, 212, 126, 30, 'Send to FM-1', { size: 11 }); wbtn(io, 148, 212, 126, 30, 'Get from FM-1', { size: 11 });
  text(io, 'Over the FM-1’s USB cable · Chrome or Edge', 14, 254, { font: F.m4, size: 10, color: C.dim });
  // output
  RY0 += 284 + 16;
  const out = module(page, RX, RY0, RW, 96, 'Output', { right: 'VOL 70' });
  text(out, 'VOLUME', 14, 44, { font: F.d7, size: 10, ls: 14, color: C.dim });
  rect(out, 80, 49, RW - 94, 4, solid('#0a0b0a'), { r: 2, name: 'vol track' });
  rect(out, 80, 49, (RW - 94) * 0.7, 4, lin([[0, C.phDim], [1, C.ph]], HORZ), { r: 2, fx: [glow(C.ph, 0.5, 4)], name: 'vol fill' });
  ellipse(out, 80 + (RW - 94) * 0.7, 51, 14, lin([[0, '#d9dcd8'], [1, '#8d918f']]), { stroke: '#000000', sa: 0.6, name: 'vol thumb' });
  rect(out, 14, 70, RW - 28, 10, solid('#070807'), { r: 2, name: 'out meter bg' });
  for (let i = 0; i < 30; i++) { const on = i < 21, pk = i >= 26; rect(out, 16 + i * 8.55, 72, 6.5, 6, solid(pk ? C.marHi : C.ph, on ? 1 : 0.12), { name: 'seg' }); }
  RY0 += 96;
}

// ── The device ───────────────────────────────────────────────────────────────
const DX = X0 + RW + 16, DW = CW - RW - 16, DY = TOP, DH = 612;
{
  const dv = frame(page, DX, DY, DW, DH, { fill: [solid(C.metal), imgFill('metal-device'), SHEEN], r: 18, stroke: '#07080a', sw: 1.5, fx: [inner('#d6e6ff', 0.22, 0, 1, 0), inner('#d6e6ff', 0.05, 0, 0, 0, 1), inner('#000000', 0.5, 0, -2, 3), drop('#000000', 0.8, 0, 22, 44)], clip: true, name: 'Device · FM-1 running Ravel' });
  [[16, 16], [DW - 16, 16], [16, DH - 16], [DW - 16, DH - 16]].forEach(([x, y]) => screw(dv, x, y, 12));
  etched(dv, 'FM-1', 36, 22, { size: 12, ls: 30 });
  etched(dv, '·  RAVEL', 76, 22, { size: 12, ls: 30, font: F.d5 });
  rtext(dv, 'SYNTHESIZE  ·  CATCH  ·  REBUILD', DW - 36, 22, { font: F.d7, size: 11, ls: 28, color: C.etch, fx: [drop('#000000', 0.8, 0, -1, 0)] });
  rect(dv, 30, 44, DW - 60, 1, solid('#000000', 0.7), { name: 'engraved rule' });
  rect(dv, 30, 45, DW - 60, 1, solid('#cfe0ff', 0.08), { name: 'engraved lip' });

  // left controls
  knob(dv, 82, 108, 34, 'MASTER', 'volume', 40);
  knob(dv, 186, 108, 34, 'SELECT', 'page', -10);
  knob(dv, 82, 228, 34, 'PRESETS', 'sounds · steps', -60);
  knob(dv, 186, 228, 34, 'ALGORITHM', 'track', -120);
  dbtn(dv, 46, 316, 74, 40, 'OCT −', null); dbtn(dv, 150, 316, 74, 40, 'OCT +', null);
  readout(dv, 46, 366, 178, 30, null, 'OCT 0 · T1', { vsize: 11, vls: 6, name: 'octave readout' });

  // CRT
  const SX = 258, SY = 62, SS = 340;
  const bez = frame(dv, SX, SY, SS, SS, { fill: lin([[0, '#1f2120'], [1, '#0a0b0a']]), r: 16, stroke: '#000000', stroke: '#000000', fx: [inner('#cfe0ff', 0.12, 0, 1, 0), drop('#000000', 0.7, 0, 6, 14), drop('#cfe0ff', 0.10, 0, -1, 0)], name: 'CRT bezel' });
  const glass = frame(bez, 18, 18, SS - 36, SS - 36, { fill: solid('#020302'), r: 10, fx: [inner('#000000', 1, 0, 0, 14), glow(C.ph, 0.18, 26)], clip: true, name: 'CRT glass' });
  rect(glass, 8, 8, SS - 52, SS - 52, imgFill('screen-tracks', 'FILL'), { name: 'screen · live render (TRACKS page)' });
  rect(glass, 0, 0, SS - 36, SS - 36, imgFill('scanlines', 'FILL', 0.55), { name: 'scanlines' });
  rect(glass, 0, 0, SS - 36, SS - 36, imgFill('vignette', 'FILL', 0.7), { name: 'vignette' });
  rect(glass, 0, 0, SS - 36, 70, lin([[0, '#ffffff', 0.07], [1, '#ffffff', 0]]), { name: 'glass sheen' });

  // right: knobs 1-4 and buttons
  const KX = 622, step = 90;
  ['KNOB 1', 'KNOB 2', 'KNOB 3', 'KNOB 4'].forEach((l, i) => knob(dv, KX + 36 + i * step, 108, 30, l, ['level', 'pan', 'kind', 'engine'][i], [20, 0, -80, 60][i]));
  const b1 = [['FX', 'buses'], ['SCL', 'scale'], ['ENV', 'articulate'], ['LFO', 'mod'], ['EDIT', 'voice'], ['GLO', 'song']];
  const b2 = [['HOME', 'tracks'], ['SAVE', 'save'], ['ARP', 'arpeggio'], ['SEQ', 'steps'], ['PLAY', 'start'], ['REC', 'record']];
  const BW = 52, BG = 6;
  b1.forEach(([l, s], i) => dbtn(dv, KX + i * (BW + BG), 196, BW, 48, l, s));
  b2.forEach(([l, s], i) => dbtn(dv, KX + i * (BW + BG), 254, BW, 48, l, s, { lit: l === 'HOME' || l === 'PLAY' }));
  // engraved legend
  etched(dv, 'HOLD A KEY FOR ITS HOLD LAYER  ·  HOLD HOME FOR HELP', KX, 316, { size: 9, ls: 10, font: F.d6 });
  readout(dv, KX, 340, 6 * 52 + 30, 56, 'Now', 'K1 LEVEL 84 · K2 PAN 0', { vsize: 13, vls: 3, right: 'T1', name: 'footer readout · what the knobs do' });

  // keybed
  const kb = frame(dv, 30, 418, DW - 60, 170, { fill: [solid('#0e1012'), imgFill('metal-dark', 'FILL', 0.6)], r: 12, stroke: '#000000', fx: [inner('#000000', 0.9, 0, 3, 10), drop('#cfe0ff', 0.10, 0, 1, 0)], clip: true, name: 'Keybed · F3–G5' });
  const whites = ['F3', 'G3', 'A3', 'B3', 'C4', 'D4', 'E4', 'F4', 'G4', 'A4', 'B4', 'C5', 'D5', 'E5', 'F5', 'G5'];
  const kbw = DW - 60, n = 16, gap = 6, kw = (kbw - 32 - gap * (n - 1)) / n;
  const held = new Set(['C4', 'E4', 'G4', 'D#4']);
  whites.forEach((k, i) => {
    const x = 16 + i * (kw + gap), on = held.has(k);
    const key = frame(kb, x, 86, kw, 72, { fill: lin([[0, on ? '#5a5547' : C.keyHi], [1, on ? '#3d392f' : C.key]]), r: 8, stroke: '#000000', sa: 0.8, fx: [inner('#ffffff', on ? 0.25 : 0.14, 0, 1, 0), drop('#000000', 0.6, 0, on ? 1 : 3, on ? 2 : 4)].concat(on ? [glow(C.cream, 0.35, 12)] : []), name: `key ${k}` });
    rect(key, kw / 2 - 1, 10, 2, 30, solid('#000000', 0.35), { r: 1, name: 'groove' });
    ctext(key, k, kw / 2, 52, { font: F.m5, size: 9, color: on ? C.cream : C.muted });
  });
  const blacks = [['F#3', 0], ['G#3', 1], ['A#3', 2], ['C#4', 4], ['D#4', 5], ['F#4', 7], ['G#4', 8], ['A#4', 9], ['C#5', 11], ['D#5', 12], ['F#5', 14]];
  blacks.forEach(([k, b]) => {
    const x = 16 + (b + 1) * (kw + gap) - gap / 2 - kw / 2, on = k === 'D#4';
    const key = frame(kb, x, 12, kw, 66, { fill: lin([[0, on ? '#4a4538' : '#272c31'], [1, on ? '#2f2b22' : C.keyBk]]), r: 8, stroke: '#000000', fx: [inner('#ffffff', 0.10, 0, 1, 0), drop('#000000', 0.7, 0, 3, 4)].concat(on ? [glow(C.cream, 0.35, 12)] : []), name: `key ${k}` });
    rect(key, kw / 2 - 1, 10, 2, 26, solid('#000000', 0.45), { r: 1, name: 'groove' });
    ctext(key, k, kw / 2, 46, { font: F.m5, size: 9, color: on ? C.cream : C.muted });
  });
}

// ── Page rail: jump straight to a screen page (replaces the GO TO grid) ──────────
const RY = DY + DH + 16;
{
  const r = frame(page, DX, RY, DW, 40, { fill: solid(C.panel), r: 8, stroke: C.line, name: 'Page rail · go to' });
  text(r, 'GO TO', 14, 13, { font: F.d7, size: 10, ls: 18, color: C.dim });
  const pages = ['Tracks', 'Influence', 'Voice', 'Articulation', 'Arp', 'Steps', 'Mod', 'Buses', 'Clock', 'Song', 'Save'];
  let x = 70;
  pages.forEach((p, i) => {
    const on = i === 0;
    const t = text(r, p.toUpperCase(), x + 10, 12, { font: F.d7, size: 11, ls: 10, color: on ? '#ffffff' : C.muted });
    const bw = t.width + 20;
    if (on) { const bg = rect(r, x, 6, bw, 28, solid(C.mar), { r: 5, stroke: C.marHi, name: 'tab on' }); r.insertChild(r.children.indexOf(t), bg); }
    x += bw + 4;
  });
  wbtn(r, DW - 150, 6, 68, 28, '‹ Back', { size: 11 }); wbtn(r, DW - 76, 6, 68, 28, 'Fwd ›', { size: 11 });
}

// ── Monitor strip: make what's happening visible ─────────────────────────────────
const MY = RY + 56, MH = RY0 - MY;
{
  const gap = 16, w1 = 432, w2 = 252, w3 = DW - w1 - w2 - gap * 2;
  // OUTPUT scope
  const m1 = module(page, DX, MY, w1, MH, 'Output scope', { lamp: C.ph, right: 'L / R · 20 ms' });
  const SH = MH - 92;
  const s1 = scope(m1, 12, 40, w1 - 24, SH, { div: 12, rows: 3, name: 'scope · output' });
  SEED = 99;
  const pts = [], pts2 = [];
  for (let i = 0; i <= 200; i++) {
    const t = i / 200, x = t * (w1 - 24);
    const env = 0.35 + 0.65 * Math.exp(-((t - 0.18) ** 2) / 0.02) + 0.3 * Math.exp(-((t - 0.66) ** 2) / 0.006);
    const y = SH / 2 + Math.sin(t * 46) * SH * 0.22 * env * (0.8 + 0.2 * Math.sin(t * 7)) + (rnd() - 0.5) * 6 * env;
    pts.push([x, y]);
    pts2.push([x, SH / 2 + Math.sin(t * 46 + 0.9) * SH * 0.15 * env + (rnd() - 0.5) * 5]);
  }
  trace(s1, pts2, { color: C.phMid, sw: 1.1, glow: 0.35, opacity: 0.6, name: 'trace R' });
  trace(s1, pts, { name: 'trace L' });
  finish(s1, w1 - 24, SH);
  text(s1, 'L', 8, 6, { font: F.m5, size: 10, color: C.ph, fx: [glow(C.ph, 0.6, 6)] });
  readout(m1, 12, MH - 42, 128, 30, null, 'PK −6.2 dB', { vsize: 11, vls: 4 });
  readout(m1, 148, MH - 42, 128, 30, null, 'RMS −14.8', { vsize: 11, vls: 4 });
  readout(m1, 284, MH - 42, w1 - 296, 30, null, 'CPU 31%', { vsize: 11, vls: 4 });

  // INFLUENCE matrix
  const m2 = module(page, DX + w1 + gap, MY, w2, MH, 'Influence', { right: 'FROM → TO' });
  const sc = scope(m2, 12, 40, w2 - 24, MH - 52, { div: 1, rows: 1, name: 'matrix · influence' });
  const cell = Math.min(24, (MH - 52 - 40) / 8), ox = (w2 - 24 - cell * 8) / 2 + 8, oy = 26;
  for (let i = 0; i < 8; i++) {
    ctext(sc, `${i + 1}`, ox + i * cell + cell / 2, 6, { font: F.m5, size: 9, color: C.phMid });
    text(sc, `T${i + 1}`, ox - 24, oy + i * cell + cell / 2 - 7, { font: F.m5, size: 9, color: C.phMid });
  }
  const links = { '0-4': 0.9, '0-6': 0.5, '1-0': 0.4, '2-7': 0.7, '4-5': 0.8, '6-2': 0.6, '6-3': 0.3, '7-1': 0.55, '3-3': 0, '5-0': 0.35 };
  for (let r = 0; r < 8; r++) for (let c = 0; c < 8; c++) {
    const a = links[`${r}-${c}`];
    const cx = ox + c * cell + cell / 2, cy = oy + r * cell + cell / 2;
    if (a) ellipse(sc, cx, cy, 5 + a * 9, solid(r === 0 ? C.ph : C.phMid), { fx: [glow(C.ph, 0.6 * a, 8)], name: `T${r + 1}→T${c + 1}` });
    else ellipse(sc, cx, cy, 3, solid(C.phDim, 0.7), { name: 'dot' });
  }
  rect(sc, ox, oy, cell * 8, cell, solid(C.ph, 0.06), { name: 'row highlight T1' });
  text(sc, 'dot size = amount · T1 selected', 10, MH - 52 - 20, { font: F.m4, size: 9, color: C.phMid });
  finish(sc, w2 - 24, MH - 52);

  // LEVELS
  const m3 = module(page, DX + w1 + w2 + gap * 2, MY, w3, MH, 'Levels', { right: 'TRACKS · MASTER' });
  const sl = scope(m3, 12, 40, w3 - 24, MH - 52, { div: 1, rows: 4, name: 'meters' });
  const lv = [0.82, 0.55, 0.30, 0.64, 0.40, 0.22, 0.90, 0.48], bw = 14, bh = MH - 52 - 48, top = 14;
  const span = (w3 - 24 - 40) / 10;
  lv.forEach((v, i) => {
    const x = 16 + i * span;
    rect(sl, x, top, bw, bh, solid('#0b140f'), { r: 2, name: 'meter bg' });
    const hh = bh * v;
    rect(sl, x, top + bh - hh, bw, hh, lin([[0, C.ph], [1, C.phDim]]), { r: 2, fx: [glow(C.ph, 0.45, 6)], name: `meter T${i + 1}` });
    rect(sl, x, top + bh - Math.min(1, v + 0.08) * bh, bw, 2, solid(v > 0.85 ? C.marHi : C.cream), { name: 'peak' });
    ctext(sl, `${i + 1}`, x + bw / 2, top + bh + 8, { font: F.m5, size: 9, color: i === 0 ? C.ph : C.phMid });
  });
  [0.78, 0.74].forEach((v, i) => {
    const x = 16 + (8.6 + i * 0.75) * span;
    rect(sl, x, top, bw, bh, solid('#0b140f'), { r: 2, name: 'meter bg' });
    rect(sl, x, top + bh * (1 - v), bw, bh * v, lin([[0, C.cream], [1, '#6e6a5c']]), { r: 2, fx: [glow(C.cream, 0.35, 6)], name: i ? 'master R' : 'master L' });
  });
  ctext(sl, 'MST', 16 + 9.35 * span + bw / 2 - 6, top + bh + 8, { font: F.m5, size: 9, color: C.cream });
  finish(sl, w3 - 24, MH - 52);
}

// ── Eight tracks: channel strips with a live trace each ─────────────────────────────
const TY = RY0 + 72;
{
  text(page, 'EIGHT TRACKS', X0, TY - 30, { font: F.d7, size: 13, ls: 22, color: C.ink });
  text(page, 'T1–T4 synthesize · T5–T6 strike · T7–T8 drum — any track can be any kind, and every one can act on every other', X0 + 140, TY - 28, { font: F.m4, size: 11, color: C.muted });
  const tw = (CW - 7 * 12) / 8;
  const info = [['SYNTH', 'VA', 'GLASS PAD 03'], ['SYNTH', 'CZ', 'RESO STAB'], ['SYNTH', 'HYBRID', 'SLOW HAZE'], ['SYNTH', 'PHYS', 'BOWED TIN'], ['STRIKE', 'MODES', 'WOOD BLOCK'], ['STRIKE', 'STRING', 'PLUCK 2'], ['PERC', 'TRX', 'KIT 707'], ['PERC', 'EFM', 'METAL KIT']];
  info.forEach(([kind, eng, snd], i) => {
    const on = i === 0, x = X0 + i * (tw + 12);
    const g = frame(page, x, TY, tw, 210, { fill: [solid(C.panel), imgFill('metal-dark', 'FILL', 0.5)], r: 10, stroke: on ? C.marHi : C.line, sw: on ? 1.5 : 1, fx: [drop('#000000', 0.6, 0, 6, 14)], clip: true, name: `strip T${i + 1}` });
    if (on) rect(g, 0, 0, tw, 4, solid(C.marHi), { name: 'selected' });
    text(g, `T${i + 1}`, 12, 12, { font: F.d7, size: 22, color: '#ffffff' });
    text(g, `${kind} · ${eng}`, 52, 15, { font: F.d7, size: 11, ls: 8, color: kind === 'PERC' ? '#d0646a' : C.muted });
    text(g, snd, 52, 30, { font: F.m4, size: 10, color: C.dim });
    const s = scope(g, 10, 54, tw - 20, 70, { div: 6, rows: 1, name: 'mini scope' });
    SEED = 300 + i * 17;
    const p = [], ww = tw - 20;
    for (let k = 0; k <= 90; k++) {
      const t = k / 90, x2 = t * ww;
      let y;
      if (kind === 'SYNTH') y = 35 + (i === 1 ? ((t * 9) % 1 - 0.5) * 34 : Math.sin(t * (14 + i * 5)) * 18 * (0.6 + 0.4 * Math.sin(t * 3 + i)));
      else if (kind === 'STRIKE') y = 35 + Math.sin(t * 60) * 28 * Math.exp(-t * 4.5);
      else { const ph = (t * 4) % 1; y = 35 + (rnd() - 0.5) * 50 * Math.exp(-ph * 12) + Math.sin(t * 90) * 8 * Math.exp(-ph * 6); }
      p.push([x2, y]);
    }
    trace(s, p, { sw: 1.2, gr: 6, color: on ? C.ph : C.phMid, glow: on ? 0.7 : 0.4 });
    finish(s, tw - 20, 70);
    // level + controls
    text(g, 'LVL', 12, 138, { font: F.m5, size: 9, color: C.dim });
    rect(g, 40, 142, tw - 52, 4, solid('#0a0b0a'), { r: 2, name: 'lvl bg' });
    rect(g, 40, 142, (tw - 52) * [0.84, 0.75, 0.6, 0.7, 0.66, 0.5, 0.88, 0.62][i], 4, solid(C.cream), { r: 2, name: 'lvl' });
    text(g, 'INF', 12, 156, { font: F.m5, size: 9, color: C.dim });
    for (let d = 0; d < 8; d++) ellipse(g, 46 + d * ((tw - 58) / 7), 161, 5, solid(d === i ? C.dim : ([4, 6].includes(d) && on) || (i === 6 && d === 2) || (i === 4 && d === 5) ? C.ph : '#2a2e2c'), { name: 'influence dot' });
    const mb = (lx, l, act) => { const b = frame(g, lx, 176, 30, 24, { fill: solid(act ? C.mar : '#1c1e1d'), r: 4, stroke: act ? C.marHi : C.line2, name: `btn ${l}` }); ctext(b, l, 15, 5, { font: F.d7, size: 11, color: act ? '#fff' : C.muted }); };
    mb(12, 'M', i === 5); mb(46, 'S', false);
    lamp(g, tw - 18, 188, C.ph, [0.82, 0.55, 0.30, 0.64, 0.40, 0.22, 0.90, 0.48][i] > 0.45, 8);
  });
}

// ── WEFT patcher + signal flow ───────────────────────────────────────────────────
const WY = TY + 250;
{
  const ww = 772;
  const m = module(page, X0, WY, ww, 440, 'WEFT patcher', { lamp: C.ph, right: 'T3 · RUN · ×1' });
  text(m, 'Patch a voice from eight blocks: drag from an output to an input.', 14, 47, { font: F.m4, size: 11, color: C.muted });
  const sel = frame(m, ww - 236, 40, 70, 28, { fill: solid('#0f1110'), r: 5, stroke: C.line2, name: 'select · track' });
  text(sel, 'T3', 10, 6, { font: F.d7, size: 13 }); path(sel, 'M 0 0 L 5 5 L 10 0', 50, 11, { stroke: C.muted });
  wbtn(m, ww - 158, 40, 70, 28, 'Run', { state: 'on', size: 11 }); wbtn(m, ww - 82, 40, 70, 28, 'Export', { size: 11 });
  const sc = scope(m, 14, 82, ww - 28, 344, { div: 16, rows: 4, name: 'patch canvas' });
  const BWID = 118;
  const blocks = [['OSC A', 'sine · 220', 24, 28], ['OSC B', 'saw · 3:2', 24, 136], ['SLOPE', 'rise 40ms', 24, 252], ['LOGIC', 'A xor B', 190, 70], ['ARITH', 'A × env', 190, 206], ['FILTER', 'lp · q 0.7', 356, 46], ['DELAY', '18 ms · fb', 356, 196], ['BANDS', '8 × res', 522, 128]];
  const pos = {};
  blocks.forEach(([n, s, bx, by]) => {
    const b = frame(sc, bx, by, BWID, 64, { fill: lin([[0, '#1b1f1c'], [1, '#111412']]), r: 6, stroke: n === 'FILTER' ? C.ph : '#2c3a31', sw: 1, fx: [drop('#000000', 0.7, 0, 3, 6)], name: `block ${n}` });
    text(b, n, 12, 10, { font: F.d7, size: 13, ls: 10, color: C.ink });
    text(b, s, 12, 32, { font: F.m4, size: 10, color: C.phMid });
    ellipse(b, 0, 32, 10, solid(C.crt), { stroke: C.phMid, sw: 1.5, name: 'in' });
    ellipse(b, BWID, 32, 10, solid(C.phMid), { stroke: C.ph, sw: 1, fx: [glow(C.ph, 0.6, 5)], name: 'out' });
    pos[n] = [bx, by];
  });
  const cord = (a, b, col = C.ph, sag = 40) => curve(sc, pos[a][0] + BWID, pos[a][1] + 32, pos[b][0], pos[b][1] + 32, { stroke: col, sw: 2, sag, fx: [glow(col, 0.55, 6)], name: `cord ${a}→${b}` });
  cord('OSC A', 'LOGIC', C.ph, 20); cord('OSC B', 'LOGIC', C.ph, -20); cord('LOGIC', 'FILTER', C.ph, 10); cord('OSC B', 'ARITH', C.phMid, 30);
  cord('SLOPE', 'ARITH', C.marHi, 10); cord('ARITH', 'DELAY', C.phMid, 20); cord('FILTER', 'BANDS', C.ph, 30); cord('DELAY', 'BANDS', C.ph, -10);
  const outp = frame(sc, ww - 28 - 68, 135, 50, 50, { fill: solid('#1a1012'), r: 25, stroke: C.marHi, sw: 1.5, fx: [glow(C.marHi, 0.4, 10)], name: 'OUT' });
  ctext(outp, 'OUT', 25, 17, { font: F.d7, size: 12, color: '#fff' });
  curve(sc, pos['BANDS'][0] + BWID, pos['BANDS'][1] + 32, ww - 28 - 68, 160, { stroke: C.cream, sw: 2, sag: 0, fx: [glow(C.cream, 0.5, 6)], name: 'cord BANDS→OUT' });
  finish(sc, ww - 28, 344);

  // signal flow schematic
  const fx = X0 + ww + 16, fw = CW - ww - 16;
  const f = module(page, fx, WY, fw, 440, 'How it fits together');
  text(f, 'Eight tracks, and every one can act on every other', 14, 44, { font: F.d7, size: 17, color: C.ink });
  text(f, 'Synthesize, strike, drum. Any track can be any kind.', 14, 68, { font: F.m4, size: 11, color: C.muted });
  const sc2 = scope(f, 14, 96, fw - 28, 330, { div: 12, rows: 3, name: 'schematic' });
  const bx = (x, y, w, h, title, sub, col) => {
    const b = frame(sc2, x, y, w, h, { fill: solid('#0c120e'), r: 6, stroke: col, sw: 1.2, name: `block ${title}` });
    text(b, title, 12, 10, { font: F.d7, size: 13, color: C.ink });
    if (sub) text(b, sub, 12, 30, { font: F.m4, size: 10, lh: 14, color: C.phMid, w: w - 24 });
    return b;
  };
  const inf = frame(sc2, 14, 24, 30, 282, { fill: solid('#14090b'), r: 6, stroke: C.marHi, name: 'influence bus' });
  const it = text(inf, 'INFLUENCE', 0, 0, { font: F.d7, size: 11, ls: 30, color: '#e2a3a6' }); it.rotation = 90; it.x = 22; it.y = 100;
  bx(64, 24, 196, 84, 'Synth T1–T4', 'VA, CZ, FM, PHYS, FORMANT, PARTICLE, CHIP, WAVE, LOGIC…', C.phMid);
  bx(64, 124, 196, 62, 'Strike T5–T6', 'exciter → modes, strings', C.phMid);
  bx(64, 202, 196, 62, 'Perc T7–T8', '33 drum machines', C.marHi);
  bx(290, 54, 150, 210, 'Mixer & buses', 'feedback bus\ndelay: echo, grains\nspace: 12-bit\nbed: dust → metal\nmaster drive', C.ph);
  const mo = bx(290, 278, 150, 36, 'Master out', null, C.cream);
  [[46], [146], [224]].forEach(([y]) => { line(sc2, 44, y + 10, 64, y + 10, { stroke: C.marHi, sw: 1.5, fx: [glow(C.marHi, 0.5, 4)] }); line(sc2, 260, y + 10, 290, y + 10, { stroke: C.ph, sw: 1.5, fx: [glow(C.ph, 0.6, 5)] }); });
  line(sc2, 365, 264, 365, 278, { stroke: C.cream, sw: 1.5 });
  finish(sc2, fw - 28, 330);
}

// ── Guides ───────────────────────────────────────────────────────────────────
const GY = WY + 472;
{
  const gw = (CW - 16) / 2;
  const g1 = module(page, X0, GY, gw, 372, 'Playing it', { right: 'MOUSE · TOUCH · KEYBOARD' });
  const kc = (p, k, x, y) => { const b = frame(p, x, y, Math.max(22, k.length * 8 + 12), 22, { fill: lin([[0, '#2e3230'], [1, '#1d201e']]), r: 4, stroke: '#000000', fx: [inner('#ffffff', 0.12, 0, 1, 0), drop('#000000', 0.5, 0, 2, 0)], name: `keycap ${k}` }); ctext(b, k, b.width / 2, 4, { font: F.m5, size: 10, color: C.ink }); return b.width; };
  const rows = [[['Z', 'X', 'C', 'V', 'B', 'N', 'M'], 'lowest seven keys, F to B'], [['A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\''], 'white keys from C4 up'], [['W', 'E', 'T', 'Y', 'U', 'O', 'P'], 'black keys from C#4 up'], [['[', ']'], 'OCT − and OCT +'], [['1', '…', '8'], 'choose the track'], [['←', '→'], 'SELECT: the page'], [['Space'], 'PLAY'], [['Esc'], 'let go of every held button']];
  rows.forEach(([ks, d], r) => {
    const y = 46 + r * 38; let x = 14;
    ks.forEach(k => { x += kc(g1, k, x, y) + 4; });
    text(g1, d, 348, y + 4, { font: F.m4, size: 11, color: C.muted });
    if (r < rows.length - 1) rect(g1, 14, y + 31, gw - 28, 1, solid(C.line), { name: 'rule' });
  });

  const g2 = module(page, X0 + gw + 16, GY, gw, 372, 'Controls guide', {});
  wbtn(g2, gw - 120, 4, 108, 22, 'Full manual', { size: 10 });
  const tabs = ['Start', 'Buttons', 'Hold', 'Influence', 'Graph', 'FX', 'Arp', 'Comp', 'Looper', 'Weft', 'Steps'];
  let tx = 14, ty = 44;
  tabs.forEach((t, i) => {
    const tt = text(g2, t.toUpperCase(), 0, 0, { font: F.d7, size: 11, ls: 8, color: i ? C.muted : '#fff' }); const w = tt.width + 18;
    if (tx + w > gw - 14) { tx = 14; ty += 32; }
    const b = rect(g2, tx, ty, w, 26, solid(i ? '#1a1c1b' : C.mar), { r: 5, stroke: i ? C.line2 : C.marHi, name: `tab ${t}` });
    g2.insertChild(g2.children.indexOf(tt), b); tt.x = tx + 9; tt.y = ty + 6; tx += w + 6;
  });
  text(g2, 'Ravel starts on a blank project: play the keys to hear T1. To hear what it can do, choose Demo: Ravel, press Load, then Play — T1–T4 synthesize, T5–T6 strike, T7–T8 drum, and every track can push on every other.', 14, ty + 44, { font: F.m4, size: 11, lh: 18, color: C.ink, w: gw - 28 });
  const bul = ['ALGORITHM picks the track; the keys play it.', 'Each button opens its section; SELECT walks its pages.', 'KNOB 1–4 set the four values at the top of the screen.', 'PRESETS browses what the page holds — sounds, steps, scenes.', 'Hold HOME for a help screen of the page’s controls.'];
  bul.forEach((b, i) => { rect(g2, 16, ty + 132 + i * 26, 4, 4, solid(C.marHi), { name: 'bullet' }); text(g2, b, 30, ty + 126 + i * 26, { font: F.m4, size: 11, color: C.muted }); });
}

// ── Footer ───────────────────────────────────────────────────────────────────
{
  H = GY + 372 + 110; page.resize(W, H); GRAIN.resize(W, H);
  const fy = H - 80;
  rect(page, X0, fy, CW, 1, solid(C.line), { name: 'rule' });
  rect(page, X0, fy + 24, 120, 22, imgFill('ravel-logo-light', 'FIT', 0.55), { name: 'footer wordmark' });
  text(page, 'Custom firmware for the M-VAVE FM-1  ·  GPL-3.0  ·  github.com/De-Pattern/Ravel_FM1', X0 + 140, fy + 28, { font: F.m4, size: 11, color: C.dim });
  rtext(page, 'MANUAL   ·   INSTALL   ·   SOURCE   ·   CHANGELOG', X0 + CW, fy + 28, { font: F.d7, size: 11, ls: 18, color: C.dim });
}
