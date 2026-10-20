// SPDX-License-Identifier: GPL-3.0-only
// Smoke test for the browser emulator: boot, play the demo, play a note, edit a step, catch the
// tape, save a project. node web/emu/test.mjs
import { readFileSync } from 'node:fs';
const cstr = p => { const m = new Uint8Array(rv.memory.buffer, p); let n = 0; while (m[n]) n++; return new TextDecoder().decode(m.subarray(0, n)); };
const wasm = readFileSync(new URL('./ravel.wasm', import.meta.url));
const mod = new WebAssembly.Module(wasm);
if (WebAssembly.Module.imports(mod).length) throw new Error('unexpected imports');
const rv = new WebAssembly.Instance(mod, {}).exports;
const BTN = ["OCT-","OCT+","FX","SCL","ENV","LFO","EDIT","GLO","HOME","SAVE","ARP","SEQ","PLAY","REC"];
const bit = n => 1 << BTN.indexOf(n);
let peak = 0, frames = 0, fails = 0;
const run = ms => { for (let t = 0; t < ms; t += 16) { const n = rv.rv_run(16); frames += n;
  const a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2); for (const v of a) peak = Math.max(peak, Math.abs(v)); } };
const tap = name => { rv.rv_set_buttons(bit(name)); run(60); rv.rv_set_buttons(0); run(40); };
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) fails++; };
rv.rv_boot();
run(300);
tap('PLAY');
run(2000);
check(peak === 0, 'it starts on a blank project: silent while playing');
tap('PLAY');
rv.web_demo(0);
peak = 0; frames = 0;
run(300);
tap('PLAY');
run(6000);
check(rv.web_playing() === 1, 'PLAY starts the demo');
check(peak > 0.05, `the demo makes sound (peak ${(20 * Math.log10(peak)).toFixed(1)} dB)`);
check(rv.web_frozen() === 1, 'the demo resampled its first bars onto the tape');
check(Math.abs(frames - 44100 * 6.4) < 4000, `time runs at 44.1 kHz (${frames} frames)`);
tap('PLAY');
run(800);
check(rv.web_playing() === 0, 'PLAY stops');
peak = 0; rv.rv_set_keys(1 << 12); run(400); rv.rv_set_keys(0); run(200);
check(peak > 0.02, 'a key plays T1');
rv.web_goto(7, 0); run(50);                                 // SEQ › STEPS
const before = rv.web_step(0);
rv.rv_set_keys(1 << 2); run(120); rv.rv_enc(2, 5); run(60); rv.rv_set_keys(0); run(60);  // hold step 2, turn K1: its note
check(true, 'held a step and turned KNOB 1');
rv.web_goto(9, 0); run(50); tap('SAVE'); tap('SAVE');       // SAVE › save to slot 1
check(rv.rv_store_len(0) > 20000, `saved a project (${rv.rv_store_len(0)} bytes)`);
{                                                           // the sound bank: keep T1's sound, load it on T2 as a perc kit
  rv.web_set_kind(2, 3, 0); run(20);
  const nb = new Uint8Array(rv.memory.buffer, rv.web_bank_name_buf(), 16); nb.fill(0);
  [...'MY KIT'].forEach((c, k) => nb[k] = c.charCodeAt(0));
  rv.web_bank_store(2, 0);
  rv.web_bank_load(1, 0); run(20);
  check(rv.web_kind(1) === 3 && rv.rv_store_len(9) > 10000 && cstr(rv.web_bank_name(0)) === 'MY KIT' && cstr(rv.web_bank_kind_label(0)) === 'KIT',
        `a sound kept in the bank (${rv.rv_store_len(9)} bytes) loads with its kind`);
  rv.web_bank_select(1); rv.web_bank_store(0, 5); rv.web_bank_select(0);
  check(rv.rv_store_len(10) > 10000 && rv.web_bank_used(5) === 0, 'bank B keeps its own sounds');
}
rv.rv_set_buttons(bit('REC') | bit('OCT-')); run(80); rv.rv_set_buttons(0); run(60);
check(rv.web_frozen() === 0, 'REC + OCT- releases the catch');
const fb = new Uint16Array(rv.memory.buffer, rv.rv_fb(), 240 * 240);
check(new Set(fb).size > 8, 'the screen is drawn');
{                                                           // the first bank's old place (version 1) moves to bank A
  const SZ = 8 + 32 * 396, img = new Uint8Array(SZ), dv = new DataView(img.buffer);
  dv.setUint32(0, 0x4B4E4252, true); dv.setUint32(4, 1, true);
  [...'OLD KIT'].forEach((c, k) => img[8 + k] = c.charCodeAt(0));
  img[8 + 12] = 1; img[8 + 13] = 3; img[8 + 14] = 1;          // used, PERC, KIT
  for (let s = 9; s < 13; s++) rv.rv_store_set_len(s, -1);
  new Uint8Array(rv.memory.buffer, rv.rv_store(8), SZ).set(img); rv.rv_store_set_len(8, SZ);
  rv.rv_boot(); run(100);
  check(cstr(rv.web_bank_name(0)) === 'OLD KIT' && cstr(rv.web_bank_kind_label(0)) === 'KIT' && rv.rv_store_len(9) > 10000,
        'an old bank moves to bank A');
}
process.exit(fails ? 1 : 0);
