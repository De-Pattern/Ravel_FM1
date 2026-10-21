// SPDX-License-Identifier: GPL-3.0-only
// Smoke test for the browser emulator: boot, play the demo, play a note, edit a step, catch the
// tape, save a project. node web/emu/test.mjs
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const XF = createRequire(import.meta.url)('./xfer.js');
// a project (slot 4) and a bank (C) kept by v0.6, laid out with 96 track parameters
const FIX = XF.fileDecode(readFileSync(new URL('../../tests/fixtures/ravel-v0.6-format14.ravel', import.meta.url)));
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
{                                                           // the hidden game's level 2: inside the call, a portal lets a ghoul out
  rv.web_moom_level(1); for (let k = 0; k < 16; k++) rv.rv_run(250);
  rv.web_moom_pose(9.5, 6.2, -Math.PI / 2); for (let k = 0; k < 12; k++) rv.rv_run(250);
  const px = new Uint16Array(rv.memory.buffer, rv.rv_fb(), 240 * 240);
  check(rv.web_moom_on() === 1 && new Set(px).size > 40, 'MOOM level 2 runs');
  rv.web_moom(); rv.rv_run(50);
  check(rv.web_moom_on() === 0, 'MOOM leaves');
}
{                                                           // the FILTER ENV: a project from before it (format 11) gets one copied from the volume envelope
  const ATK = 23, DEC = 26, SUS = 27, REL = 29, FATK = 81, FDEC = 83, FSUS = 84, FREL = 88, FLOOP = 91;
  const pj = FIX.find(e => e.slot === 3).data;                 // a 96-parameter project, marked as from before format 12
  new Uint8Array(rv.memory.buffer, rv.rv_store(3), pj.length).set(pj); rv.rv_store_set_len(3, pj.length);
  const dv = new DataView(rv.memory.buffer, rv.rv_store(3), pj.length), tp0 = (p, v) => dv.setInt16(20 + p * 2, v, true);
  dv.setUint32(4, 11, true);
  tp0(ATK, 30); tp0(DEC, 50); tp0(SUS, 40); tp0(REL, 70); tp0(FATK, 99); tp0(FLOOP, 1);
  rv.web_load(3);
  check(rv.web_tp(0, FATK) === 30 && rv.web_tp(0, FDEC) === 50 && rv.web_tp(0, FSUS) === 40 && rv.web_tp(0, FREL) === 70 && rv.web_tp(0, FLOOP) === 0,
        'an older project gets a FILTER ENV from its envelope');
}
{                                                           // a sound kept before the FILTER ENV (bank version 2) gets one from its envelope
  const bk = FIX.find(e => e.slot === 11).data;                // bank C as v0.6 kept it (96 parameters a sound), marked version 2
  rv.web_bank_select(0);
  new Uint8Array(rv.memory.buffer, rv.rv_store(11), bk.length).set(bk); rv.rv_store_set_len(11, bk.length);
  const st = new DataView(rv.memory.buffer, rv.rv_store(11), bk.length), snd = 8 + 4 * 324 + 16;
  st.setUint32(4, 2, true); st.setInt16(snd + 23 * 2, 44, true); st.setInt16(snd + 81 * 2, 99, true);   // ATK 44, a stray F ATK
  rv.web_bank_select(2);
  check(rv.web_bank_used(4) === 1, 'a version 2 bank still loads');
  rv.web_bank_load(1, 4); rv.web_bank_select(0);
  check(rv.web_tp(1, 81) === 44, 'its sounds get a FILTER ENV from their envelope');
}
{                                                           // the LIBRARY: step envelopes, chained, and a step that starts one
  rv.rv_boot(); rv.rv_run(250);
  const run = ms => { while (ms > 0) { rv.rv_run(Math.min(ms, 250)); ms -= 250; } };
  rv.web_senv_start(0, 0); run(250);                          // 1 RAMP UP: four steps (0.5 s at 120)
  const mid = rv.web_senv(0); run(400);
  check(mid > 0.35 && mid < 0.65 && rv.web_senv(0) > 0.99 && rv.web_senv_env(0) === -1, `RAMP UP climbs over four steps (${mid.toFixed(2)} halfway) and holds`);
  rv.web_senv_start(0, 10); run(1100);                        // 11 BUILD: eight steps up, then 12 FALL
  check(rv.web_senv_env(0) === 11, 'BUILD hands over to FALL (a chain)');
  run(300);
  check(rv.web_senv(0) < 0.05, 'and FALL comes down');
  rv.web_step_set(0, 0, 1, 3);                                // step 1 starts envelope 3 (BLIP)
  rv.web_transport(1); let seen = -1; for (let k = 0; k < 40 && seen < 0; k++) { rv.rv_run(25); if (rv.web_senv_env(0) >= 0) seen = rv.web_senv_env(0); }
  check(seen === 2, 'a step starts its LIBRARY envelope');
  rv.web_transport(0); run(50);
  check(rv.web_senv(0) === 0, 'stopping puts it back');
}
{                                                           // chords stay clean at the defaults: the track's DRIVE is 0
  const chord = (drive) => {
    rv.rv_boot(); rv.rv_run(250); rv.web_set_kind(0, 0, 0);
    if (drive >= 0) { rv.web_tp_set(0, 41, drive); rv.web_gp_set(47, 0); }
    rv.rv_run(20); rv.rv_set_keys((1 << 12) | (1 << 16) | (1 << 19) | (1 << 23));
    const L = []; for (let t = 0; t < 800; t += 20) { const fr = rv.rv_run(20), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), fr * 2); for (let i = 0; i < fr; i++) L.push(a[2 * i]); }
    rv.rv_set_keys(0); rv.rv_run(300); return L.slice(4410);
  };
  const clean = chord(0), def = chord(-1);
  let ab = 0, aa = 0, bb = 0; for (let i = 0; i < clean.length; i++) { ab += clean[i] * def[i]; aa += clean[i] * clean[i]; bb += def[i] * def[i]; }
  let r = 0; const g = ab / aa; for (let i = 0; i < clean.length; i++) { const d = def[i] - g * clean[i]; r += d * d; }
  const db = 10 * Math.log10(r / bb + 1e-12);
  check(rv.web_tp(0, 41) === 0 && db < -35, `a four-note VA chord at the defaults stays clean (${db.toFixed(1)} dB of distortion)`);
}
{                                                           // a matrix row can move another row's AMOUNT
  rv.rv_boot(); rv.rv_run(250);
  rv.web_mat_set(0, 1, rv.web_pid_t(0, 38), 0);               // row 1: LFO 1 into T1 CUTOFF, at 0
  rv.web_mat_set(1, 7, rv.web_pid_mat(0), 63);                // row 2: MACRO 1 into ROW 1's AMOUNT
  rv.web_gp_set(64, 0); rv.rv_run(50);
  const a0 = rv.web_mat_amt(0);
  rv.web_gp_set(64, 127); rv.rv_run(50);
  const a1 = rv.web_mat_amt(0);
  rv.web_mat_set(2, 7, rv.web_pid_mat(1), -63); rv.rv_run(50);   // row 3: MACRO 1 takes row 2 back down: a chain
  const a2 = rv.web_mat_amt(0), b2 = rv.web_mat_amt(1);
  check(a0 === 0 && a1 >= 60 && b2 <= 1 && a2 <= 2, `MACRO 1 opens row 1's AMOUNT (${a0} to ${a1}); a third row closing row 2 closes it again (${b2}, ${a2})`);
}
{                                                           // projects and sounds: a file, and over SysEx to a second Ravel (as to an FM-1)
  const { createRequire } = await import('node:module');
  const X = createRequire(import.meta.url)('./xfer.js');
  const A = rv, B = new WebAssembly.Instance(mod, {}).exports;
  A.rv_boot(); A.rv_run(250); A.web_demo(0); A.rv_run(100);
  check(A.web_save(2) === 0, 'a project saved in slot 3');
  const nb = new Uint8Array(A.memory.buffer, A.web_bank_name_buf(), 16); nb.set(new TextEncoder().encode('MOVED PAD\0'));
  A.web_bank_select(1); check(A.web_bank_store(0, 5) === 0, 'a sound kept in bank B, slot 6');
  const entries = [];                                           // the emulator's Export: the stores as they are
  for (let s = 0; s < A.rv_store_slots(); s++) { const n = A.rv_store_len(s); if (n >= 0 && X.SLOTS.includes(s)) entries.push({ slot: s, data: new Uint8Array(A.memory.buffer, A.rv_store(s), n).slice() }); }
  const file = X.fileEncode(entries), back = X.fileDecode(file);
  check(back.length === entries.length && back.every((e, i) => e.slot === entries[i].slot && X.crc32(e.data) === X.crc32(entries[i].data)), `a .ravel file holds them (${entries.length} slots, ${file.length} bytes)`);
  let bad = 0; try { const f2 = file.slice(); f2[f2.length - 1] ^= 1; X.fileDecode(f2); } catch (e) { bad = 1; }
  check(bad, 'a damaged file is refused');
  B.rv_boot(); B.rv_run(250);
  const pending = [];                                           // B is the "FM-1": messages in, answers out
  const sess = new X.Session(w => { const buf = new Uint8Array(B.memory.buffer, B.web_sysex_in_buf(), w.length); buf.set(w); B.web_sysex_in(w.length);
    const n = B.web_sysex_out_len(), o = new Uint8Array(B.memory.buffer, B.web_sysex_out(), n).slice(); B.web_sysex_out_clear();
    let st = 0; for (let i = 0; i < n; i++) if (o[i] === 0xF7) { pending.push(o.slice(st, i + 1)); st = i + 1; }
    setTimeout(() => { while (pending.length) sess.receive(pending.shift()); }, 0); }, { timeout: 500 });
  await sess.restore(back);
  const info = await sess.info();
  check(info.length === entries.length, `sent over SysEx: the second Ravel lists ${info.length} slots`);
  check(B.web_load(2) === 0, 'and loads the project');
  B.web_bank_select(1);
  check(B.web_bank_used(5) === 1 && cstr2(B, B.web_bank_name(5)) === 'MOVED PAD', 'and has the sound in bank B');
  const again = await sess.backup();
  check(again.length === entries.length && again.every(e => X.crc32(e.data) === X.crc32(entries.find(f => f.slot === e.slot).data)), 'and gives it all back, unchanged');
  let refused = 0; try { await sess.writeSlot(0, new Uint8Array(40000)); } catch (e) { refused = 1; }
  check(refused, 'a project too big for its slot is refused');
}
function cstr2(I, p) { const m = new Uint8Array(I.memory.buffer, p); let n = 0; while (m[n]) n++; return new TextDecoder().decode(m.subarray(0, n)); }
{                                                           // a project and a bank kept by v0.6 (96 track parameters) load as they were
  const { createRequire } = await import('node:module');
  const X = createRequire(import.meta.url)('./xfer.js');
  const C = new WebAssembly.Instance(mod, {}).exports;
  C.rv_boot(); C.rv_run(250);
  for (const e of X.fileDecode(readFileSync(new URL('../../tests/fixtures/ravel-v0.6-format14.ravel', import.meta.url)))) {
    new Uint8Array(C.memory.buffer, C.rv_store(e.slot), e.data.length).set(e.data); C.rv_store_set_len(e.slot, e.data.length);
  }
  check(C.web_load(3) === 0 && C.web_tp(0, 38) === 77, 'an older project loads, its settings in place');
  check(C.web_mat_dst(0) === C.web_pid_t(2, 39) && C.web_mat_dst(1) === C.web_pid_mat(0) && C.web_mat_dst(2) === C.web_pid_g(47), 'its matrix still points where it did (a track, a row, a global)');
  check(C.web_tp(0, 97) === 0, 'and the FILTER ENV\'s new AMOUNT starts at 0');
  C.web_bank_select(2);
  check(C.web_bank_used(4) === 1 && cstr2(C, C.web_bank_name(4)) === 'OLD SOUND', 'an older bank reads, its sound named');
  C.web_bank_load(1, 4);
  check(C.web_tp(1, 38) === 77, 'and the sound loads as it was kept');
}
{                                                           // the FILTER ENV into its own destination: looping, a per-track LFO on PAN
  rv.rv_boot(); rv.rv_run(250);
  const PAN = 52, FATK = 81, FDEC = 83, FSUS = 84, FLOOP = 91, FDST = 96, FAMT = 97;
  rv.web_tp_set(0, FATK, 20); rv.web_tp_set(0, FDEC, 30); rv.web_tp_set(0, FSUS, 0); rv.web_tp_set(0, FLOOP, 1);
  rv.web_tp_set(0, FDST, PAN); rv.web_tp_set(0, FAMT, 63);
  rv.rv_set_keys(1 << 12);
  let lo = 1e9, hi = -1e9;
  for (let k = 0; k < 120; k++) { rv.rv_run(10); const v = rv.web_eff(0, PAN); lo = Math.min(lo, v); hi = Math.max(hi, v); }
  rv.rv_set_keys(0); rv.rv_run(300);
  check(hi - lo > 30, `F AMT into PAN: the pan swings with the looping FILTER ENV (${lo.toFixed(0)} to ${hi.toFixed(0)})`);
  rv.web_tp_set(0, FAMT, 0); rv.rv_set_keys(1 << 12); rv.rv_run(300);
  check(Math.abs(rv.web_eff(0, PAN) - rv.web_tp(0, PAN)) < 0.5, 'and at F AMT 0 it leaves PAN alone');
  rv.rv_set_keys(0); rv.rv_run(300);
}
{                                                           // a kit lane at its own rate: twice as fast, half as fast, as fast as the track
  rv.rv_boot(); rv.rv_run(250);
  rv.web_set_kind(3, 3, 1);                                   // T4: a PERC kit
  for (let st = 0; st < 16; st++) for (let l = 0; l < 3; l++) rv.web_kit_hit(3, st, l);
  rv.web_kit_rate(3, 0, 6); rv.web_kit_rate(3, 1, 2);        // lane 1 x2, lane 2 x1/2, lane 3 the track's
  rv.web_transport(1);
  const adv = [0, 0, 0], last = [-1, -1, -1];                // lane steps walked (a lane at x2 plays two in each of the track's)
  for (let k = 0; k < 200; k++) { rv.rv_run(10); for (let l = 0; l < 3; l++) { const s = rv.web_lane_step(3, l); if (s !== last[l]) { if (last[l] >= 0) adv[l] += (s - last[l] + 16) % 16; last[l] = s; } } }
  rv.web_transport(0); rv.rv_run(100);
  check(adv[2] > 8 && Math.abs(adv[0] / adv[2] - 2) < 0.3 && Math.abs(adv[1] / adv[2] - 0.5) < 0.2, `lane rates: x2, x1/2 and the track's walk ${adv[0]}, ${adv[1]} and ${adv[2]} steps in 2 s`);
}
process.exit(fails ? 1 : 0);
