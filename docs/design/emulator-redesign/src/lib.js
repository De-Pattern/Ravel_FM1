// Ravel emulator page design — helpers over the Figma Plugin API (runs in OpenPencil's `eval`, and in Figma).
// ASSETS is injected by build.py: { name: base64png }.

const HEX = (h) => { h = h.replace('#', ''); if (h.length === 3) h = h.split('').map(c => c + c).join(''); return { r: parseInt(h.slice(0, 2), 16) / 255, g: parseInt(h.slice(2, 4), 16) / 255, b: parseInt(h.slice(4, 6), 16) / 255 }; };
const solid = (hex, a = 1) => ({ type: 'SOLID', color: HEX(hex), opacity: a });
const VERT = { m00: 0, m01: 1, m02: 0, m10: -1, m11: 0, m12: 1 };   // top → bottom
const HORZ = { m00: 1, m01: 0, m02: 0, m10: 0, m11: 1, m12: 0 };    // left → right
const stops = (list) => list.map(([p, hex, a = 1]) => ({ position: p, color: { ...HEX(hex), a } }));
const lin = (list, t = VERT) => ({ type: 'GRADIENT_LINEAR', gradientTransform: t, gradientStops: stops(list) });
// soft top light on satin metal: brighter at the top edge, a little darker at the bottom
const SHEEN = { type: 'GRADIENT_LINEAR', gradientTransform: { m00: 0, m01: 1, m02: 0, m10: -1, m11: 0, m12: 1 }, gradientStops: [{ position: 0, color: { r: 0.85, g: 0.92, b: 1, a: 0.10 } }, { position: 0.12, color: { r: 0.85, g: 0.92, b: 1, a: 0.03 } }, { position: 0.30, color: { r: 0.85, g: 0.92, b: 1, a: 0.06 } }, { position: 0.55, color: { r: 0, g: 0, b: 0, a: 0 } }, { position: 1, color: { r: 0, g: 0, b: 0, a: 0.30 } }] };
const rad = (list) => ({ type: 'GRADIENT_RADIAL', gradientTransform: HORZ, gradientStops: stops(list) });

const inner = (hex, a, x, y, r, s = 0) => ({ type: 'INNER_SHADOW', color: { ...HEX(hex), a }, offset: { x, y }, radius: r, spread: s, visible: true, blendMode: 'NORMAL' });
const drop = (hex, a, x, y, r, s = 0) => ({ type: 'DROP_SHADOW', color: { ...HEX(hex), a }, offset: { x, y }, radius: r, spread: s, visible: true, blendMode: 'NORMAL', showShadowBehindNode: false });
const glow = (hex, a = 0.55, r = 10) => drop(hex, a, 0, 0, r);

// Palette (the tokens sheet lists these too)
const C = {
  room: '#0a0b0a', panel: '#131413', panel2: '#191b1a', line: '#2a2d2b', line2: '#3a3e3b',
  ink: '#e2e5dd', muted: '#8a8f88', dim: '#565b57',
  metal: '#262a2e', metalHi: '#3c4248', metalLo: '#15181b', etch: '#a7b0b7',
  read: '#c8ceb6', readHi: '#d9dec8', readInk: '#1a1e18', readDim: '#6f765f',
  ph: '#6dffa6', phMid: '#3fbf78', phDim: '#1d5c3a', phGrid: '#10261a', crt: '#050806',
  mar: '#6b1218', marHi: '#a3161c', marLo: '#3e0b0f',
  cream: '#e9e2cc', teal: '#2f8f86',
  key: '#2f3439', keyHi: '#3e444a', keyBk: '#1b1f23',
};

const FONTS = [
  ['Barlow Semi Condensed', 'Medium'], ['Barlow Semi Condensed', 'SemiBold'], ['Barlow Semi Condensed', 'Bold'],
  ['IBM Plex Mono', 'Regular'], ['IBM Plex Mono', 'Medium'], ['IBM Plex Mono', 'SemiBold'],
];
for (const [family, style] of FONTS) await figma.loadFontAsync({ family, style });
const F = {
  d5: { family: 'Barlow Semi Condensed', style: 'Medium' }, d6: { family: 'Barlow Semi Condensed', style: 'SemiBold' }, d7: { family: 'Barlow Semi Condensed', style: 'Bold' },
  m4: { family: 'IBM Plex Mono', style: 'Regular' }, m5: { family: 'IBM Plex Mono', style: 'Medium' }, m6: { family: 'IBM Plex Mono', style: 'SemiBold' },
};

const IMG = {};
function imageHash(name) {
  if (IMG[name]) return IMG[name];
  const bin = atob(ASSETS[name]); const u = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
  return (IMG[name] = figma.createImage(u).hash);
}
const imgFill = (name, mode = 'FILL', opacity = 1) => ({ type: 'IMAGE', imageHash: imageHash(name), scaleMode: mode, opacity });

function place(n, parent, x, y, name) { if (name) n.name = name; parent.appendChild(n); n.x = x; n.y = y; return n; }

function frame(parent, x, y, w, h, o = {}) {
  const f = figma.createFrame(); f.resize(w, h);
  f.fills = o.fill === undefined ? [] : [].concat(o.fill);
  if (o.r !== undefined) f.cornerRadius = o.r;
  if (o.stroke) { f.strokes = [solid(o.stroke, o.sa ?? 1)]; f.strokeWeight = o.sw ?? 1; f.strokeAlign = o.align ?? 'INSIDE'; }
  if (o.fx) f.effects = o.fx;
  f.clipsContent = o.clip ?? false;
  if (o.opacity !== undefined) f.opacity = o.opacity;
  return place(f, parent, x, y, o.name || 'frame');
}
function rect(parent, x, y, w, h, fill, o = {}) {
  const r = figma.createRectangle(); r.resize(Math.max(w, 0.01), Math.max(h, 0.01));
  r.fills = fill ? [].concat(fill) : [];
  if (o.r !== undefined) r.cornerRadius = o.r;
  if (o.stroke) { r.strokes = [solid(o.stroke, o.sa ?? 1)]; r.strokeWeight = o.sw ?? 1; r.strokeAlign = o.align ?? 'INSIDE'; }
  if (o.fx) r.effects = o.fx;
  if (o.opacity !== undefined) r.opacity = o.opacity;
  return place(r, parent, x, y, o.name || 'rect');
}
function ellipse(parent, cx, cy, d, fill, o = {}) {
  const e = figma.createEllipse(); e.resize(d, d);
  e.fills = fill ? [].concat(fill) : [];
  if (o.stroke) { e.strokes = [solid(o.stroke, o.sa ?? 1)]; e.strokeWeight = o.sw ?? 1; e.strokeAlign = o.align ?? 'INSIDE'; }
  if (o.fx) e.effects = o.fx;
  if (o.opacity !== undefined) e.opacity = o.opacity;
  return place(e, parent, cx - d / 2, cy - d / 2, o.name || 'ellipse');
}
function text(parent, str, x, y, o = {}) {
  const t = figma.createText();
  t.fontName = o.font || F.d6; t.characters = String(str); t.fontSize = o.size || 12;
  t.fills = [solid(o.color || C.ink, o.a ?? 1)];
  if (o.ls !== undefined) t.letterSpacing = { value: o.ls, unit: 'PERCENT' };
  if (o.lh) t.lineHeight = { value: o.lh, unit: 'PIXELS' };
  if (o.upper) t.textCase = 'UPPER';
  if (o.w) { t.resize(o.w, t.height); t.textAutoResize = 'HEIGHT'; } else t.textAutoResize = 'WIDTH_AND_HEIGHT';
  if (o.align) t.textAlignHorizontal = o.align;
  if (o.fx) t.effects = o.fx;
  if (o.opacity !== undefined) t.opacity = o.opacity;
  place(t, parent, x, y, o.name || String(str).slice(0, 32));
  if (o.w && o.align === 'CENTER') t.x = x;
  return t;
}
// text centred on cx
function ctext(parent, str, cx, y, o = {}) { const t = text(parent, str, 0, y, o); t.x = cx - t.width / 2; return t; }
function rtext(parent, str, rx, y, o = {}) { const t = text(parent, str, 0, y, o); t.x = rx - t.width; return t; }

function path(parent, d, x, y, o = {}) {
  const v = figma.createVector();
  v.vectorPaths = [{ windingRule: o.fill ? 'NONZERO' : 'NONE', data: d }];
  v.fills = o.fill ? [].concat(o.fill) : [];
  v.strokes = o.stroke ? [solid(o.stroke, o.sa ?? 1)] : [];
  v.strokeWeight = o.sw ?? 1.5;
  v.strokeCap = o.cap || 'ROUND'; v.strokeJoin = 'ROUND';
  if (o.fx) v.effects = o.fx;
  if (o.opacity !== undefined) v.opacity = o.opacity;
  return place(v, parent, x, y, o.name || 'path');
}
// polyline from points [[x,y],...], drawn relative to the parent; normalised so the vector's box starts at its min point
function poly(parent, pts, o = {}) {
  const minx = Math.min(...pts.map(p => p[0])), miny = Math.min(...pts.map(p => p[1]));
  let d = '';
  if (o.smooth) {
    d = `M ${(pts[0][0] - minx).toFixed(2)} ${(pts[0][1] - miny).toFixed(2)}`;
    for (let i = 1; i < pts.length; i++) {
      const [x0, y0] = pts[i - 1], [x1, y1] = pts[i], mx = (x0 + x1) / 2;
      d += ` C ${(mx - minx).toFixed(2)} ${(y0 - miny).toFixed(2)} ${(mx - minx).toFixed(2)} ${(y1 - miny).toFixed(2)} ${(x1 - minx).toFixed(2)} ${(y1 - miny).toFixed(2)}`;
    }
  } else d = pts.map((p, i) => `${i ? 'L' : 'M'} ${(p[0] - minx).toFixed(2)} ${(p[1] - miny).toFixed(2)}`).join(' ');
  if (o.close) d += ' Z';
  return path(parent, d, minx, miny, o);
}
function line(parent, x1, y1, x2, y2, o = {}) { return poly(parent, [[x1, y1], [x2, y2]], o); }
function curve(parent, x1, y1, x2, y2, o = {}) {   // a patch-cord style cubic from (x1,y1) to (x2,y2)
  const minx = Math.min(x1, x2), miny = Math.min(y1, y2) - 0;
  const sag = o.sag ?? 40, dx = Math.max(40, Math.abs(x2 - x1) * 0.5);
  const d = `M ${x1 - minx} ${y1 - miny} C ${x1 + dx - minx} ${y1 + sag - miny} ${x2 - dx - minx} ${y2 + sag - miny} ${x2 - minx} ${y2 - miny}`;
  return path(parent, d, minx, miny, o);
}

// ── Pieces ───────────────────────────────────────────────────────────────────

function screw(parent, cx, cy, d = 10) {
  ellipse(parent, cx, cy, d, rad([[0, '#6c747b'], [1, '#22262a']]), { stroke: '#0b0d0f', sw: 1, fx: [drop('#000000', 0.5, 0, 1, 1)], name: 'screw' });
  line(parent, cx - d * 0.32, cy + d * 0.12, cx + d * 0.32, cy - d * 0.12, { stroke: '#0b0d0f', sw: 1.4, name: 'slot' });
}

// engraved label on metal: dark ink with a light lower lip
function etched(parent, str, x, y, o = {}) {
  return text(parent, str, x, y, { font: F.d7, size: 10, ls: 14, color: C.etch, fx: [drop('#000000', 0.8, 0, -1, 0)], ...o });
}
function etchedC(parent, str, cx, y, o = {}) { const t = etched(parent, str, 0, y, o); t.x = cx - t.width / 2; return t; }

// sunken pale readout window with dark segment-ish mono text
function readout(parent, x, y, w, h, label, value, o = {}) {
  const g = frame(parent, x, y, w, h, { fill: lin([[0, C.readHi], [1, C.read]]), r: 4, stroke: '#0a0b0a', sa: 0.9, fx: [inner('#000000', 0.55, 0, 2, 3), inner('#000000', 0.25, 0, 0, 0, 1), drop('#ffffff', 0.10, 0, 1, 0)], clip: true, name: o.name || `readout ${label}` });
  rect(g, 0, 0, w, h, imgFill('grain', 'FILL', 0.18), { name: 'grain' });
  if (label) text(g, label, 8, 5, { font: F.d7, size: 9, ls: 14, color: C.readDim, upper: true });
  const vy = label ? h - (o.vsize || 18) - 8 : (h - (o.vsize || 14) * 1.3) / 2;
  text(g, value, 8, vy, { font: F.m6, size: o.vsize || 18, color: C.readInk, ls: o.vls ?? 2 });
  if (o.right) rtext(g, o.right, w - 8, vy + (o.vsize || 18) - 12, { font: F.m5, size: 10, color: C.readDim });
  return g;
}

function lamp(parent, cx, cy, color, on = true, d = 8) {
  if (on) ellipse(parent, cx, cy, d, rad([[0, '#ffffff'], [0.35, color], [1, color]]), { fx: [glow(color, 0.85, 8)], stroke: '#000000', sa: 0.4, name: 'lamp on' });
  else ellipse(parent, cx, cy, d, rad([[0, '#3a3d3b'], [1, '#141615']]), { stroke: '#000000', sa: 0.6, name: 'lamp off' });
}

// panel/web button (dark, slightly raised); state: 'off' | 'on' | 'prime' | 'warn'
function wbtn(parent, x, y, w, h, label, o = {}) {
  const st = o.state || 'off';
  const fill = st === 'on' ? lin([[0, C.marHi], [1, C.mar]]) : st === 'prime' ? lin([[0, '#2d302e'], [1, '#202321']]) : lin([[0, '#1f2220'], [1, '#171918']]);
  const g = frame(parent, x, y, w, h, { fill, r: 5, stroke: st === 'on' ? '#c42a31' : C.line2, fx: [inner('#ffffff', 0.06, 0, 1, 0), drop('#000000', 0.5, 0, 1, 2)], name: o.name || `button ${label}` });
  let lx = 0;
  if (o.lamp) { lamp(g, 12, h / 2, o.lamp, o.lampOn !== false, 7); lx = 8; }
  const t = text(g, label, 0, 0, { font: F.d7, size: o.size || 12, ls: 8, color: st === 'on' ? '#ffffff' : C.ink, upper: true });
  t.x = (w - t.width) / 2 + lx; t.y = (h - t.height) / 2;
  return g;
}

// rotary knob with etched scale on its surface (metal panel) or on dark
function knob(parent, cx, cy, d, label, sub, angle = -30, o = {}) {
  const g = frame(parent, cx - d, cy - d, d * 2, d * 2 + 30, { name: `knob ${label}` });
  const c = d; // local centre
  // scale ticks, 270° sweep
  const R1 = d * 0.66, R2 = d * 0.76;
  for (let i = 0; i <= 10; i++) {
    const a = (-135 + i * 27) * Math.PI / 180;
    const big = i % 5 === 0;
    line(g, c + Math.sin(a) * R1, c - Math.cos(a) * R1, c + Math.sin(a) * (big ? R2 + 3 : R2), c - Math.cos(a) * (big ? R2 + 3 : R2), { stroke: o.dark ? C.dim : C.etch, sw: big ? 1.6 : 1, name: 'tick' });
  }
  // skirt and cap
  ellipse(g, c, c, d * 1.12, rad([[0, '#2a2d2b'], [1, '#0d0e0d']]), { fx: [drop('#000000', 0.6, 0, 3, 6)], name: 'skirt' });
  ellipse(g, c, c, d * 0.9, lin([[0, '#3d413f'], [1, '#161817']]), { stroke: '#4d514f', sw: 1, fx: [inner('#ffffff', 0.12, 0, 1, 1)], name: 'cap' });
  // knurl ring
  for (let i = 0; i < 24; i++) {
    const a = i * 15 * Math.PI / 180;
    line(g, c + Math.sin(a) * d * 0.41, c - Math.cos(a) * d * 0.41, c + Math.sin(a) * d * 0.45, c - Math.cos(a) * d * 0.45, { stroke: '#0b0c0b', sw: 1, opacity: 0.7, name: 'knurl' });
  }
  const a = angle * Math.PI / 180;
  line(g, c + Math.sin(a) * d * 0.1, c - Math.cos(a) * d * 0.1, c + Math.sin(a) * d * 0.38, c - Math.cos(a) * d * 0.38, { stroke: o.pointer || C.cream, sw: 2.2, name: 'pointer' });
  if (label) { const t = (o.dark ? text : etched)(g, label, 0, d * 2 - 6, o.dark ? { font: F.d7, size: 10, ls: 14, color: C.muted } : {}); t.x = c - t.width / 2; }
  if (sub) { const t = text(g, sub, 0, d * 2 + 8, { font: F.m4, size: 9, color: o.dark ? C.dim : '#7b848b' }); t.x = c - t.width / 2; }
  return g;
}

// device button: label + small function label, optional lit lamp
function dbtn(parent, x, y, w, h, label, sub, o = {}) {
  const g = frame(parent, x, y, w, h, { fill: lin([[0, o.lit ? '#3b2124' : '#343735'], [1, o.lit ? '#24100f' : '#1d201e']]), r: 7, stroke: '#0b0c0b', sa: 0.9, fx: [inner('#ffffff', 0.10, 0, 1, 0), drop('#000000', 0.55, 0, 2, 3)], name: `key ${label}` });
  if (o.lit) rect(g, 10, 6, w - 20, 2, solid(C.marHi), { r: 1, fx: [glow(C.marHi, 0.9, 6)], name: 'led' });
  else rect(g, 10, 6, w - 20, 2, solid('#0e0f0e'), { r: 1, name: 'led off' });
  ctext(g, label, w / 2, h / 2 - 9, { font: F.d7, size: 11, ls: 8, color: C.ink });
  if (sub) ctext(g, sub, w / 2, h / 2 + 5, { font: F.m4, size: 8, color: C.muted });
  return g;
}

// panel module card for the rack: header strip with label + lamp
function module(parent, x, y, w, h, title, o = {}) {
  const g = frame(parent, x, y, w, h, { fill: [solid(C.panel), imgFill('plastic-dark', 'FILL', 0.55)], r: 10, stroke: C.line, fx: [inner('#ffffff', 0.04, 0, 1, 0), drop('#000000', 0.6, 0, 6, 16)], clip: true, name: `module ${title}` });
  rect(g, 0, 0, w, 30, lin([[0, '#1c1e1d'], [1, '#161817']]), { name: 'head' });
  rect(g, 0, 30, w, 1, solid('#000000', 0.6), { name: 'rule' });
  text(g, title, 14, 9, { font: F.d7, size: 11, ls: 18, color: C.muted, upper: true });
  if (o.lamp) lamp(g, w - 16, 15, o.lamp, o.lampOn !== false, 7);
  if (o.right) rtext(g, o.right, w - (o.lamp ? 30 : 14), 9, { font: F.m4, size: 10, color: C.dim });
  return g;
}

// dark phosphor screen panel with graticule
function scope(parent, x, y, w, h, o = {}) {
  const g = frame(parent, x, y, w, h, { fill: solid(C.crt), r: 6, stroke: '#000000', fx: [inner('#000000', 0.9, 0, 2, 8), drop('#ffffff', 0.05, 0, 1, 0)], clip: true, name: o.name || 'scope' });
  const div = o.div || 10, rows = o.rows || 4;
  for (let i = 1; i < div; i++) rect(g, (w / div) * i, 0, 1, h, solid(C.phGrid), { name: 'grid v' });
  for (let i = 1; i < rows * 2; i++) rect(g, 0, (h / (rows * 2)) * i, w, 1, solid(C.phGrid, i === rows ? 1 : 0.6), { name: 'grid h' });
  return g;
}
// phosphor trace: two soft wide strokes under a sharp one (no shadow effects, so open paths render cleanly everywhere)
function trace(parent, pts, o = {}) {
  const col = o.color || C.ph, sw = o.sw ?? 1.6, g = o.glow ?? 0.7, op = o.opacity ?? 1;
  poly(parent, pts, { stroke: col, sw: sw * 6, opacity: 0.07 * g * op, smooth: o.smooth, name: 'glow wide' });
  poly(parent, pts, { stroke: col, sw: sw * 2.6, opacity: 0.22 * g * op, smooth: o.smooth, name: 'glow' });
  return poly(parent, pts, { stroke: col, sw, smooth: o.smooth, opacity: op, name: o.name || 'trace' });
}
const finish = (g, w, h) => { rect(g, 0, 0, w, h, imgFill('scanlines', 'FILL', 0.6), { name: 'scanlines' }); rect(g, 0, 0, w, h, imgFill('vignette', 'FILL', 0.9), { name: 'vignette' }); };

// a deterministic pseudo-random for traces
let SEED = 1234;
const rnd = () => { SEED = (SEED * 16807) % 2147483647; return SEED / 2147483647; };

// ── CRT unit: thin pale-wine bezel with a short soft glow, glass, real screen, scanlines, glare ──
// glow: 0 (rest) … 1 (pulse peak). glare: null (none) or 0 … 1 (position of the sweep, top-left → bottom-right).
// deep maroon-red bezel with a subtle iridescent drift: oxblood → crimson → faint violet-wine → copper-red glint → oxblood
const WINE = { rim: '#c23a3f', glow: '#b3202c' };
const DIAG = { m00: 0.5, m01: 0.5, m02: 0, m10: -0.5, m11: 0.5, m12: 0.5 };   // top-left → bottom-right
const BEZEL = { type: 'GRADIENT_LINEAR', gradientTransform: DIAG, gradientStops: stops([[0, '#4c0a12'], [0.22, '#8a1622'], [0.45, '#6b1430'], [0.62, '#5a0f1f'], [0.8, '#9b2a1c'], [1, '#430910']]) };
function crt(parent, x, y, s, o = {}) {
  const g = o.glow ?? 0.3, B = o.border ?? 4;
  const bez = frame(parent, x, y, s, s, {
    fill: [BEZEL, lin([[0, '#ffffff', 0.10], [0.25, '#ffffff', 0], [1, '#000000', 0.18]])], r: 10, stroke: WINE.rim, sa: 0.45,
    fx: [inner('#ffffff', 0.22, 0, 1, 0), inner('#000000', 0.35, 0, -1, 1), drop(WINE.glow, 0.30 + 0.30 * g, 0, 0, 6 + 5 * g), drop('#000000', 0.6, 0, 4, 10)],
    name: o.name || 'CRT bezel · maroon',
  });
  const gs = s - B * 2;
  const glass = frame(bez, B, B, gs, gs, { fill: solid('#020302'), r: 7, stroke: '#000000', fx: [inner('#000000', 1, 0, 0, 12)], clip: true, name: 'CRT glass' });
  rect(glass, 6, 6, gs - 12, gs - 12, imgFill(o.screen || 'screen-tracks', 'FILL'), { name: 'screen · live render' });
  rect(glass, 0, 0, gs, gs, imgFill('scanlines', 'FILL', 0.5), { name: 'scanlines' });
  rect(glass, 0, 0, gs, gs, imgFill('vignette', 'FILL', 0.65), { name: 'vignette' });
  rect(glass, 0, 0, gs, gs * 0.2, lin([[0, '#ffffff', 0.05], [1, '#ffffff', 0]]), { name: 'glass sheen' });
  if (o.glare !== null && o.glare !== undefined) {
    // a soft diagonal band; its centre travels from beyond the top-left to beyond the bottom-right corner
    const t = o.glare, w = gs * 0.34, h = gs * 2.2, th = -28, rr = th * Math.PI / 180;
    const cx = -gs * 0.35 + t * gs * 1.7, cy = gs / 2;
    const band = rect(glass, 0, 0, w, h, lin([[0, '#ffffff', 0], [0.5, '#f4e9ec', 0.16], [1, '#ffffff', 0]], HORZ), { name: 'glare · animated' });
    band.rotation = th;
    const c = Math.cos(rr), sn = Math.sin(rr);
    band.x = cx - (c * w / 2 + sn * h / 2); band.y = cy - (-sn * w / 2 + c * h / 2);
  }
  return bez;
}
