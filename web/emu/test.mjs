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
  { let on = 0, same = 1; for (let t = 0; t < 8; t++) for (let st = 0; st < 64; st++) if (C.web_step_info(t, st, 0)) { on++; if (C.web_step_info(t, st, 2) !== 1) same = 0; }
    check(on > 10 && same, `and its steps come through, single notes (${on} steps)`); }
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
{                                                           // recording a chord: three keys down together land on one step
  rv.rv_boot(); rv.rv_run(250);
  rv.web_gp_set(102, 0);                                     // (no count-in)
  tap('PLAY'); tap('REC'); run(300);
  rv.rv_set_keys((1 << 12) | (1 << 16) | (1 << 19)); run(200); rv.rv_set_keys(0); run(400);
  rv.rv_set_keys(1 << 14); run(150); rv.rv_set_keys(0); run(300);
  tap('REC'); tap('PLAY');
  let chords = 0, singles = 0; for (let st = 0; st < 64; st++) if (rv.web_step_info(0, st, 0)) { const n = rv.web_step_info(0, st, 2); if (n === 3) chords++; else if (n === 1) singles++; }
  check(chords === 1 && singles === 1, `a chord records onto one step (${chords} three-note step, ${singles} single)`);
  peak = 0; tap('PLAY'); rv.rv_set_keys(0);
  let most = 0; for (let k = 0; k < 100; k++) { run(16); most = Math.max(most, rv.web_cpu()); }
  tap('PLAY');
  check(peak > 0.01 && most >= 3, `and plays back, all its notes at once (${most} voices)`);
}
{                                                           // the SPACE modes: CLOUDS and 12-BIT ring and die away; none go NaN; ROOM is the default
  const tail = (mode, size, secs) => {
    rv.rv_boot(); rv.rv_run(250);
    rv.web_gp_set(100, mode); rv.web_gp_set(44, size); rv.web_tp_set(0, 56, 110); rv.rv_run(20);
    rv.rv_set_keys(1 << 12); rv.rv_run(400); rv.rv_set_keys(0);
    let nan = false, e = 0, n = 0;
    for (let t = 0; t < secs * 1000; t += 10) { const k = rv.rv_run(10), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), k * 2); for (let i = 0; i < k * 2; i++) { if (!(a[i] === a[i])) nan = true; if (t >= (secs - 1) * 1000) { e += a[i] * a[i]; n++; } } }
    return [nan, Math.sqrt(e / Math.max(1, n))];
  };
  const [cn, ct] = tail(1, 90, 12), [qn, qt] = tail(2, 90, 8), [rn] = tail(0, 70, 2);
  check(!cn && !qn && !rn, 'the SPACE modes stay finite');
  check(ct < 0.0005, `CLOUDS dies away (rms ${ct.toFixed(5)} after 12 s)`);
  check(qt < 0.0005, `12-BIT dies away (rms ${qt.toFixed(5)} after 8 s)`);
}
{                                                           // effects start off: no sends, no master drive, ROOM; a preset keeps the track's sends
  const SENDS = [55, 56, 53, 54, 79, 46], GP_MDRIVE = 47, GP_REVMODE = 100;
  rv.rv_boot(); rv.rv_run(250);
  let any = 0; for (let t = 0; t < 8; t++) for (const p of SENDS) any += rv.web_tp(t, p);
  check(any === 0 && rv.web_gp(GP_MDRIVE) === 0 && rv.web_gp(GP_REVMODE) === 0, 'a new project feeds no effect: sends 0, MASTER DRIVE 0, SPACE ROOM');
  let wet = 0; for (let i = 0; i < rv.web_preset_count(); i++) { rv.web_preset_apply(0, i); for (const p of SENDS) wet += rv.web_tp(0, p); }
  rv.web_tp_set(0, 56, 33); rv.web_preset_apply(0, 3);
  check(wet === 0 && rv.web_tp(0, 56) === 33, `presets leave the sends alone (sum over all ${rv.web_preset_count()} presets ${wet}; a send set by hand stays)`);
  rv.web_demo(0); let dw = 0; for (let t = 0; t < 8; t++) dw += rv.web_tp(t, 56) + rv.web_tp(t, 55);
  check(dw > 0, `a demo still brings its own sends (${dw})`);
}
{                                                           // the BED: silent at LEVEL 0, sounds when up, held down by busy tracks (DUCK)
  const GPB = 110;
  const level = (lvl, duck, kit) => {
    rv.rv_boot(); rv.rv_run(250);
    rv.web_gp_set(GPB, lvl); rv.web_gp_set(GPB + 1, 60); rv.web_gp_set(GPB + 4, duck);
    if (kit) { rv.web_set_kind(3, 3, 1); for (let st = 0; st < 16; st++) rv.web_kit_hit(3, st, 0); rv.web_transport(1); }
    rv.rv_run(2000);
    let e = 0, n = 0; for (let k = 0; k < 100; k++) { rv.rv_run(10); e += rv.web_live_bed(); n++; }
    rv.web_transport(0);
    return e / n;
  };
  const off = level(0, 0, 0), on = level(90, 0, 0), free = level(90, 0, 1), held = level(90, 127, 1);
  check(off < 1e-5 && on > 0.002, `the BED: silent at LEVEL 0, there when up (${on.toFixed(4)})`);
  check(held < free * 0.6, `DUCK holds it under a busy kit (${held.toFixed(4)} against ${free.toFixed(4)})`);
}
{                                                           // SPARK: T4's hits ring T5's strike body; without the route T5 stays silent
  const run = (route) => {
    rv.rv_boot(); rv.rv_run(250);
    rv.web_set_kind(3, 3, 1); for (const st of [0, 4, 8, 12]) rv.web_kit_hit(3, st, 0);
    rv.web_tp_set(4, 51, 110);
    if (route) rv.web_route_set(3, 4, 7, 60);
    rv.web_transport(1);
    let m = 0; for (let k = 0; k < 200; k++) { rv.rv_run(10); m = Math.max(m, rv.web_meter(4)); }
    rv.web_transport(0); rv.rv_run(200);
    return m;
  };
  const quiet = run(0), rung = run(1);
  check(quiet < 0.001 && rung > 0.01, `SPARK: T5 rings only when T4 taps it (${rung.toFixed(3)} against ${quiet.toFixed(4)})`);
}
{                                                           // scene GLIDE: the level is part way there mid-glide, there at the end
  rv.rv_boot(); rv.rv_run(250);
  rv.web_tp_set(0, 51, 20); rv.web_scene_store(0);
  rv.web_tp_set(0, 51, 120); rv.web_scene_store(1);
  rv.web_tp_set(0, 51, 20);
  rv.web_gp_set(117, 2);                                      // GLIDE 2 BARS
  rv.web_transport(1); rv.rv_run(100);
  rv.web_scene_launch(1);
  let mid = -1;
  for (let k = 0; k < 900 && mid < 0; k++) { rv.rv_run(10); const g = rv.web_live_glide(); if (g > 0.4 && g < 0.6) mid = rv.web_tp(0, 51); }
  for (let k = 0; k < 800; k++) rv.rv_run(10);
  const end = rv.web_tp(0, 51);
  rv.web_transport(0);
  check(mid > 40 && mid < 100 && end === 120, `a scene glides: level ${mid} half way, ${end} at the end`);
}
{                                                           // cuts in every shape and size, and bouncing, moving ratchets: all finite
  rv.rv_boot(); rv.rv_run(250);
  for (let st = 0; st < 16; st++) rv.web_step_set(0, st, 1, 0);
  rv.web_tp_set(0, 55, 0);
  for (let st = 0; st < 16; st++) { rv.web_step_byte(0, st, 1, st % 4 ? (1 + st % 3) | ((st % 3) << 2) | ((st % 8) << 4) : 0); rv.web_step_byte(0, st, 0, (st & 7) | ((st % 4) << 3) | ((st % 4) << 6)); }
  rv.web_set_kind(3, 3, 1); for (let st = 0; st < 16; st++) { rv.web_kit_hit(3, st, st % 6); rv.web_step_byte(3, st, 0, 7 | (3 << 3) | ((st % 4) << 6)); rv.web_step_byte(3, st, 4, 63); }
  rv.web_tp_set(3, 98, 127);
  rv.web_gp_set(109, 2); rv.web_punch(2, 1);
  let bad = false, pk = 0;
  rv.web_transport(1);
  for (let k = 0; k < 600; k++) { const n = rv.rv_run(10), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2); for (let i = 0; i < n * 2; i++) { if (!(a[i] === a[i])) bad = true; pk = Math.max(pk, Math.abs(a[i])); } }
  rv.web_punch(2, 0); rv.web_transport(0);
  check(!bad && pk > 0.01 && pk < 1.5 && rv.web_fault_count() === 0, `cut shapes, BOUNCE and R MOVE ratchets, GRAIN: finite, no faults (peak ${pk.toFixed(2)})`);
}
{                                                           // TAPE: KEYS plays a caught sample across the keys (a chord: three voices); SHARDS, STRETCH, a tape kit; all finite
  rv.rv_boot(); rv.rv_run(250); rv.web_demo(0); rv.web_transport(1); rv.rv_run(1500);
  rv.web_resample(); for (let k = 0; k < 600; k++) rv.rv_run(10);
  rv.web_transport(0); for (let k = 0; k < 150; k++) rv.rv_run(10);
  rv.web_set_mute(0x7F); rv.web_set_track(7);
  const play = (keys, ms) => { let pk = 0, bad = false; rv.rv_set_keys(keys); for (let t = 0; t < ms; t += 10) { const n = rv.rv_run(10), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2); for (let i = 0; i < n * 2; i++) { if (!(a[i] === a[i])) bad = true; pk = Math.max(pk, Math.abs(a[i])); } } rv.rv_set_keys(0); for (let k = 0; k < 80; k++) rv.rv_run(10); return [pk, bad]; };
  rv.web_tp_set(7, 0, 2); rv.web_tp_set(7, 1, 0); rv.web_tp_set(7, 2, 1); rv.web_tp_set(7, 21, 3); rv.web_tp_set(7, 51, 110);
  const [kp, kb] = play((1 << 12) | (1 << 16) | (1 << 19), 600);
  let voices = 0; rv.rv_set_keys((1 << 12) | (1 << 16) | (1 << 19)); rv.rv_run(100); voices = rv.web_tape_voices(7); rv.rv_set_keys(0); for (let k = 0; k < 80; k++) rv.rv_run(10);
  rv.web_tp_set(7, 2, 4); rv.web_tp_set(7, 10, 1); rv.web_tp_set(7, 13, 5); rv.web_tp_set(7, 14, 60);
  const [sp, sb] = play(1 << 12, 800);
  rv.web_tp_set(7, 2, 3); rv.web_tp_set(7, 11, 40);
  const [tp, tb] = play(1 << 14, 800);
  rv.web_tape_kit(7); for (let st = 0; st < 64; st++) rv.web_step_set(7, st, 0, 0); for (let st = 0; st < 16; st++) rv.web_kit_hit(7, st, st % 6);
  rv.web_transport(1); let kitpk = 0, kitbad = false;
  for (let k = 0; k < 300; k++) { const n = rv.rv_run(10), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2); for (let i = 0; i < n * 2; i++) { if (!(a[i] === a[i])) kitbad = true; kitpk = Math.max(kitpk, Math.abs(a[i])); } }
  rv.web_transport(0);
  check(kp > 0.02 && voices === 3 && !kb, `TAPE KEYS: a chord from the tape, ${voices} voices (peak ${kp.toFixed(2)})`);
  check(sp > 0.02 && tp > 0.02 && !sb && !tb, `SHARDS and STRETCH sound (peaks ${sp.toFixed(2)}, ${tp.toFixed(2)})`);
  check(kitpk > 0.02 && !kitbad && rv.web_fault_count() === 0, `a tape kit plays its lanes (peak ${kitpk.toFixed(2)}), no faults`);
}
{                                                           // the tape delay at its extremes: grains, full feedback, saturation, age, double: bounded and finite
  rv.rv_boot(); rv.rv_run(250);
  for (let st = 0; st < 16; st += 4) rv.web_step_set(0, st, 1, 0);
  rv.web_tp_set(0, 55, 127);
  for (const [g, v] of [[40, 30], [41, 127], [43, 127], [96, 127], [97, 127], [98, 127], [99, 24], [71, 127], [119, 127], [120, 127], [121, 127], [122, 127]]) rv.web_gp_set(g, v);
  let bad = false, pk = 0;
  rv.web_transport(1);
  for (let k = 0; k < 800; k++) { const n = rv.rv_run(10), a = new Float32Array(rv.memory.buffer, rv.rv_audio(), n * 2); for (let i = 0; i < n * 2; i++) { if (!(a[i] === a[i])) bad = true; pk = Math.max(pk, Math.abs(a[i])); } }
  rv.web_transport(0);
  check(!bad && pk < 1.6 && rv.web_fault_count() === 0, `the tape delay flat out stays bounded (peak ${pk.toFixed(2)})`);
}
process.exit(fails ? 1 : 0);
