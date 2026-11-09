// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS);
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP);
  return out;
}

// ---- the way back to stock: M-VAVE's flash.bin with Ravel's own loader (ota-stock.bin) in place of
// M-VAVE's, which a Felucca-family firmware can't hand over to (the FM-1 switches off). The loader
// writes the app area only, checks that the unit's flash head is that stock version's, and puts
// stock's settings areas back to factory state (firmware/fm1/loader/ldr_core.c, LDR_TO_STOCK).
// The same layout as tools/fm1pkg_make.py ufw(); tests/fm1/pkg_test.mjs checks they agree.
const CRC_T = (() => {
  const t = new Uint16Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i << 8;
    for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
    t[i] = c;
  }
  return t;
})();
function crc16(d, c = 0) {
  for (let i = 0; i < d.length; i++) c = ((c << 8) & 0xFFFF) ^ CRC_T[((c >> 8) ^ d[i]) & 0xFF];
  return c;
}
function jlEnc(b, off, n) {            // JieLi ENC stream cipher, key 0xFFFF (in place)
  let key = 0xFFFF;
  for (let i = 0; i < n; i++) {
    b[off + i] ^= key & 0xFF;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
}
const rd16 = (b, o) => b[o] | (b[o + 1] << 8);
const rd32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

// the package's files by type (0 flash.bin, 100 ota.bin), CRC-checked
export function filesOf(fwsc) {
  const lg = logicalImage(fwsc), h = lg.slice(0, 0x40);
  jlEnc(h, 0, 0x40);
  const n = rd16(h, 8), out = {};
  if (String.fromCharCode(...h.subarray(16, 22)) !== "AC791N" || !n || n > 11) throw new Error("not an FM-1 package (header)");
  for (let i = 0; i < n; i++) {
    const e = lg.slice(0x40 + i * 0x50, 0x90 + i * 0x50);
    jlEnc(e, 0, 0x50);
    const typ = rd16(e, 0), dcrc = rd16(e, 4), off = rd32(e, 8), size = rd32(e, 12);
    const d = lg.slice(off, off + size);
    if ((typ === 0 || typ === 100) && (d.length !== size || crc16(d) !== dcrc)) throw new Error("the package is damaged (CRC)");
    out[typ] = d;
  }
  return out;
}

// a .fwsc from flash.bin + ota.bin with the identity product ("FM-1_015")
export function buildPackage(flash, ota, product) {
  const files = [[0, "flash.bin", flash], [100, "ota.bin", ota]];
  let off = 0x400;
  const ents = [];
  files.forEach(([typ, name, d], i) => {
    off = (off + 0xFF) & ~0xFF;
    const e = new Uint8Array(0x50), v = new DataView(e.buffer);
    v.setUint16(0, typ, true); v.setUint16(2, i, true); v.setUint16(4, crc16(d), true); v.setUint16(6, 0, true);
    v.setUint32(8, off, true); v.setUint32(12, d.length, true); v.setUint32(16, (d.length + 31) & ~31, true);
    for (let k = 0; k < name.length; k++) e[0x40 + k] = name.charCodeAt(k);
    ents.push([e, off, d]);
    off += d.length;
  });
  const total = off, hdr = new Uint8Array(0x40), hv = new DataView(hdr.buffer);
  hv.setUint32(4, total, true); hv.setUint16(8, files.length, true);
  hv.setUint16(10, 0x0004, true); hv.setUint16(12, 0x0200, true); hv.setUint16(14, 0, true);
  for (let k = 0; k < 6; k++) hdr[16 + k] = "AC791N".charCodeAt(k);
  const lst = new Uint8Array(ents.length * 0x50);
  ents.forEach(([e], i) => { const x = e.slice(); jlEnc(x, 0, 0x50); lst.set(x, i * 0x50); });
  hv.setUint16(2, crc16(lst), true);
  hv.setUint16(0, crc16(hdr.subarray(2, 0x40)), true);
  jlEnc(hdr, 0, 0x40);
  const lg = new Uint8Array(total).fill(0xFF);
  lg.set(hdr, 0); lg.set(lst, 0x40);
  for (const [, o, d] of ents) lg.set(d, o);
  if (product.length > 20) throw new Error("identity too long");
  const raw = new Uint8Array(total + BLOCKS);
  for (let i = 0; i < BLOCKS; i++) {
    const m = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
    if (i < product.length && m === 0x7D) throw new Error("identity has a character the format can't hold");
    raw.set(lg.subarray(i * KEEP, (i + 1) * KEEP), i * BLK);
    raw[i * BLK + KEEP] = m;
  }
  raw.set(lg.subarray(BLOCKS * KEEP), BLOCKS * BLK);
  return raw;
}

// stock .fwsc + ota-stock.bin -> the package "Back to the stock firmware" sends
export function stockBack(stockFwsc, otaStock) {
  const f = filesOf(stockFwsc);
  if (!f[0] || f[0].length !== 0x93000) throw new Error("not the stock firmware this page knows (flash.bin size)");
  if (!String.fromCharCode(...otaStock.subarray(0x48, 0x48 + 64)).includes("FELUCCA-LOADER-1")) throw new Error("ota-stock.bin is not Ravel's loader");
  return buildPackage(f[0], otaStock, productOf(stockFwsc));
}
