// ── Components & tokens sheet ──────────────────────────────────────────────────
{
  const sx = 1440 + 120, SWD = 1100, SHT = 1560;
  const sh = figma.createFrame(); sh.name = 'Components & tokens'; sh.resize(SWD, SHT);
  sh.fills = [solid(C.room)]; sh.clipsContent = true; figma.currentPage.appendChild(sh); sh.x = sx; sh.y = 0;
  rect(sh, 0, 0, SWD, SHT, imgFill('grain', 'FILL', 0.05), { name: 'grain' });
  text(sh, 'RAVEL EMULATOR — COMPONENTS & TOKENS', 48, 40, { font: F.d7, size: 20, ls: 14, color: C.ink });
  text(sh, 'Direction: a dark medical monitor in a black speckled matte-plastic case — phosphor-green traces for anything live, sunken pale readouts for values, maroon only for selection, recording and warnings.', 48, 72, { font: F.m4, size: 12, lh: 18, color: C.muted, w: SWD - 96 });

  const head = (t, y) => { text(sh, t, 48, y, { font: F.d7, size: 12, ls: 20, color: C.dim }); rect(sh, 48, y + 22, SWD - 96, 1, solid(C.line), { name: 'rule' }); };

  // colour tokens
  head('COLOUR', 128);
  const sw = [['room', C.room], ['panel', C.panel], ['line', C.line2], ['ink', C.ink], ['muted', C.muted], ['metal', C.metal], ['etch', C.etch], ['readout', C.read], ['readout ink', C.readInk], ['phosphor', C.ph], ['phosphor mid', C.phMid], ['phosphor dim', C.phDim], ['crt', C.crt], ['maroon', C.mar], ['maroon hi', C.marHi], ['cream', C.cream]];
  sw.forEach(([n, hex], i) => {
    const x = 48 + (i % 8) * 126, y = 166 + Math.floor(i / 8) * 104;
    rect(sh, x, y, 112, 56, solid(hex), { r: 8, stroke: '#ffffff', sa: 0.08, fx: hex === C.ph ? [glow(C.ph, 0.5, 12)] : [], name: `swatch ${n}` });
    text(sh, n, x, y + 62, { font: F.d6, size: 12, color: C.ink });
    text(sh, hex.toUpperCase(), x, y + 78, { font: F.m4, size: 10, color: C.dim });
  });

  // type
  head('TYPE', 384);
  text(sh, 'Barlow Semi Condensed — labels, buttons, headings', 48, 420, { font: F.m4, size: 11, color: C.dim });
  text(sh, 'EIGHT TRACKS', 48, 442, { font: F.d7, size: 22, ls: 22, color: C.ink });
  text(sh, 'SESSION  ·  11 / Bold / +18%', 48, 478, { font: F.d7, size: 11, ls: 18, color: C.muted });
  text(sh, 'IBM Plex Mono — readouts, notes, data', 560, 420, { font: F.m4, size: 11, color: C.dim });
  readout(sh, 560, 442, 220, 56, 'Readout', 'GLASS PAD 03', { vsize: 16 });
  text(sh, 'Notes, clock and start/stop from a keyboard or DAW.', 800, 456, { font: F.m4, size: 11, lh: 16, color: C.muted, w: 250 });

  // buttons
  head('BUTTONS & LAMPS', 540);
  const cap = (t, x, y) => text(sh, t, x, y, { font: F.m4, size: 10, color: C.dim });
  wbtn(sh, 48, 580, 120, 36, 'Default'); cap('default', 48, 624);
  wbtn(sh, 184, 580, 120, 36, 'Prime', { state: 'prime', lamp: C.ph }); cap('prime + lamp', 184, 624);
  wbtn(sh, 320, 580, 120, 36, 'Selected', { state: 'on' }); cap('selected / armed', 320, 624);
  wbtn(sh, 456, 580, 120, 36, 'Record', { lamp: C.marHi, lampOn: false }); cap('lamp off', 456, 624);
  lamp(sh, 620, 598, C.ph, true, 10); lamp(sh, 650, 598, C.marHi, true, 10); lamp(sh, 680, 598, C.ph, false, 10); cap('lamps', 600, 624);
  dbtn(sh, 740, 572, 56, 50, 'ENV', 'articulate'); cap('device key', 740, 630);
  dbtn(sh, 812, 572, 56, 50, 'PLAY', 'start', { lit: true }); cap('lit', 812, 630);
  const tb = frame(sh, 900, 580, 152, 36, { fill: solid(C.panel), r: 8, stroke: C.line, name: 'rail tabs' });
  rect(tb, 6, 4, 70, 28, solid(C.mar), { r: 5, stroke: C.marHi, name: 'tab on' }); ctext(tb, 'TRACKS', 41, 11, { font: F.d7, size: 11, color: '#fff' }); ctext(tb, 'VOICE', 112, 11, { font: F.d7, size: 11, color: C.muted });
  cap('page rail', 900, 624);

  // knobs & keys on a metal swatch
  head('ON THE METAL', 680);
  const mp = frame(sh, 48, 718, SWD - 96, 230, { fill: [solid(C.metal), imgFill('speckle-device'), SHEEN], r: 14, stroke: '#07080a', sw: 1.5, fx: [inner('#d6e6ff', 0.22, 0, 1, 0), drop('#000000', 0.7, 0, 12, 28)], clip: true, name: 'metal swatch' });
  screw(mp, 16, 16, 11); screw(mp, SWD - 112, 16, 11);
  knob(mp, 90, 92, 34, 'MASTER', 'volume', 40);
  knob(mp, 200, 92, 30, 'KNOB 1', 'level', -40);
  etched(mp, 'ENGRAVED LABEL', 290, 60, { size: 12, ls: 24 });
  text(mp, 'cool light ink, sunk in', 290, 80, { font: F.m4, size: 10, color: '#7b848b' });
  readout(mp, 290, 110, 200, 52, 'Now', 'K1 LEVEL 84', { vsize: 13 });
  const kb = frame(mp, 530, 30, 470, 170, { fill: [solid('#141615'), imgFill('plastic-dark', 'FILL', 0.6)], r: 12, stroke: '#000000', fx: [inner('#000000', 0.9, 0, 3, 10)], clip: true, name: 'keys' });
  [['C4', false], ['D4', true], ['E4', false]].forEach(([k, on], i) => {
    const key = frame(kb, 20 + i * 70, 86, 58, 72, { fill: lin([[0, on ? '#5a5547' : C.keyHi], [1, on ? '#3d392f' : C.key]]), r: 8, stroke: '#000000', sa: 0.8, fx: [inner('#ffffff', 0.14, 0, 1, 0), drop('#000000', 0.6, 0, 3, 4)].concat(on ? [glow(C.cream, 0.35, 12)] : []), name: `white ${on ? 'held' : 'idle'}` });
    rect(key, 28, 10, 2, 30, solid('#000000', 0.35), { r: 1, name: 'groove' }); ctext(key, k, 29, 52, { font: F.m5, size: 9, color: on ? C.cream : C.muted });
  });
  [['C#4', false], ['D#4', true]].forEach(([k, on], i) => {
    const key = frame(kb, 55 + i * 70, 12, 58, 66, { fill: lin([[0, on ? '#4a4538' : '#272c31'], [1, on ? '#2f2b22' : C.keyBk]]), r: 8, stroke: '#000000', fx: [inner('#ffffff', 0.10, 0, 1, 0), drop('#000000', 0.7, 0, 3, 4)].concat(on ? [glow(C.cream, 0.35, 12)] : []), name: `black ${on ? 'held' : 'idle'}` });
    rect(key, 28, 10, 2, 26, solid('#000000', 0.45), { r: 1, name: 'groove' }); ctext(key, k, 29, 46, { font: F.m5, size: 9, color: on ? C.cream : C.muted });
  });
  text(kb, 'held keys glow cream —\nthe screen’s own highlight', 260, 70, { font: F.m4, size: 10, lh: 15, color: C.muted });

  // live visuals
  head('LIVE VISUALS', 990);
  const s1 = scope(sh, 48, 1030, 320, 150, { div: 8, rows: 2, name: 'scope' });
  const p = []; for (let i = 0; i <= 120; i++) { const t = i / 120; p.push([t * 320, 75 + Math.sin(t * 30) * 46 * Math.exp(-t * 2.2)]); }
  trace(s1, p); finish(s1, 320, 150); cap('scope trace · graticule · scanlines', 48, 1188);
  // track cell states
  const cell = (x, on, lvl, kind) => {
    const c = frame(sh, x, 1030, 60, 52, { fill: lin([[0, on ? '#2a1013' : '#1d201e'], [1, on ? '#1a0a0c' : '#151716']]), r: 6, stroke: on ? C.marHi : C.line2, sw: on ? 1.5 : 1, name: `track cell ${on ? 'selected' : 'idle'}` });
    text(c, on ? 'T1' : 'T7', 8, 6, { font: F.d7, size: 15, color: '#fff' }); text(c, kind, 8, 25, { font: F.m4, size: 9, color: kind === 'PRC' ? '#c7555b' : C.muted });
    rect(c, 8, 41, 44, 3, solid('#0a0b0a'), { r: 1.5, name: 'meter bg' }); rect(c, 8, 41, 44 * lvl, 3, solid(C.ph), { r: 1.5, name: 'meter' }); lamp(c, 50, 13, C.ph, lvl > 0.5, 5);
  };
  cell(400, true, 0.8, 'SYN'); cell(472, false, 0.3, 'PRC'); cap('track cell', 400, 1090); cap('selected · idle', 400, 1106);
  // matrix + meters
  const mx = scope(sh, 560, 1030, 150, 150, { div: 1, rows: 1, name: 'influence mini' });
  for (let r = 0; r < 6; r++) for (let c = 0; c < 6; c++) { const a = (r * 7 + c * 3) % 5 === 0 ? ((r + c) % 3 + 1) / 3 : 0; ellipse(mx, 20 + c * 22, 20 + r * 22, a ? 5 + a * 9 : 3, solid(a ? C.ph : C.phDim, a ? 1 : 0.7), { name: 'dot' }); }
  finish(mx, 150, 150); cap('influence: dot size = amount', 560, 1188);
  const lv = scope(sh, 740, 1030, 150, 150, { div: 1, rows: 3, name: 'meters mini' });
  [0.8, 0.5, 0.92, 0.3, 0.6].forEach((v, i) => { rect(lv, 14 + i * 26, 14 + 120 * (1 - v), 14, 120 * v, lin([[0, C.ph], [1, C.phDim]]), { r: 2, name: 'meter' }); rect(lv, 14 + i * 26, 14 + 120 * (1 - Math.min(1, v + 0.06)), 14, 2, solid(v > 0.85 ? C.marHi : C.cream), { name: 'peak' }); });
  finish(lv, 150, 150); cap('meters: maroon peak = hot', 740, 1188);
  const cr = frame(sh, 920, 1030, 132, 132, { fill: lin([[0, '#1f2120'], [1, '#0a0b0a']]), r: 12, name: 'crt mini' });
  const gl = frame(cr, 10, 10, 112, 112, { fill: solid('#020302'), r: 8, fx: [inner('#000000', 1, 0, 0, 10)], clip: true, name: 'glass' });
  rect(gl, 4, 4, 104, 104, imgFill('screen-influence', 'FILL'), { name: 'screen · INFLUENCE page' }); rect(gl, 0, 0, 112, 112, imgFill('scanlines', 'FILL', 0.55), { name: 'scanlines' });
  cap('CRT: real screen', 920, 1188);

  // patch blocks
  head('WEFT PATCH BLOCKS', 1230);
  const pc = scope(sh, 48, 1270, SWD - 96, 230, { div: 12, rows: 2, name: 'patch canvas' });
  const blk = (x, y, n, s, hl) => { const b = frame(pc, x, y, 118, 64, { fill: lin([[0, '#1b1f1c'], [1, '#111412']]), r: 6, stroke: hl ? C.ph : '#2c3a31', name: `block ${n}` }); text(b, n, 12, 10, { font: F.d7, size: 13, ls: 10 }); text(b, s, 12, 32, { font: F.m4, size: 10, color: C.phMid }); ellipse(b, 0, 32, 10, solid(C.crt), { stroke: C.phMid, sw: 1.5, name: 'in' }); ellipse(b, 118, 32, 10, solid(C.phMid), { stroke: C.ph, name: 'out' }); };
  blk(40, 40, 'OSC A', 'sine · 220', false); blk(260, 40, 'FILTER', 'focused', true); blk(40, 140, 'SLOPE', 'rise 40ms', false); blk(260, 140, 'ARITH', 'A × env', false);
  curve(pc, 158, 72, 260, 72, { stroke: C.ph, sw: 2, sag: 0, name: 'audio cord' });
  curve(pc, 158, 172, 260, 172, { stroke: C.marHi, sw: 2, sag: 0, name: 'mod cord' });
  text(pc, 'phosphor cord = audio', 470, 64, { font: F.m4, size: 11, color: C.ph });
  text(pc, 'maroon cord = modulation / influence', 470, 164, { font: F.m4, size: 11, color: '#e2a3a6' });
  finish(pc, SWD - 96, 230);
}
