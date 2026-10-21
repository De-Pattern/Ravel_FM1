/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: projects and sounds as a file, and to and from an FM-1 over USB-MIDI SysEx.
 * The computer's side of firmware/app/rv_xfer.c (the message format is described there).
 * The file (.ravel): "RAVELDAT", version 1, count, then for each slot: slot, length, CRC-32, the bytes
 * (all little-endian). The emulator page and the tests use it; tools/fm1_data.py speaks the same. */
(function (root) {
  "use strict";
  const STORES = 13, PROJECTS = 7, BANK0 = 9, BANKS = 4, CHUNK = 256;
  const SLOTS = [0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12];
  const slotName = s => s < PROJECTS ? `project ${s + 1}` : `bank ${"ABCD"[s - BANK0]}`;

  let CRC_T = null;
  function crc32(b) {
    if (!CRC_T) { CRC_T = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; CRC_T[n] = c >>> 0; } }
    let c = 0xFFFFFFFF;
    for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  function pack7(b) {
    const o = []; let acc = 0, nb = 0;
    for (const x of b) { acc |= x << nb; nb += 8; while (nb >= 7) { o.push(acc & 0x7F); acc >>>= 7; nb -= 7; } }
    if (nb) o.push(acc & 0x7F);
    return o;
  }
  function unpack7(b) {
    const o = []; let acc = 0, nb = 0;
    for (const x of b) { acc |= (x & 0x7F) << nb; nb += 7; if (nb >= 8) { o.push(acc & 255); acc >>>= 8; nb -= 8; } }
    return o;
  }
  /* one message: F0 pack7(00 59 cmd len24 body ~sum) F7 */
  function frame(cmd, body) {
    let s = 0; for (const x of body) s += x;
    const m = [0, 0x59, cmd, body.length & 255, (body.length >> 8) & 255, (body.length >> 16) & 255, ...body, (~s) & 255];
    return Uint8Array.from([0xF0, ...pack7(m), 0xF7]);
  }
  function parse(wire) {
    if (wire.length < 2 || wire[0] !== 0xF0 || wire[wire.length - 1] !== 0xF7) return null;
    const d = unpack7(wire.subarray ? wire.subarray(1, wire.length - 1) : wire.slice(1, -1));
    if (d.length < 7 || d[0] !== 0 || d[1] !== 0x59 || (d[3] | d[4] << 8 | d[5] << 16) + 7 !== d.length) return null;
    let s = 0; for (let i = 6; i < d.length - 1; i++) s += d[i];
    if (((~s) & 255) !== d[d.length - 1]) return null;
    return { cmd: d[2], body: Uint8Array.from(d.slice(6, -1)) };
  }
  const u32 = v => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
  const rd32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

  /* the file */
  function fileEncode(entries) {           /* [{slot, data: Uint8Array}] */
    const parts = [new TextEncoder().encode("RAVELDAT"), Uint8Array.from([...u32(1), ...u32(entries.length)])];
    for (const e of entries) { parts.push(Uint8Array.from([...u32(e.slot), ...u32(e.data.length), ...u32(crc32(e.data))])); parts.push(e.data); }
    const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
  }
  function fileDecode(buf) {
    const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
    if (b.length < 16 || new TextDecoder().decode(b.subarray(0, 8)) !== "RAVELDAT") throw new Error("not a Ravel file");
    if (rd32(b, 8) !== 1) throw new Error("a newer Ravel file than this version reads");
    const n = rd32(b, 12), out = []; let o = 16;
    for (let i = 0; i < n; i++) {
      if (o + 12 > b.length) throw new Error("the file is cut short");
      const slot = rd32(b, o), len = rd32(b, o + 4), crc = rd32(b, o + 8); o += 12;
      if (o + len > b.length) throw new Error("the file is cut short");
      const data = b.slice(o, o + len); o += len;
      if (crc32(data) !== crc) throw new Error(`${slotName(slot)} is damaged in the file`);
      if (!SLOTS.includes(slot)) throw new Error("the file has a slot this version doesn't know");
      out.push({ slot, data });
    }
    return out;
  }

  /* talking to a device: send(Uint8Array) puts one message on the wire; feed each message that comes back
   * into session.receive(). Every request waits for its answer (same command), with retries. */
  class Session {
    constructor(send, opts = {}) { this.send = send; this.wait = null; this.timeout = opts.timeout || 2000; this.tries = opts.tries || 3; }
    receive(wire) {
      const m = parse(wire);
      if (m && this.wait && m.cmd === this.wait.cmd) { const w = this.wait; this.wait = null; clearTimeout(w.timer); w.resolve(m.body); }
    }
    async ask(cmd, body) {
      for (let t = 0; t < this.tries; t++) {
        const got = await new Promise(resolve => {
          this.wait = { cmd, resolve, timer: setTimeout(() => { this.wait = null; resolve(null); }, this.timeout) };
          this.send(frame(cmd, body));
        });
        if (got) return got;
      }
      throw new Error("the FM-1 doesn't answer (is Ravel running on it, and connected by USB?)");
    }
    async info() {
      const b = await this.ask(0x60, []);
      if (String.fromCharCode(b[0], b[1], b[2], b[3]) !== "RVX1") throw new Error("the FM-1 answered, but not as Ravel");
      const out = [];
      for (let s = 0; s < b[4]; s++) { const len = rd32(b, 5 + s * 8); if (len !== 0xFFFFFFFF && SLOTS.includes(s)) out.push({ slot: s, len, crc: rd32(b, 9 + s * 8) }); }
      return out;
    }
    async readSlot(slot, len, progress) {
      const data = new Uint8Array(len);
      for (let off = 0; off < len; off += CHUNK) {
        const n = Math.min(CHUNK, len - off), r = await this.ask(0x61, [slot, ...u32(off), n & 255, n >> 8]);
        if (r[5] !== 0 || r.length !== 6 + n) throw new Error(`reading ${slotName(slot)} failed`);
        data.set(r.subarray(6), off);
        if (progress) progress(off + n, len);
      }
      return data;
    }
    async writeSlot(slot, data, progress) {
      let r = await this.ask(0x62, [slot, ...u32(data.length)]);
      if (r[1] !== 0) throw new Error(`${slotName(slot)} won't fit on the FM-1 (or its flash failed)`);
      for (let off = 0; off < data.length; off += CHUNK) {
        r = await this.ask(0x63, [slot, ...u32(off), ...data.subarray(off, off + CHUNK)]);
        if (r[5] !== 0) throw new Error(`writing ${slotName(slot)} failed`);
        if (progress) progress(off + Math.min(CHUNK, data.length - off), data.length);
      }
      r = await this.ask(0x64, [slot, ...u32(data.length), ...u32(crc32(data))]);
      if (r[1] !== 0) throw new Error(`${slotName(slot)} didn't check out after writing; try again`);
    }
    async backup(progress) {           /* everything the device holds -> entries */
      const list = await this.info(), out = [];
      for (const e of list) { out.push({ slot: e.slot, data: await this.readSlot(e.slot, e.len, (a, b) => progress && progress(e.slot, a, b)) }); }
      for (const e of out) { const want = list.find(l => l.slot === e.slot).crc; if (crc32(e.data) !== want) throw new Error(`${slotName(e.slot)} came over damaged; try again`); }
      return out;
    }
    async restore(entries, progress) {
      for (const e of entries) await this.writeSlot(e.slot, e.data, (a, b) => progress && progress(e.slot, a, b));
    }
  }
  const api = { crc32, frame, parse, fileEncode, fileDecode, Session, SLOTS, slotName, STORES };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.RavelXfer = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
