/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: every parameter's name, range, default and display, and the project's defaults. */
#include "rv.h"
#include "rv_sqwaves.h"
#include "rv_shapes.h"
#include "rv_wavetables.h"
#include "rv_organ.h"

/* ------------------------------------------------------------------ names --- */
const char *const rv_kind_names[] = {"SYNTH", "STRIKE", "TAPE", "PERC"};
const char *const rv_engine_names[] = {"VA", "CZ", "FM", "PHYS", "FORMANT", "PARTICLE", "CHIP", "SQ", "WAVE", "LOGIC", "ORGAN", "MONO"};
const char *const rv_route_names[] = {"-", "MOD", "TRIG", "EXCITE", "TAPE", "CLOCK", "RING"};
const char *const rv_msrc_names[] = {"OFF", "LFO1", "LFO2", "DUST", "ENV", "VELO", "WHEEL", "MACRO1", "MACRO2", "MACRO3", "MACRO4", "CLOCK", "KEY"};
const char *const rv_cond_names[] = {"ALWAYS", "FIRST", "NOT 1ST", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "NO FILL"};
const char *const rv_scale_names[] = {"CHROMA", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "PENTA", "MINPENT", "WHOLE", "HIRAJO", "IWATO"};
static const char *const ftype_names[] = {"LADDER", "LOWPASS", "BANDPASS", "HIGHPASS", "LPG", "MS-20", "SH-101", "SQ-80", "Z VOWEL", "Z TALK", "Z PHASER", "Z PEAKS", "Z SWEEP", "Z METAL", "XT LP24", "XT BP24", "XT NOTCH", "XT S&H", "XT SHAPE", "SEM"};
static const char *const ins_names[] = {"OFF", "COMB", "SHIFT", "CRUSH", "SMEAR"};
static const char *const dir_names[] = {"FWD", "REV", "PINGPONG", "SNAKE", "GRAPH"};
static const char *const seqmode_names[] = {"STEPS", "GRAPH"};
static const char *const role_names[] = {"OFF", "NOTES", "RHYTHM", "MELODY"};   /* head 2: ROLE_GATE, ROLE_PITCH */
static const char *const role1_names[] = {"NOTES", "GATE", "PITCH"};
static const char *const fg_names[] = {"OFF", "RISE", "FALL", "ARCH", "DECAY", "SWELL", "LFO 1", "LFO 2", "DUST"};
static const char *const comb_names[] = {"x CLOCK", "+ CLOCK"};
static const char *const target_names[] = {"THIS", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const rate_names[] = {"1/4", "1/3", "1/2", "2/3", "1", "3/2", "2", "3", "4"};
static const char *const clksrc_names[] = {"MASTER", "LFO1", "LFO2", "DUST"};
static const char *const branch_names[] = {"CHANCE", "TURNS", "LFO1", "LFO2", "MOTION"};
static const char *const onoff_names[] = {"OFF", "ON"};
static const char *const clkmode_names[] = {"STEADY", "RATE", "SHAPE", "PHASE", "SUM", "AND", "OR", "XOR"};
static const char *const lock_names[] = {"LOCKED", "FREE"};
static const char *const lfo_names[] = {"SINE", "TRI", "RAMP", "SAW", "SQUARE", "S&H", "DRIFT"};
static const char *const vocbody_names[] = {"DUST", "DROPS", "NOISE", "TAPE", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const fbproc_names[] = {"FILTER", "COMB", "SMEAR", "SHIFT", "FOLD", "CRUSH"};
static const char *const fbret_names[] = {"MASTER", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const eshape_names[] = {"OFF", SHP_NAMES};
static const char *const emode_names[] = {"ONESHOT", "GATED", "LOOP", "CYCLE"};
static const char *const metro_names[] = {"OFF", "QUIET", "LOUD"};
static const char *const countin_names[] = {"OFF", "1 BAR", "2 BARS"};
static const char *const dlymode_names[] = {"TAPE", "GRAINS"};
static const char *const revmode_names[] = {"ROOM", "CLOUDS"};
static const char *const bandshape_names[] = {"UP", "DOWN", "PINGPNG", "RANDOM", "STEP UP", "STEP RND"};
static const char *const pair_names[] = {"SERIES", "PARALLEL", "FEEDBACK"};
static const char *const ptype_names[] = {"LOWPASS", "BANDPASS", "HIGHPASS"};
static const char *const qual_names[] = {"DIRTY", "TAPE", "CLEAN"};   /* = bd_res: 8, 12, 16 bit */
static const char *const tapesrc_names[] = {"MASTER", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "BUSES", "INPUT"};
static const char *const resbars_names[] = {"1 BAR", "2 BARS", "4 BARS", "1/2 BAR"};
static const char *const songmode_names[] = {"PATTERN", "SONG"};
static const char *const root_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const polys[] = {"MONO", "2", "3", "4"};

/* engines */
static const char *const va_ratio[] = {"1/2", "1", "3/2", "2", "3", "4", "5", "7"};
static const char *const cz_lines[] = {"1", "2", "1+1'", "1+2'"};
static const char *const cz_octs[] = {"-1", "0", "+1"};
static const char *const cz_mods[] = {"OFF", "RING", "NOISE"};
static const char *const cz_chorus[] = {"OFF", "I", "II", "I+II"};
static const char *const cz_signs[] = {"+", "-"};
static const char *const cz_vibw[] = {"TRI", "SQUARE", "UP", "DOWN"};
static const char *const fm_algs[] = {"1>2>3>4", "1>2>3+4", "1>2+3+4", "1>2,3>4", "1+2>3>4", "1+2+3>4",
                                      "1+2,3>4", "1>2>3,4", "STACK3", "ALL+4FB", "ALL", "RING"};
static const char *const fm_waves[] = {"SINE", "TRI", "SAW", "SQUARE", "HALF", "ABS", "QUART", "BITS"};
static const char *const chip_waves[] = {"TRI", "SAW", "PULSE", "NOISE", "SAW&PUL", "RING", "PWM", "KICK", "SNARE"};
static const char *const chip_modes[] = {"UP", "DOWN", "PINGPNG", "RANDOM", "ONCE"};
static const char *const chip_suboct[] = {"-3 OCT", "-2 OCT", "-1 OCT", "0 OCT", "+1 OCT"};
static const char *const sq_wave_names[] = {SQ_WAVE_NAMES};
static const char *const wt_names[] = {WT_NAMES};
static const char *const wx_names[] = {"OFF", "SYNC", "RING", "FM", "SYNC+FM"};
static const char *const ww_names[] = {"OFF", "SYNC", "BEND", "PWM", "MIRROR", "REMAP 2", "REMAP 4", "SQUEEZE", "FLIP", "QUANT"};
static const char *const wenv_names[] = {"OFF", SHP_NAMES};
static const char *const lw_names[] = {"SAW", "TRI", "SQUARE", "SINE", "PULSE", "NOISE"};
static const char *const lr_names[] = {"1/4", "1/3", "1/2", "2/3", "3/4", "1", "5/4", "4/3", "3/2", "5/3", "2", "5/2", "3", "4", "5", "6", "7", "8", "11", "16"};
static const char *const lo_names[] = {"A+B", "A*B", "MIN", "MAX", "AND", "OR", "XOR", "NAND", "A>B", "GATE", "DIFF", "QUANT", "MOD", "SIN(A+B)", "A(B)"};
static const char *const org_names[] = {ORG_NAMES};
static const char *const mono_subs[] = {"-1 OCT", "-2 OCT", "-2 PULSE"};
static const char *const mono_trig[] = {"LEGATO", "ALWAYS"};
static const char *const sq_mods[] = {"OFF", "SYNC", "AM", "SYNC+AM"};
static const char *const phys_models[] = {"STRING", "BOWED", "BLOWN", "MEMBRAN"};
static const char *const form_modes[] = {"FILTER", "FOF"};
static const char *const part_tables[] = {"SINE", "GLASS", "REED", "VOICE", "BELL", "BITS", "NOISE", "DROP"};
static const char *const exc_names[] = {"CLICK", "NOISE", "METAL", "FM", "DUST", "RAIN", "BOW", "BLOW", "EXT"};   /* EXT: only from outside (EXC_INPUT) */
static const char *const body_names[] = {"PING", "MODAL", "DISC", "STRINGS"};
static const char *const tape_modes[] = {"MLR", "BEADS", "SLICE"};
static const char *const tape_rows[] = {"ROW 1", "ROW 2", "ROW 3", "ROW 4"};
static const char *const tape_rates[] = {"-2X", "-1X", "-1/2X", "-1/4X", "1/4X", "1/2X", "1X", "2X", "4X"};
static const char *const tape_head2[] = {"OFF", "+1/4", "+1/2", "+3/4", "REV"};
static const char *const tape_quant[] = {"NOW", "STEP", "BEAT", "BAR"};

#define E(names) PF_ENUM, names
#define N(a) (int16_t)(sizeof(a) / sizeof(a[0]) - 1)

/* source slots S0..S15 per synth engine, per strike, per tape */
static const rv_pinfo SRC_VA[16] = {
    {"WAVE", 0, 127, 0, PF_NUM, 0}, {"PW", 0, 127, 64, PF_NUM, 0}, {"SUPER", 1, 7, 1, PF_NUM, 0},
    {"DETUNE", 0, 127, 24, PF_NUM, 0}, {"SUB", 0, 127, 0, PF_NUM, 0}, {"NOISE", 0, 127, 0, PF_NUM, 0},
    {"TZFM", 0, 127, 0, PF_NUM, 0}, {"FMRATIO", 0, N(va_ratio), 1, E(va_ratio)}, {"SYNC", 0, 127, 0, PF_NUM, 0},
    {"DRIFT", 0, 127, 12, PF_NUM, 0}};
/* CZ: the tone's general settings (the per-line waves, key follow and envelopes are rv.cz) */
static const rv_pinfo SRC_CZ[16] = {
    {"LINE", 0, N(cz_lines), 0, E(cz_lines)}, {"OCTAVE", 0, N(cz_octs), 1, E(cz_octs)}, {"MOD", 0, N(cz_mods), 0, E(cz_mods)},
    {"CHORUS", 0, N(cz_chorus), 0, E(cz_chorus)}, {"DETUNE", 0, N(cz_signs), 0, E(cz_signs)}, {"DT OCT", 0, 3, 0, PF_NUM, 0},
    {"DT NOTE", 0, 11, 0, PF_NUM, 0}, {"DT FINE", 0, 60, 4, PF_NUM, 0}, {"VIB WAV", 0, N(cz_vibw), 0, E(cz_vibw)},
    {"DELAY", 0, 99, 0, PF_NUM, 0}, {"RATE", 0, 99, 0, PF_NUM, 0}, {"DEPTH", 0, 99, 0, PF_NUM, 0},
    {"DCW +-", -64, 63, 0, PF_BI, 0}, {"FOAM", 0, 127, 0, PF_NUM, 0}, {"VEL DCW", 0, 9, 0, PF_NUM, 0}};
static const rv_pinfo SRC_FM[16] = {
    {"ALGO", 0, N(fm_algs), 0, E(fm_algs)}, {"FEEDBK", 0, 127, 0, PF_NUM, 0}, {"RATIO1", 0, 31, 3, PF_NUM, 0},
    {"RATIO2", 0, 31, 3, PF_NUM, 0}, {"RATIO3", 0, 31, 5, PF_NUM, 0}, {"RATIO4", 0, 31, 3, PF_NUM, 0},
    {"LEVEL2", 0, 127, 70, PF_NUM, 0}, {"LEVEL3", 0, 127, 40, PF_NUM, 0}, {"LEVEL4", 0, 127, 30, PF_NUM, 0},
    {"WAVE1", 0, N(fm_waves), 0, E(fm_waves)}, {"WAVE2", 0, N(fm_waves), 0, E(fm_waves)}, {"WAVE3", 0, N(fm_waves), 0, E(fm_waves)},
    {"WAVE4", 0, N(fm_waves), 0, E(fm_waves)}, {"MOD DEC", 0, 127, 70, PF_NUM, 0}, {"TZFM", 0, 127, 0, PF_NUM, 0},
    {"VEL MOD", 0, 127, 40, PF_NUM, 0}};
static const rv_pinfo SRC_PHYS[16] = {
    {"MODEL", 0, N(phys_models), 0, E(phys_models)}, {"BRIGHT", 0, 127, 80, PF_NUM, 0}, {"DAMP", 0, 127, 40, PF_NUM, 0},
    {"POSITN", 0, 127, 30, PF_NUM, 0}, {"EXCITE", 0, 127, 90, PF_NUM, 0}, {"INHARM", 0, 127, 0, PF_NUM, 0},
    {"BODY", 0, 127, 40, PF_NUM, 0}, {"DECAY", 0, 127, 90, PF_NUM, 0}};
static const rv_pinfo SRC_FORM[16] = {
    {"VOWEL", 0, 127, 0, PF_NUM, 0}, {"SHIFT", -64, 63, 0, PF_BI, 0}, {"WIDTH", 0, 127, 50, PF_NUM, 0},
    {"MODE", 0, N(form_modes), 0, E(form_modes)}, {"BREATH", 0, 127, 10, PF_NUM, 0}, {"GLOTTAL", 0, 127, 60, PF_NUM, 0},
    {"SPREAD", 0, 127, 0, PF_NUM, 0}, {"VIBRATO", 0, 127, 12, PF_NUM, 0}};
static const rv_pinfo SRC_PART[16] = {
    {"TABLE", 0, N(part_tables), 0, E(part_tables)}, {"DUTY", 0, 127, 40, PF_NUM, 0}, {"EMIT", 1, 3, 1, PF_NUM, 0},
    {"RATIO", 0, 127, 40, PF_NUM, 0}, {"COUPLE", 0, 127, 0, PF_NUM, 0}, {"MASK", 0, 127, 0, PF_NUM, 0},
    {"FORMANT", 0, 127, 64, PF_NUM, 0}, {"SCATTER", 0, 127, 0, PF_NUM, 0}};
static const rv_pinfo SRC_STRIKE[16] = {
    {"EXCITER", 0, N(exc_names), 0, E(exc_names)}, {"BODY", 0, N(body_names), 1, E(body_names)}, {"DECAY", 0, 127, 70, PF_NUM, 0},
    {"TONE", 0, 127, 70, PF_NUM, 0}, {"DAMP", 0, 127, 40, PF_NUM, 0}, {"INHARM", 0, 127, 20, PF_NUM, 0},
    {"GEOMTRY", 0, 127, 40, PF_NUM, 0}, {"MODES", 2, 8, 6, PF_NUM, 0}, {"ORDER", 0, 127, 64, PF_NUM, 0},
    {"RATE", 0, 127, 40, PF_NUM, 0}, {"TRANS", 0, 127, 50, PF_NUM, 0}, {"T DECAY", 0, 127, 20, PF_NUM, 0},
    {"RING F", 0, 127, 60, PF_NUM, 0}, {"RING", 0, 127, 0, PF_NUM, 0}, {"P DROP", 0, 127, 20, PF_NUM, 0},
    {"SEED", 0, 127, 1, PF_NUM, 0}};
static const rv_pinfo SRC_TAPE[16] = {
    {"MODE", 0, N(tape_modes), 0, E(tape_modes)}, {"ROW", 0, 3, 0, E(tape_rows)}, {"RATE", 0, N(tape_rates), 6, E(tape_rates)},
    {"HEAD 2", 0, N(tape_head2), 0, E(tape_head2)}, {"JUMP", 0, N(tape_quant), 1, E(tape_quant)}, {"XFADE", 0, 127, 20, PF_NUM, 0},
    {"TIME", 0, 127, 40, PF_NUM, 0}, {"SIZE", 0, 127, 64, PF_NUM, 0}, {"SHAPE", 0, 127, 64, PF_NUM, 0},
    {"PITCH", -24, 24, 0, PF_SEMI, 0}, {"DENSITY", 0, 127, 40, PF_NUM, 0}, {"FEEDBK", 0, 127, 0, PF_NUM, 0},
    {"MIX", 0, 127, 127, PF_NUM, 0}, {"RANDOM", 0, 127, 0, PF_NUM, 0}, {"SLICES", 4, 32, 16, PF_NUM, 0},
    {"SENS", 0, 127, 64, PF_NUM, 0}};
/* CHIP: a SID-flavoured voice that arpeggiates through offsets (after the Syntakt's SY CHIP),
 * then a decompose stage: sample-rate reduction, bit crushing and fragments that break apart */
static const rv_pinfo SRC_CHIP[16] = {
    {"WAVE", 0, N(chip_waves), CW_PULSE, E(chip_waves)}, {"PW", 0, 127, 64, PF_NUM, 0}, {"RATE", 0, 127, 80, PF_NUM, 0},
    {"MODE", 0, N(chip_modes), 0, E(chip_modes)}, {"OF2", -24, 24, 3, PF_SEMI, 0}, {"OF3", -24, 24, 7, PF_SEMI, 0},
    {"OF4", -24, 24, 12, PF_SEMI, 0}, {"STEPS", 1, 4, 1, PF_NUM, 0}, {"SUB", 0, 127, 0, PF_NUM, 0},
    {"SUB OCT", 0, N(chip_suboct), 2, E(chip_suboct)}, {"SYNC", 0, 1, 0, PF_ONOFF, 0}, {"OVER", 0, 127, 0, PF_NUM, 0},
    {"SRR", 0, 127, 0, PF_NUM, 0}, {"CRUSH", 1, 16, 16, PF_NUM, 0}, {"DECOMP", 0, 127, 0, PF_NUM, 0},
    {"FRAG", 0, 127, 50, PF_NUM, 0}};
/* SQ: after the Ensoniq SQ-80: three oscillators on 8-bit single-cycle waves (Ravel's own, made
 * from harmonic recipes), osc 1 syncing or amplitude-modulating osc 2, ENV 2 for the filter, its
 * decay for pitch and an osc 3 transient, a delayed vibrato, and GRIT: from clean to the DOC
 * chip's aliasing, held samples and coarse DAC. The track filter defaults to SQ-80 (CEM3379). */
static const rv_pinfo SRC_SQ[16] = {
    {"WAVE 1", 0, SQ_NWAVES - 1, 0, E(sq_wave_names)}, {"WAVE 2", 0, SQ_NWAVES - 1, 0, E(sq_wave_names)},
    {"WAVE 3", 0, SQ_NWAVES - 1, 5, E(sq_wave_names)}, {"SEMI 2", -24, 24, 0, PF_SEMI, 0}, {"SEMI 3", -24, 24, 12, PF_SEMI, 0},
    {"DETUNE", 0, 127, 16, PF_NUM, 0}, {"LEVEL 2", 0, 127, 90, PF_NUM, 0}, {"LEVEL 3", 0, 127, 0, PF_NUM, 0},
    {"MOD", 0, N(sq_mods), 0, E(sq_mods)}, {"E2 DEC", 0, 127, 60, PF_NUM, 0}, {"E2 SUS", 0, 127, 40, PF_NUM, 0},
    {"P ENV", -64, 63, 0, PF_BI, 0}, {"TRANS", 0, 127, 0, PF_NUM, 0}, {"VIBRATO", 0, 127, 0, PF_NUM, 0},
    {"VIB RATE", 0, 127, 64, PF_NUM, 0}, {"GRIT", 0, 127, 64, PF_NUM, 0}};
/* WAVE: after the PPG Wave and the Waldorf Microwave: two oscillators reading wavetables (Ravel's
 * own: 8 key waves each, 64 positions between them), cross-modulation, Serum-style phase warps, a
 * looping wave envelope that sweeps the positions, and ASIC: smooth, then stepping, then sizzle */
static const rv_pinfo SRC_WAVE[16] = {
    {"TABLE 1", 0, WT_TABLES - 1, 0, E(wt_names)}, {"POS 1", 0, 63, 0, PF_NUM, 0},
    {"TABLE 2", 0, WT_TABLES - 1, 1, E(wt_names)}, {"POS 2", 0, 63, 0, PF_NUM, 0},
    {"SEMI 2", -24, 24, 0, PF_SEMI, 0}, {"DETUNE", 0, 127, 10, PF_NUM, 0}, {"MIX", 0, 127, 0, PF_NUM, 0},
    {"SAT", 0, 127, 20, PF_NUM, 0}, {"XMOD", 0, N(wx_names), 0, E(wx_names)}, {"XM AMT", 0, 127, 40, PF_NUM, 0},
    {"WARP", 0, N(ww_names), 0, E(ww_names)}, {"WARP AMT", 0, 127, 40, PF_NUM, 0},
    {"WAVE ENV", 0, SHP_N, 0, E(wenv_names)}, {"WE AMT", -64, 63, 40, PF_BI, 0}, {"WE TIME", 0, 127, 95, PF_TIME, 0},
    {"ASIC", 0, 127, 0, PF_NUM, 0}};
/* LOGIC: oscillators built from function blocks, after Kurzweil's VAST FUNs and E-mu's patch-cord
 * functions: three sources (A on the note, B and C at ratios) meet in OP 1 (A with B), then OP 2
 * (that with C); then a pitch-synchronous grain stage */
static const rv_pinfo SRC_LOGIC[16] = {
    {"A WAVE", 0, N(lw_names), LW_SAW, E(lw_names)}, {"B RATIO", 0, N(lr_names), 10, E(lr_names)},
    {"B WAVE", 0, N(lw_names), LW_TRI, E(lw_names)}, {"C RATIO", 0, N(lr_names), 2, E(lr_names)},
    {"C WAVE", 0, N(lw_names), LW_TRI, E(lw_names)}, {"OP 1", 0, N(lo_names), LO_XOR, E(lo_names)},
    {"OP 2", 0, N(lo_names), LO_MULT, E(lo_names)}, {"OP 2 AMT", 0, 127, 0, PF_NUM, 0},
    {"BITS", 1, 12, 6, PF_NUM, 0}, {"PW", 0, 127, 64, PF_NUM, 0}, {"B DRIFT", 0, 127, 0, PF_NUM, 0},
    {"DRY A", 0, 127, 0, PF_NUM, 0}, {"SMOOTH", 0, 127, 30, PF_NUM, 0}, {"GRAIN", 0, 127, 0, PF_NUM, 0},
    {"G SPRAY", 0, 127, 20, PF_NUM, 0}, {"G PITCH", -24, 24, 0, PF_SEMI, 0}};
/* ORGAN: pipe ranks by footage over one stop family, each pipe speaking in its own time */
static const rv_pinfo SRC_ORGAN[16] = {
    {"STOP", 0, ORG_STOPS - 1, 1, E(org_names)}, {"16'", 0, 127, 0, PF_NUM, 0}, {"8'", 0, 127, 110, PF_NUM, 0},
    {"4'", 0, 127, 70, PF_NUM, 0}, {"2 2/3'", 0, 127, 0, PF_NUM, 0}, {"2'", 0, 127, 40, PF_NUM, 0},
    {"1 3/5'", 0, 127, 0, PF_NUM, 0}, {"1'", 0, 127, 0, PF_NUM, 0}, {"CHIFF", 0, 127, 40, PF_NUM, 0},
    {"SPEECH", 0, 127, 50, PF_NUM, 0}, {"SPREAD", 0, 127, 40, PF_NUM, 0}, {"AIR", 0, 127, 25, PF_NUM, 0},
    {"WIND", 0, 127, 30, PF_NUM, 0}, {"TREMLNT", 0, 127, 0, PF_NUM, 0}, {"TREM RT", 0, 127, 60, PF_NUM, 0},
    {"CELESTE", 0, 127, 0, PF_NUM, 0}};
/* MONO: one oscillator in the SH-101's way, played like a wind instrument */
static const rv_pinfo SRC_MONO[16] = {
    {"SAW", 0, 127, 127, PF_NUM, 0}, {"PULSE", 0, 127, 0, PF_NUM, 0}, {"SUB", 0, 127, 8, PF_NUM, 0},
    {"SUB OCT", 0, N(mono_subs), 0, E(mono_subs)}, {"NOISE", 0, 127, 0, PF_NUM, 0}, {"PW", 0, 127, 64, PF_NUM, 0},
    {"PWM", 0, 127, 0, PF_NUM, 0}, {"SCOOP", 0, 127, 20, PF_NUM, 0}, {"SCOOP T", 0, 127, 40, PF_NUM, 0},
    {"VIBRATO", 0, 127, 10, PF_NUM, 0}, {"VIB RATE", 0, 127, 70, PF_NUM, 0}, {"VIB DLY", 0, 127, 50, PF_NUM, 0},
    {"VEL>CUT", 0, 127, 40, PF_NUM, 0}, {"DRIFT", 0, 127, 20, PF_NUM, 0}, {"RETRIG", 0, 1, 0, E(mono_trig)},
    {"DRIVE", 0, 127, 30, PF_NUM, 0}};
static const rv_pinfo *const SRC_SYNTH[ENG_N] = {SRC_VA, SRC_CZ, SRC_FM, SRC_PHYS, SRC_FORM, SRC_PART, SRC_CHIP, SRC_SQ, SRC_WAVE, SRC_LOGIC, SRC_ORGAN, SRC_MONO};

/* PERC (rv_perc.c): the machine list and each machine's eight parameters (0 = unused), with their defaults */
static const rv_pinfo perc_pi[PM_N][8] = {
    [PM_TRX_BD] = {{"PTCH", 0, 127, 40, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"RAMP", 0, 127, 64, PF_NUM, 0}, {"RDEC", 0, 127, 40, PF_NUM, 0}, {"STRT", 0, 127, 50, PF_NUM, 0}, {"NOIS", 0, 127, 10, PF_NUM, 0}, {"HARM", 0, 127, 0, PF_NUM, 0}, {"CLIP", 0, 127, 20, PF_NUM, 0}},
    [PM_TRX_B2] = {{"PTCH", 0, 127, 40, PF_NUM, 0}, {"DEC", 0, 127, 60, PF_NUM, 0}, {"RAMP", 0, 127, 50, PF_NUM, 0}, {"HOLD", 0, 127, 30, PF_NUM, 0}, {"TICK", 0, 127, 60, PF_NUM, 0}, {"NOIS", 0, 127, 10, PF_NUM, 0}, {"DIRT", 0, 127, 0, PF_NUM, 0}, {"DIST", 0, 127, 20, PF_NUM, 0}},
    [PM_TRX_SD] = {{"PTCH", 0, 127, 50, PF_NUM, 0}, {"DEC", 0, 127, 50, PF_NUM, 0}, {"BUMP", 0, 127, 40, PF_NUM, 0}, {"BENV", 0, 127, 40, PF_NUM, 0}, {"SNAP", 0, 127, 80, PF_NUM, 0}, {"TONE", 0, 127, 64, PF_NUM, 0}, {"TUNE", 0, 127, 40, PF_NUM, 0}, {"CLIP", 0, 127, 10, PF_NUM, 0}},
    [PM_TRX_XT] = {{"PTCH", 0, 127, 50, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"RAMP", 0, 127, 40, PF_NUM, 0}, {"RDEC", 0, 127, 50, PF_NUM, 0}, {"DAMP", 0, 127, 0, PF_NUM, 0}, {"DIST", 0, 127, 0, PF_NUM, 0}, {"DTYP", 0, 127, 40, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_CP] = {{"CLPY", 0, 127, 64, PF_NUM, 0}, {"TONE", 0, 127, 64, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {"RICH", 0, 127, 40, PF_NUM, 0}, {"RATE", 0, 127, 50, PF_NUM, 0}, {"ROOM", 0, 127, 40, PF_NUM, 0}, {"RSIZ", 0, 127, 60, PF_NUM, 0}, {"RTUN", 0, 127, 64, PF_NUM, 0}},
    [PM_TRX_RS] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 40, PF_NUM, 0}, {"DIST", 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_CB] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 60, PF_NUM, 0}, {"ENH", 0, 127, 64, PF_NUM, 0}, {"DAMP", 0, 127, 20, PF_NUM, 0}, {"TONE", 0, 127, 64, PF_NUM, 0}, {"BUMP", 0, 127, 20, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_CH] = {{"GAP", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 30, PF_NUM, 0}, {"HPF", 0, 127, 70, PF_NUM, 0}, {"LPF", 0, 127, 110, PF_NUM, 0}, {"MTAL", 0, 127, 64, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_OH] = {{"GAP", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 80, PF_NUM, 0}, {"HPF", 0, 127, 70, PF_NUM, 0}, {"LPF", 0, 127, 110, PF_NUM, 0}, {"MTAL", 0, 127, 64, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_CY] = {{"RICH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 90, PF_NUM, 0}, {"TOP", 0, 127, 64, PF_NUM, 0}, {"TTUN", 0, 127, 64, PF_NUM, 0}, {"SIZE", 0, 127, 64, PF_NUM, 0}, {"PEAK", 0, 127, 30, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_MA] = {{"ATT", 0, 127, 10, PF_NUM, 0}, {"SUS", 0, 127, 30, PF_NUM, 0}, {"REV", 0, 127, 0, PF_NUM, 0}, {"DAMP", 0, 127, 0, PF_NUM, 0}, {"RATL", 0, 127, 40, PF_NUM, 0}, {"RTYP", 0, 127, 64, PF_NUM, 0}, {"TONE", 0, 127, 80, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}},
    [PM_TRX_CL] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 40, PF_NUM, 0}, {"DUAL", 0, 127, 0, PF_NUM, 0}, {"ENH", 0, 127, 40, PF_NUM, 0}, {"TUNE", 0, 127, 64, PF_NUM, 0}, {"CLIC", 0, 127, 40, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_TRX_XC] = {{"PTCH", 0, 127, 60, PF_NUM, 0}, {"DEC", 0, 127, 50, PF_NUM, 0}, {"RAMP", 0, 127, 30, PF_NUM, 0}, {"RDEC", 0, 127, 40, PF_NUM, 0}, {"DAMP", 0, 127, 0, PF_NUM, 0}, {"DIST", 0, 127, 0, PF_NUM, 0}, {"DTYP", 0, 127, 40, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_EFM_BD] = {{"PTCH", 0, 127, 40, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"RAMP", 0, 127, 64, PF_NUM, 0}, {"RDEC", 0, 127, 40, PF_NUM, 0}, {"MOD", 0, 127, 40, PF_NUM, 0}, {"MFRQ", 0, 127, 40, PF_NUM, 0}, {"MDEC", 0, 127, 30, PF_NUM, 0}, {"MFB", 0, 127, 0, PF_NUM, 0}},
    [PM_EFM_SD] = {{"PTCH", 0, 127, 60, PF_NUM, 0}, {"DEC", 0, 127, 50, PF_NUM, 0}, {"NOIS", 0, 127, 80, PF_NUM, 0}, {"NDEC", 0, 127, 50, PF_NUM, 0}, {"MOD", 0, 127, 50, PF_NUM, 0}, {"MFRQ", 0, 127, 60, PF_NUM, 0}, {"MDEC", 0, 127, 40, PF_NUM, 0}, {"HPF", 0, 127, 20, PF_NUM, 0}},
    [PM_EFM_XT] = {{"PTCH", 0, 127, 50, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"RAMP", 0, 127, 40, PF_NUM, 0}, {"RDEC", 0, 127, 50, PF_NUM, 0}, {"MOD", 0, 127, 40, PF_NUM, 0}, {"MFRQ", 0, 127, 50, PF_NUM, 0}, {"MDEC", 0, 127, 40, PF_NUM, 0}, {"CLIC", 0, 127, 30, PF_NUM, 0}},
    [PM_EFM_CP] = {{"PTCH", 0, 127, 70, PF_NUM, 0}, {"DEC", 0, 127, 50, PF_NUM, 0}, {"CLPS", 0, 127, 64, PF_NUM, 0}, {"CDEC", 0, 127, 40, PF_NUM, 0}, {"MOD", 0, 127, 100, PF_NUM, 0}, {"MFRQ", 0, 127, 90, PF_NUM, 0}, {"MDEC", 0, 127, 60, PF_NUM, 0}, {"HPF", 0, 127, 40, PF_NUM, 0}},
    [PM_EFM_RS] = {{"PTCH", 0, 127, 80, PF_NUM, 0}, {"DEC", 0, 127, 30, PF_NUM, 0}, {"MOD", 0, 127, 40, PF_NUM, 0}, {"HPF", 0, 127, 20, PF_NUM, 0}, {"SNAR", 0, 127, 40, PF_NUM, 0}, {"SPTC", 0, 127, 70, PF_NUM, 0}, {"SDEC", 0, 127, 30, PF_NUM, 0}, {"SMOD", 0, 127, 60, PF_NUM, 0}},
    [PM_EFM_CB] = {{"PTCH", 0, 127, 70, PF_NUM, 0}, {"DEC", 0, 127, 60, PF_NUM, 0}, {"SNAP", 0, 127, 40, PF_NUM, 0}, {"FB", 0, 127, 20, PF_NUM, 0}, {"MOD", 0, 127, 50, PF_NUM, 0}, {"MFRQ", 0, 127, 70, PF_NUM, 0}, {"MDEC", 0, 127, 60, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_EFM_HH] = {{"PTCH", 0, 127, 90, PF_NUM, 0}, {"DEC", 0, 127, 30, PF_NUM, 0}, {"TREM", 0, 127, 0, PF_NUM, 0}, {"TFRQ", 0, 127, 64, PF_NUM, 0}, {"MOD", 0, 127, 110, PF_NUM, 0}, {"MFRQ", 0, 127, 100, PF_NUM, 0}, {"MDEC", 0, 127, 90, PF_NUM, 0}, {"FB", 0, 127, 70, PF_NUM, 0}},
    [PM_EFM_CY] = {{"PTCH", 0, 127, 80, PF_NUM, 0}, {"DEC", 0, 127, 90, PF_NUM, 0}, {"FB", 0, 127, 80, PF_NUM, 0}, {"HPF", 0, 127, 40, PF_NUM, 0}, {"MOD", 0, 127, 110, PF_NUM, 0}, {"MFRQ", 0, 127, 95, PF_NUM, 0}, {"MDEC", 0, 127, 100, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_BD] = {{"PTCH", 0, 127, 40, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {"HAMR", 0, 127, 40, PF_NUM, 0}, {"TENS", 0, 127, 40, PF_NUM, 0}, {"DAMP", 0, 127, 30, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_SD] = {{"PTCH", 0, 127, 60, PF_NUM, 0}, {"DEC", 0, 127, 50, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {"RING", 0, 127, 20, PF_NUM, 0}, {"TENS", 0, 127, 30, PF_NUM, 0}, {"RVOL", 0, 127, 80, PF_NUM, 0}, {"RDEC", 0, 127, 50, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_MT] = {{"PTCH", 0, 127, 50, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {"HAMR", 0, 127, 40, PF_NUM, 0}, {"TUNE", 0, 127, 64, PF_NUM, 0}, {"DAMP", 0, 127, 20, PF_NUM, 0}, {"SIZE", 0, 127, 64, PF_NUM, 0}, {"POS", 0, 127, 30, PF_NUM, 0}},
    [PM_PI_RS] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 40, PF_NUM, 0}, {"HARD", 0, 127, 80, PF_NUM, 0}, {"RING", 0, 127, 40, PF_NUM, 0}, {"RVOL", 0, 127, 40, PF_NUM, 0}, {"RDEC", 0, 127, 50, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_ML] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 80, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {"TENS", 0, 127, 20, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_MA] = {{"DEC", 0, 127, 50, PF_NUM, 0}, {"GRNS", 0, 127, 64, PF_NUM, 0}, {"GLEN", 0, 127, 40, PF_NUM, 0}, {"SIZE", 0, 127, 64, PF_NUM, 0}, {"HARD", 0, 127, 64, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_PI_HH] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 40, PF_NUM, 0}, {"CLSN", 0, 127, 30, PF_NUM, 0}, {"RING", 0, 127, 64, PF_NUM, 0}, {"AG", 0, 127, 64, PF_NUM, 0}, {"AU", 0, 127, 30, PF_NUM, 0}, {"BR", 0, 127, 30, PF_NUM, 0}, {"CLOS", 0, 127, 20, PF_NUM, 0}},
    [PM_PI_RC] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 100, PF_NUM, 0}, {"HARD", 0, 127, 40, PF_NUM, 0}, {"RING", 0, 127, 80, PF_NUM, 0}, {"AG", 0, 127, 64, PF_NUM, 0}, {"AU", 0, 127, 30, PF_NUM, 0}, {"BR", 0, 127, 30, PF_NUM, 0}, {"GRAB", 0, 127, 127, PF_NUM, 0}},
    [PM_PI_CC] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 100, PF_NUM, 0}, {"HARD", 0, 127, 80, PF_NUM, 0}, {"RING", 0, 127, 60, PF_NUM, 0}, {"AG", 0, 127, 70, PF_NUM, 0}, {"AU", 0, 127, 20, PF_NUM, 0}, {"BR", 0, 127, 40, PF_NUM, 0}, {"GRAB", 0, 127, 127, PF_NUM, 0}},
    [PM_GND_SN] = {{"PTCH", 0, 127, 64, PF_NUM, 0}, {"DEC", 0, 127, 64, PF_NUM, 0}, {"RAMP", 0, 127, 0, PF_NUM, 0}, {"RDEC", 0, 127, 40, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_GND_NS] = {{"DEC", 0, 127, 50, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
    [PM_GND_IM] = {{"UP", 0, 127, 20, PF_NUM, 0}, {"UVAL", 0, 127, 127, PF_NUM, 0}, {"DOWN", 0, 127, 20, PF_NUM, 0}, {"DVAL", 0, 127, 127, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}, {0, 0, 127, 0, PF_NUM, 0}},
};

static const char *const perc_mnames[PM_N] = {
    "TRX-BD", "TRX-B2", "TRX-SD", "TRX-XT", "TRX-CP", "TRX-RS", "TRX-CB", "TRX-CH", "TRX-OH", "TRX-CY", "TRX-MA", "TRX-CL", "TRX-XC",
    "EFM-BD", "EFM-SD", "EFM-XT", "EFM-CP", "EFM-RS", "EFM-CB", "EFM-HH", "EFM-CY",
    "PI-BD", "PI-SD", "PI-MT", "PI-RS", "PI-ML", "PI-MA", "PI-HH", "PI-RC", "PI-CC", "GND-SN", "GND-NS", "GND-IM"};
/* SOURCE 1: the machine; then its eight; then the Machinedrum's track effects */
static const rv_pinfo SRC_PERC_FX[7] = {
    {"AMD", 0, 127, 0, PF_NUM, 0}, {"AMF", 0, 127, 64, PF_NUM, 0}, {"EQF", 0, 127, 64, PF_NUM, 0},
    {"EQG", -64, 63, 0, PF_BI, 0}, {"FLTF", 0, 127, 0, PF_NUM, 0}, {"FLTW", 0, 127, 127, PF_NUM, 0},
    {"FLTQ", 0, 127, 0, PF_NUM, 0}};
static const rv_pinfo *perc_pinfo(int mach, int s)
{
    static const rv_pinfo machine = {"MACHINE", 0, PM_N - 1, PM_TRX_BD, PF_ENUM, perc_mnames};
    if (s == 0)
        return &machine;
    if (s <= 8)
        return &perc_pi[mach < 0 ? 0 : mach >= PM_N ? PM_N - 1 : mach][s - 1];
    return &SRC_PERC_FX[s - 9];
}

static const rv_pinfo TRACK[TP_N] = {
    [TP_KIND] = {"KIND", 0, KIND_N - 1, 0, E(rv_kind_names)},
    [TP_ENGINE] = {"ENGINE", 0, ENG_N - 1, 0, E(rv_engine_names)},
    [TP_PITCH] = {"PITCH", -24, 24, 0, PF_SEMI, 0},
    [TP_FINE] = {"FINE", -50, 50, 0, PF_CENT, 0},
    [TP_GLIDE] = {"GLIDE", 0, 127, 0, PF_NUM, 0},
    [TP_POLY] = {"VOICES", 0, 3, 3, E(polys)},
    [TP_VELO] = {"VELO", 0, 127, 64, PF_NUM, 0},
    [TP_ATK] = {"ATTACK", 0, 127, 4, PF_TIME, 0},
    [TP_ACURVE] = {"CURVE", -64, 63, 0, PF_BI, 0},
    [TP_OVER] = {"OVERSHT", 0, 127, 0, PF_NUM, 0},
    [TP_DEC] = {"DECAY", 0, 127, 70, PF_TIME, 0},
    [TP_SUS] = {"SUSTAIN", 0, 127, 90, PF_NUM, 0},
    [TP_LOOP] = {"LOOP", 0, 127, 0, PF_NUM, 0},
    [TP_REL] = {"RELEASE", 0, 127, 60, PF_TIME, 0},
    [TP_TAIL] = {"TAIL", 0, 127, 0, PF_NUM, 0},
    [TP_GIVE] = {"GIVE", 0, 127, 0, PF_NUM, 0},
    [TP_EFILT] = {"ENV>CUT", -64, 63, 20, PF_BI, 0},
    [TP_EDST1] = {"DEST 1", 0, TP_N - 1, TP_S0, PF_DEST, 0},
    [TP_EAMT1] = {"AMOUNT 1", -64, 63, 0, PF_BI, 0},
    [TP_EDST2] = {"DEST 2", 0, TP_N - 1, TP_DRIVE, PF_DEST, 0},
    [TP_EAMT2] = {"AMOUNT 2", -64, 63, 0, PF_BI, 0},
    [TP_FTYPE] = {"FILTER", 0, FT_N - 1, FT_LADDER, E(ftype_names)},
    [TP_CUT] = {"CUTOFF", 0, 127, 90, PF_HZ, 0},
    [TP_RES] = {"RESO", 0, 127, 20, PF_NUM, 0},
    [TP_KTRK] = {"KEYTRK", 0, 127, 64, PF_NUM, 0},
    [TP_DRIVE] = {"DRIVE", 0, 127, 10, PF_NUM, 0},
    [TP_FOLD] = {"FOLD", 0, 127, 0, PF_NUM, 0},
    [TP_BITS] = {"BITS", 0, 127, 0, PF_NUM, 0},
    [TP_DEREZ] = {"DEREZ", 0, 127, 0, PF_NUM, 0},
    [TP_MASK] = {"BITMASK", 0, 127, 0, PF_NUM, 0},
    [TP_VOC] = {"VOCODER", 0, 127, 0, PF_NUM, 0},
    [TP_INS] = {"INSERT", 0, INS_N - 1, INS_OFF, E(ins_names)},
    [TP_INA] = {"TUNE", 0, 127, 64, PF_NUM, 0},
    [TP_INB] = {"AMOUNT", 0, 127, 64, PF_NUM, 0},
    [TP_INMIX] = {"MIX", 0, 127, 64, PF_NUM, 0},
    [TP_LEVEL] = {"LEVEL", 0, 127, 100, PF_NUM, 0},
    [TP_PAN] = {"PAN", -64, 63, 0, PF_BI, 0},
    [TP_FBS] = {"FEEDBK", 0, 127, 0, PF_NUM, 0},
    [TP_PAIRS] = {"PAIR", 0, 127, 0, PF_NUM, 0},
    [TP_DLYS] = {"DELAY", 0, 127, 0, PF_NUM, 0},
    [TP_REVS] = {"SPACE", 0, 127, 0, PF_NUM, 0},
    [TP_LEN] = {"LENGTH", 1, 64, 16, PF_NUM, 0},
    [TP_RATE] = {"RATE", 0, N(rate_names), 4, E(rate_names)},
    [TP_DIR] = {"DIRECTN", 0, DIR_SNAKE, DIR_FWD, E(dir_names)},
    [TP_INTERVAL] = {"INTERVAL", 1, 12, 1, PF_NUM, 0},   /* only notes a multiple of N semitones from the root */
    [TP_CLKSRC] = {"CLOCK", 0, N(clksrc_names), 0, E(clksrc_names)},
    [TP_BRANCH] = {"BRANCH", 0, N(branch_names), 1, E(branch_names)},
    [TP_H2ROLE] = {"HEAD 2", 0, N(role_names), ROLE_OFF, E(role_names)},
    [TP_H2RATE] = {"H2 RATE", 0, N(rate_names), 2, E(rate_names)},
    [TP_H2START] = {"H2 START", 1, 64, 9, PF_NUM, 0},
    [TP_SEQMODE] = {"MODE", 0, N(seqmode_names), SEQ_STEPS, E(seqmode_names)},
    [TP_H1ROLE] = {"ROLE", 0, N(role1_names), 0, E(role1_names)},
    [TP_H1SHAPE] = {"SHAPE", 0, N(fg_names), FG_OFF, E(fg_names)},
    [TP_H1DEPTH] = {"DEPTH", -64, 63, 32, PF_BI, 0},
    [TP_H1CYCLE] = {"CYCLE", 2, 64, 16, PF_NUM, 0},
    [TP_H1COMB] = {"COMBINE", 0, N(comb_names), COMB_SCALE, E(comb_names)},
    [TP_H1TRANS] = {"TRANSP", -24, 24, 0, PF_SEMI, 0},
    [TP_H2TARGET] = {"PLAYS", 0, N(target_names), 0, E(target_names)},
    [TP_H2SHAPE] = {"SHAPE", 0, N(fg_names), FG_OFF, E(fg_names)},
    [TP_H2DEPTH] = {"DEPTH", -64, 63, 32, PF_BI, 0},
    [TP_H2CYCLE] = {"CYCLE", 2, 64, 16, PF_NUM, 0},
    [TP_H2COMB] = {"COMBINE", 0, N(comb_names), COMB_SCALE, E(comb_names)},
    [TP_H2TRANS] = {"TRANSP", -24, 24, 0, PF_SEMI, 0},
    [TP_BANDS] = {"BANDS", 0, 127, 0, PF_NUM, 0},
    [TP_ESHAPE] = {"SHAPE", 0, SHP_N, 0, E(eshape_names)},
    [TP_EMORPH] = {"MORPH", 0, 127, 0, PF_NUM, 0},
    [TP_ETIME] = {"TIME", 0, 127, 90, PF_TIME, 0},
    [TP_EMODE] = {"PLAY", 0, N(emode_names), SHM_GATED, E(emode_names)},
    [TP_EWARP] = {"WARP", -64, 63, 0, PF_BI, 0},
    [TP_FMODE] = {"LP-HP", 0, 127, 0, PF_NUM, 0},   /* 0 low pass, 64 notch, 127 high pass */
    [TP_FBP] = {"BP", 0, 1, 0, PF_ONOFF, 0},
};

static const rv_pinfo GLOBAL[GP_N] = {
    [GP_TEMPO] = {"TEMPO", 30, 240, 112, PF_BPM, 0},
    [GP_SWING] = {"SWING", 50, 100, 50, PF_PCT, 0},   /* where the offbeat sits in each pair of steps: 50% even */
    [GP_QUANT] = {"QUANTZE", 0, 100, 0, PF_PCT, 0},
    [GP_CLKMODE] = {"B DOES", 0, N(clkmode_names), 0, E(clkmode_names)},
    [GP_CLKSHAPE] = {"A SHAPE", 0, 127, 64, PF_NUM, 0},
    [GP_CLKBRATE] = {"B RATE", 0, 127, 30, PF_NUM, 0},
    [GP_CLKBSHAPE] = {"B SHAPE", 0, 127, 127, PF_NUM, 0},
    [GP_CLKDEPTH] = {"DEPTH", 0, 127, 0, PF_NUM, 0},
    [GP_CLKTHR] = {"THRESH", 0, 127, 0, PF_NUM, 0},
    [GP_CLKLOCK] = {"TEMPO IS", 0, 1, 0, E(lock_names)},
    [GP_ROOT] = {"ROOT", 0, 11, 0, E(root_names)},
    [GP_SCALE] = {"SCALE", 0, N(rv_scale_names), 0, E(rv_scale_names)},
    [GP_TRANSPOSE] = {"TRANSPS", -24, 24, 0, PF_SEMI, 0},
    [GP_LFO1RATE] = {"RATE", 0, 127, 40, PF_NUM, 0},
    [GP_LFO1SHAPE] = {"SHAPE", 0, N(lfo_names), 0, E(lfo_names)},
    [GP_LFO2RATE] = {"RATE", 0, 127, 20, PF_NUM, 0},
    [GP_LFO2SHAPE] = {"SHAPE", 0, N(lfo_names), 1, E(lfo_names)},
    [GP_DUSTRATE] = {"RATE", 0, 127, 40, PF_NUM, 0},
    [GP_DUSTDECAY] = {"DECAY", 0, 127, 40, PF_NUM, 0},
    [GP_VOCBODY] = {"BODY", 0, N(vocbody_names), 1, E(vocbody_names)},
    [GP_VOCTILT] = {"TILT", -64, 63, 0, PF_BI, 0},
    [GP_VOCREL] = {"RELEASE", 0, 127, 40, PF_NUM, 0},
    [GP_VOCLEVEL] = {"LEVEL", 0, 127, 100, PF_NUM, 0},
    [GP_FBTIME] = {"TIME", 0, 127, 50, PF_NUM, 0},
    [GP_FBREGEN] = {"REGEN", 0, 127, 70, PF_NUM, 0},
    [GP_FBPROC] = {"PROCESS", 0, N(fbproc_names), 0, E(fbproc_names)},
    [GP_FBAMT] = {"AMOUNT", 0, 127, 50, PF_NUM, 0},
    [GP_FBTUNE] = {"TUNE", 0, 127, 64, PF_NUM, 0},
    [GP_FBEDGE] = {"EDGE", 0, 1, 1, E(onoff_names)},
    [GP_FBRET] = {"RETURN", 0, N(fbret_names), 0, E(fbret_names)},
    [GP_FBLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_FBCUT] = {"CUTOFF", 0, 127, 80, PF_HZ, 0},
    [GP_PAIRMODE] = {"ROUTING", 0, N(pair_names), 0, E(pair_names)},
    [GP_PAIRF1] = {"FREQ 1", 0, 127, 60, PF_HZ, 0},
    [GP_PAIRF2] = {"FREQ 2", 0, 127, 85, PF_HZ, 0},
    [GP_PAIRRES] = {"RESO", 0, 127, 80, PF_NUM, 0},
    [GP_PAIRLFO1] = {"LFO 1", 0, 127, 20, PF_NUM, 0},
    [GP_PAIRLFO2] = {"LFO 2", 0, 127, 33, PF_NUM, 0},
    [GP_PAIRDEPTH] = {"DEPTH", 0, 127, 40, PF_NUM, 0},
    [GP_PAIRLEVEL] = {"LEVEL", 0, 127, 100, PF_NUM, 0},
    [GP_DLYTIME] = {"TIME", 0, 127, 60, PF_NUM, 0},
    [GP_DLYFB] = {"FEEDBK", 0, 127, 50, PF_NUM, 0},
    [GP_DLYTONE] = {"TONE", 0, 127, 70, PF_NUM, 0},
    [GP_DLYLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_REVSIZE] = {"SIZE", 0, 127, 70, PF_NUM, 0},
    [GP_REVDAMP] = {"DAMP", 0, 127, 50, PF_NUM, 0},
    [GP_REVLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_MDRIVE] = {"DRIVE", 0, 127, 20, PF_NUM, 0},
    [GP_CUTSIZE] = {"CUT SIZE", 0, 127, 50, PF_NUM, 0},
    [GP_CUTREP] = {"REPEATS", 1, 16, 4, PF_NUM, 0},
    [GP_CUTMIX] = {"CUT MIX", 0, 127, 127, PF_NUM, 0},
    [GP_TAPEQUAL] = {"QUALITY", 0, 2, 2, E(qual_names)},
#ifdef RV_NO_AUDIO_IN
    [GP_TAPESRC] = {"SOURCE", 0, N(tapesrc_names) - 1, 0, E(tapesrc_names)},   /* no INPUT on the FM-1 */
#else
    [GP_TAPESRC] = {"SOURCE", 0, N(tapesrc_names), 0, E(tapesrc_names)},
#endif
    [GP_TAPEMON] = {"INPUT", 0, 127, 0, PF_NUM, 0},
    [GP_RESBARS] = {"RESAMPL", 0, N(resbars_names), 0, E(resbars_names)},
    [GP_ERODE] = {"ERODE", 0, 127, 0, PF_NUM, 0},
    [GP_ROW1S] = {"START", 0, 127, 0, PF_NUM, 0}, [GP_ROW1E] = {"END", 1, 128, 128, PF_NUM, 0},
    [GP_ROW2S] = {"START", 0, 127, 0, PF_NUM, 0}, [GP_ROW2E] = {"END", 1, 128, 64, PF_NUM, 0},
    [GP_ROW3S] = {"START", 0, 127, 64, PF_NUM, 0}, [GP_ROW3E] = {"END", 1, 128, 128, PF_NUM, 0},
    [GP_ROW4S] = {"START", 0, 127, 0, PF_NUM, 0}, [GP_ROW4E] = {"END", 1, 128, 16, PF_NUM, 0},
    [GP_MACRO1] = {"MACRO 1", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO2] = {"MACRO 2", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO3] = {"MACRO 3", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO4] = {"MACRO 4", 0, 127, 0, PF_NUM, 0},
    [GP_SONGMODE] = {"PLAYS", 0, 1, 0, E(songmode_names)},
    [GP_INPUTLEVEL] = {"IN GAIN", 0, 127, 90, PF_NUM, 0},
    [GP_DLYSPREAD] = {"SPREAD", 0, 127, 127, PF_NUM, 0},
    [GP_DLYWOB] = {"WOBBLE", 0, 127, 0, PF_NUM, 0},
    [GP_DLYFREEZE] = {"FREEZE", 0, 1, 0, E(onoff_names)},
    [GP_REVMOD] = {"MOTION", 0, 127, 20, PF_NUM, 0},
    [GP_REVDIFF] = {"DIFFUSE", 0, 127, 85, PF_NUM, 0},
    [GP_REVLOW] = {"LOW CUT", 0, 127, 0, PF_NUM, 0},
    [GP_VOCATK] = {"ATTACK", 0, 127, 18, PF_NUM, 0},
    [GP_VOCSHIFT] = {"SHIFT", -24, 24, 0, PF_SEMI, 0},
    [GP_VOCQ] = {"NARROW", 0, 127, 64, PF_NUM, 0},
    [GP_PAIRT1] = {"TYPE 1", 0, N(ptype_names), 0, E(ptype_names)},
    [GP_PAIRT2] = {"TYPE 2", 0, N(ptype_names), 1, E(ptype_names)},
    [GP_BAND1] = {"150", 0, 127, 100, PF_NUM, 0}, [GP_BAND2] = {"240", 0, 127, 100, PF_NUM, 0},
    [GP_BAND3] = {"380", 0, 127, 100, PF_NUM, 0}, [GP_BAND4] = {"600", 0, 127, 100, PF_NUM, 0},
    [GP_BAND5] = {"950", 0, 127, 100, PF_NUM, 0}, [GP_BAND6] = {"1.5K", 0, 127, 100, PF_NUM, 0},
    [GP_BAND7] = {"2.4K", 0, 127, 100, PF_NUM, 0}, [GP_BAND8] = {"3.8K", 0, 127, 100, PF_NUM, 0},
    [GP_BANDQ] = {"RESO", 0, 127, 80, PF_NUM, 0},
    [GP_BANDSHIFT] = {"SHIFT", -12, 12, 0, PF_SEMI, 0},
    [GP_BANDLEVEL] = {"LEVEL", 0, 127, 100, PF_NUM, 0},
    [GP_BANDSHAPE] = {"SWEEP", 0, N(bandshape_names), 0, E(bandshape_names)},
    [GP_BANDRATE] = {"RATE", 0, 127, 50, PF_NUM, 0},
    [GP_BANDDEPTH] = {"DEPTH", 0, 127, 0, PF_NUM, 0},
    [GP_BANDWIDTH] = {"WIDTH", 0, 127, 30, PF_NUM, 0},
    [GP_DLYMODE] = {"MODE", 0, N(dlymode_names), 0, E(dlymode_names)},
    [GP_GRDENS] = {"DENSITY", 0, 127, 60, PF_NUM, 0},
    [GP_GRSIZE] = {"SIZE", 0, 127, 60, PF_NUM, 0},
    [GP_GRPITCH] = {"PITCH", -24, 24, 0, PF_SEMI, 0},
    [GP_REVMODE] = {"MODE", 0, N(revmode_names), 0, E(revmode_names)},
    [GP_METRO] = {"METRO", 0, N(metro_names), 0, E(metro_names)},
    [GP_COUNTIN] = {"COUNT-IN", 0, N(countin_names), 1, E(countin_names)},
};

static const rv_pinfo NONE = {0, 0, 0, 0, PF_NUM, 0};

const rv_pinfo *rv_pinfo_track(int t, int p)
{
    if (p >= TP_S0 && p <= TP_S15) {
        int kind = rv.tp[t & 7][TP_KIND], s = p - TP_S0;
        const rv_pinfo *tab;
        if (kind == KIND_PERC) {
            const rv_pinfo *q = perc_pinfo(rv.tp[t & 7][TP_S0], s);
            if (rv.tp[t & 7][TP_ENGINE] == 1 && s <= 8)   /* a KIT: the lanes hold the machines (LANE pages) */
                return &NONE;
            return q->name ? q : &NONE;
        }
        tab = kind == KIND_STRIKE ? SRC_STRIKE : kind == KIND_TAPE ? SRC_TAPE
                                                                                   : SRC_SYNTH[rv.tp[t & 7][TP_ENGINE] % ENG_N];
        return tab[s].name ? &tab[s] : &NONE;
    }
    if (p < 0 || p >= TP_N || !TRACK[p].name)
        return &NONE;
    if ((p == TP_CUT || p == TP_RES) && FT_IS_Z(rv.tp[t & 7][TP_FTYPE])) {   /* Z filters: two axes */
        static const rv_pinfo morph = {"MORPH", 0, 127, 90, PF_NUM, 0}, xform = {"XFORM", 0, 127, 20, PF_NUM, 0};
        return p == TP_CUT ? &morph : &xform;
    }
    if (p == TP_ENGINE && rv.tp[t & 7][TP_KIND] == KIND_PERC) {   /* PERC: one machine, or a kit of six */
        static const char *const pm[] = {"SINGLE", "KIT"};
        static const rv_pinfo mode = {"MODE", 0, 1, 0, PF_ENUM, pm};
        return &mode;
    }
    if (p == TP_POLY && (rv.tp[t & 7][TP_KIND] == KIND_STRIKE || rv.tp[t & 7][TP_KIND] == KIND_PERC)) {
        static const rv_pinfo strike_poly = {"VOICES", 0, 1, 1, PF_ENUM, polys};
        return &strike_poly;
    }
    return &TRACK[p];
}

const rv_pinfo *rv_pinfo_global(int g)
{
    if (rv.gp[GP_DLYMODE] == 1 && (g == GP_DLYTIME || g == GP_DLYSPREAD || g == GP_DLYWOB)) {   /* GRAINS reads them its way */
        static const rv_pinfo gt[3] = {{"POSITN", 0, 127, 60, PF_NUM, 0}, {"STEREO", 0, 127, 127, PF_NUM, 0}, {"RANDOM", 0, 127, 0, PF_NUM, 0}};
        return &gt[g == GP_DLYTIME ? 0 : g == GP_DLYSPREAD ? 1 : 2];
    }
    if (rv.gp[GP_REVMODE] == 1 && g == GP_REVSIZE) {                   /* CLOUDS: SIZE is how long it rings */
        static const rv_pinfo dec = {"DECAY", 0, 127, 70, PF_NUM, 0};
        return &dec;
    }
    return g >= 0 && g < GP_N && GLOBAL[g].name ? &GLOBAL[g] : &NONE;
}

const rv_pinfo *rv_pinfo_pid(int pid)
{
    if (pid < 0 || pid >= RV_NPID)
        return &NONE;
    return pid < RV_GPID ? rv_pinfo_track(pid / RV_TP, pid % RV_TP) : rv_pinfo_global(pid - RV_GPID);
}

int rv_pid_get(int pid)
{
    if (pid < 0 || pid >= RV_NPID)
        return 0;
    return pid < RV_GPID ? rv.tp[pid / RV_TP][pid % RV_TP] : rv.gp[pid - RV_GPID];
}

void rv_pid_set(int pid, int v)
{
    const rv_pinfo *pi = rv_pinfo_pid(pid);
    if (!pi->name)
        return;
    if (v < pi->min)
        v = pi->min;
    if (v > pi->max)
        v = pi->max;
    if (pid < RV_GPID)
        rv.tp[pid / RV_TP][pid % RV_TP] = (int16_t)v;
    else
        rv.gp[pid - RV_GPID] = (int16_t)v;
}

/* ----------------------------------------------------------------- display --- */
static int put_str(char *o, int i, int max, const char *s)
{
    while (*s && i < max - 1)
        o[i++] = *s++;
    o[i] = 0;
    return i;
}

static int put_int(char *o, int i, int max, int v, int plus)
{
    char tmp[12];
    int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)-v : (unsigned)v;
    do {
        tmp[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && n < 11);
    if (neg && i < max - 1)
        o[i++] = '-';
    else if (plus && v > 0 && i < max - 1)
        o[i++] = '+';
    while (n && i < max - 1)
        o[i++] = tmp[--n];
    o[i] = 0;
    return i;
}

/* the time a TIME parameter stands for, in seconds: 0.5 ms .. 8 s */
float rv_time_s(int v) { return 0.0005f * fm_exp2f((float)v * (13.97f / 127.0f)); }
float rv_cut_hz(float v) { return 20.0f * fm_exp2f(v * (10.0f / 127.0f)); }

static const char *dest_name(int p)
{
    static const rv_pinfo none = {"-", 0, 0, 0, PF_NUM, 0};
    (void)none;
    return p >= 0 && p < TP_N && TRACK[p].name ? TRACK[p].name : "-";
}

void rv_param_format(int pid, int v, char *o, int max)
{
    const rv_pinfo *pi = rv_pinfo_pid(pid);
    int i = 0;
    o[0] = 0;
    if (!pi->name)
        return;
    switch (pi->fmt) {
    case PF_ENUM:
        if (pi->names && v >= pi->min && v <= pi->max)
            put_str(o, 0, max, pi->names[v - pi->min]);
        break;
    case PF_ONOFF:
        put_str(o, 0, max, v ? "ON" : "OFF");
        break;
    case PF_BI:
    case PF_SEMI:
        put_int(o, 0, max, v, 1);
        break;
    case PF_CENT:
        i = put_int(o, 0, max, v, 1);
        put_str(o, i, max, "c");
        break;
    case PF_HZ: {
        float hz = rv_cut_hz((float)v);
        if (hz >= 1000.0f) {
            int k10 = (int)(hz / 100.0f + 0.5f);
            i = put_int(o, 0, max, k10 / 10, 0);
            if (k10 < 100) {
                i = put_str(o, i, max, ".");
                i = put_int(o, i, max, k10 % 10, 0);
            }
            put_str(o, i, max, "k");
        } else {
            put_int(o, 0, max, (int)(hz + 0.5f), 0);
        }
        break;
    }
    case PF_TIME: {
        float s = rv_time_s(v);
        if (s < 1.0f) {
            int ms = (int)(s * 1000.0f + 0.5f);
            i = put_int(o, 0, max, ms, 0);
            put_str(o, i, max, "ms");
        } else {
            int d = (int)(s * 10.0f + 0.5f);
            i = put_int(o, 0, max, d / 10, 0);
            i = put_str(o, i, max, ".");
            i = put_int(o, i, max, d % 10, 0);
            put_str(o, i, max, "s");
        }
        break;
    }
    case PF_DEST:
        put_str(o, 0, max, pid < RV_GPID && v >= TP_S0 && v <= TP_S15 ? rv_pinfo_track(pid / RV_TP, v)->name ? rv_pinfo_track(pid / RV_TP, v)->name : "-" : dest_name(v));
        break;
    case PF_BPM:
    default:
        put_int(o, 0, max, v, 0);
        break;
    }
}

const char *rv_pid_label(int pid, char *buf, int max)
{
    int i = 0;
    const rv_pinfo *pi = rv_pinfo_pid(pid);
    buf[0] = 0;
    if (pid < RV_GPID) {
        i = put_str(buf, 0, max, "T");
        i = put_int(buf, i, max, pid / RV_TP + 1, 0);
        i = put_str(buf, i, max, " ");
    }
    put_str(buf, i, max, pi->name ? pi->name : "-");
    return buf;
}

/* does this knob do anything with the track as it is set now? (the page shows a dash if not) */
int rv_tp_applies(int t, int p)
{
    const int16_t *tp = rv.tp[t];
    if (p == TP_ENGINE && tp[TP_KIND] != KIND_SYNTH && tp[TP_KIND] != KIND_PERC)
        return 0;
    if (tp[TP_KIND] == KIND_PERC && tp[TP_ENGINE] == 1 && (p == TP_POLY || p == TP_SEQMODE || p == TP_DIR || p == TP_PITCH || p == TP_FINE))
        return 0;                                                      /* a KIT: a voice per lane, steps forward */
    if (p == TP_VELO && tp[TP_KIND] != KIND_SYNTH && tp[TP_KIND] != KIND_PERC)   /* strike and tape take velocity as it comes */
        return 0;
    if (tp[TP_KIND] == KIND_PERC) {                                    /* the machines make their own envelopes */
        switch (p) {
        case TP_ATK: case TP_ACURVE: case TP_OVER: case TP_DEC: case TP_SUS: case TP_LOOP: case TP_REL: case TP_TAIL:
        case TP_GIVE: case TP_ESHAPE: case TP_EMORPH: case TP_ETIME: case TP_EMODE: case TP_EWARP:
            return 0;
        default: break;
        }
    }                                                      /* take velocity as it comes */
    if (p == TP_POLY && tp[TP_KIND] == KIND_TAPE)                      /* a tape track is one playhead */
        return 0;
    if ((p == TP_FTYPE || p == TP_CUT || p == TP_RES || p == TP_KTRK || p == TP_EFILT || p == TP_FMODE || p == TP_FBP) && tp[TP_KIND] != KIND_SYNTH)
        return 0;                                                      /* only synth voices have the filter */
    if (p == TP_FMODE)                                                 /* the SEM's sweep; its band pass replaces it */
        return tp[TP_FTYPE] == FT_SEM && !tp[TP_FBP];
    if (p == TP_FBP)
        return tp[TP_FTYPE] == FT_SEM;
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_CZ && (p == TP_ESHAPE || p == TP_EMORPH || p == TP_ETIME || p == TP_EMODE || p == TP_EWARP))
        return 0;                                                      /* the CZ's DCA envelopes are its level */
    if (tp[TP_ESHAPE] > 0 && !(tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_CZ)) {   /* SHAPE replaces the classic articulation */
        switch (p) {
        case TP_ATK: case TP_ACURVE: case TP_OVER: case TP_DEC: case TP_SUS: case TP_LOOP: case TP_TAIL:
            return 0;
        case TP_REL: return tp[TP_EMODE] == SHM_LOOP;                  /* LOOP lets go the classic way */
        default: break;
        }
    } else if (p == TP_EMORPH || p == TP_ETIME || p == TP_EMODE || p == TP_EWARP) {
        return 0;
    }
    if (tp[TP_KIND] == KIND_STRIKE) {
        int exc = tp[TP_S0], body = tp[TP_S1];
        int grains = exc == EXC_DUST || exc == EXC_RAIN;
        switch (p) {
        case TP_S3: return body == BODY_MODAL || body == BODY_DISC || exc <= EXC_FM || exc == EXC_BOW;   /* TONE */
        case TP_S4: case TP_S6: return body != BODY_PING;                  /* DAMP, GEOMETRY */
        case TP_S5: return body != BODY_PING;                               /* INHARM */
        case TP_S7: return body == BODY_MODAL || body == BODY_DISC;         /* MODES */
        case TP_S8: case TP_S9: case TP_S15: return grains;                 /* ORDER, RATE, SEED */
        case TP_S11: return tp[TP_S10] > 0;                                 /* T DECAY: with a TRANS layer */
        case TP_S12: return tp[TP_S13] > 0;                                 /* RING F: with RING */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_CZ) {
        int two = tp[TP_S0] >= 2;                                          /* 1+1' or 1+2' */
        switch (p) {
        case TP_ATK: case TP_ACURVE: case TP_OVER: case TP_GIVE: case TP_DEC: case TP_SUS:
        case TP_LOOP: case TP_REL: case TP_TAIL:
            return 0;                                                      /* the CZ's DCA envelopes are the level */
        case TP_S2: case TP_S4: case TP_S5: case TP_S6: case TP_S7: return two;   /* MOD, detune: two lines */
        case TP_S8: case TP_S9: case TP_S10: return tp[TP_S11] > 0;         /* vibrato: with DEPTH */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_CHIP) {
        int w = tp[TP_S0], steps = tp[TP_S7];
        switch (p) {
        case TP_S1: return w == CW_PULSE || w == CW_SAWPUL || w == CW_RING || w == CW_PWM;   /* PW (RING: ratio) */
        case TP_S2: case TP_S3: case TP_S4: return steps >= 2;             /* SPEED, MODE, OF2 */
        case TP_S5: return steps >= 3;                                      /* OF3 */
        case TP_S6: return steps >= 4;                                      /* OF4 */
        case TP_S9: return tp[TP_S8] > 0 || tp[TP_S10] > 0;                 /* SUB OCT: with SUB or SYNC */
        case TP_S15: return tp[TP_S14] > 0;                                 /* FRAG: with DECOMP */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_ORGAN) {
        switch (p) {
        case TP_S14: return tp[TP_S13] > 0;                                 /* TREM RT: with TREMLNT */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_MONO) {
        switch (p) {
        case TP_S3: return tp[TP_S2] > 0;                                   /* SUB OCT: with SUB */
        case TP_S5: case TP_S6: return tp[TP_S1] > 0;                       /* PW, PWM: with PULSE */
        case TP_S8: return tp[TP_S7] > 0;                                   /* SCOOP T: with SCOOP */
        case TP_S10: case TP_S11: return tp[TP_S9] > 0;                     /* the vibrato's rate and delay */
        case TP_S14: return tp[TP_POLY] == 0;                               /* RETRIG: a mono track */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_LOGIC) {
        int bitop = tp[TP_S5] >= LO_AND && tp[TP_S5] <= LO_NAND, bitop2 = tp[TP_S6] >= LO_AND && tp[TP_S6] <= LO_NAND;
        int quant = tp[TP_S5] == LO_QUANT || tp[TP_S6] == LO_QUANT, c = tp[TP_S7] > 0;
        switch (p) {
        case TP_S3: case TP_S4: case TP_S6: return c;                       /* C and OP 2: with OP 2 AMT */
        case TP_S8: return bitop || quant || (c && bitop2);                 /* BITS: for the bit operators */
        case TP_S9: return tp[TP_S0] == LW_PULSE || tp[TP_S2] == LW_PULSE || (c && tp[TP_S4] == LW_PULSE);
        case TP_S14: case TP_S15: return tp[TP_S13] > 0;                    /* the grains */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_WAVE) {
        int two = tp[TP_S6] > 0 || tp[TP_S8] == WX_RING;                    /* osc 2 is heard: with MIX, or rung into osc 1 */
        switch (p) {
        case TP_S2: case TP_S3: case TP_S4: case TP_S5: return two;          /* osc 2 */
        case TP_S9: return tp[TP_S8] != WX_OFF && two;                     /* XM AMT: SYNC and FM change osc 2, heard with MIX */
        case TP_S11: return tp[TP_S10] != WW_OFF;                           /* WARP AMT */
        case TP_S13: case TP_S14: return tp[TP_S12] > 0;                    /* the wave envelope */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_SQ) {
        int two = tp[TP_S6] > 0 || tp[TP_S8] != SQM_OFF, three = tp[TP_S7] > 0 || tp[TP_S12] > 0;
        switch (p) {
        case TP_S1: case TP_S3: return two;                                 /* WAVE 2, SEMI 2 */
        case TP_S2: case TP_S4: return three;                               /* WAVE 3, SEMI 3 */
        case TP_S5: return two || three;                                    /* DETUNE */
        case TP_S14: return tp[TP_S13] > 0;                                 /* VIB RATE: with VIBRATO */
        default: return 1;
        }
    }
    if (tp[TP_KIND] == KIND_SYNTH && tp[TP_ENGINE] == ENG_PHYS) {
        int model = tp[TP_S0];                                              /* STRING, BOWED, BLOWN, MEMBRAN */
        switch (p) {
        case TP_S3: return model <= 1;                                      /* POSITN: pluck or bow point */
        case TP_S6: return model != 3;                                      /* BODY */
        default: return 1;
        }
    }
    return 1;
}

/* parameters modulation may move: continuous ones (an engine's numbered slots too) */
int rv_is_moddest(int p)
{
    switch (p) {
    case TP_PITCH: case TP_FINE: case TP_CUT: case TP_RES: case TP_DRIVE: case TP_FOLD: case TP_BITS:
    case TP_DEREZ: case TP_MASK: case TP_VOC: case TP_INA: case TP_INB: case TP_INMIX: case TP_LEVEL:
    case TP_PAN: case TP_FBS: case TP_PAIRS: case TP_DLYS: case TP_REVS: case TP_BANDS: case TP_FMODE: case TP_EMORPH: case TP_ETIME: case TP_EWARP: case TP_ATK: case TP_DEC:
    case TP_SUS: case TP_REL: case TP_GIVE: case TP_OVER: case TP_TAIL: case TP_GLIDE:
        return 1;
    default:
        return p >= TP_S0 && p <= TP_S15;
    }
}

/* the next destination that does something on track t (named, and active as the track is set) */
int rv_moddest_next_t(int t, int p, int dir)
{
    int i;
    for (i = 0; i < TP_N; i++) {
        p += dir;
        if (p < 0)
            p = TP_N - 1;
        if (p >= TP_N)
            p = 0;
        if (rv_is_moddest(p) && rv_pinfo_track(t, p)->name && rv_tp_applies(t, p))
            return p;
    }
    return rv_moddest_next(p, dir);
}

int rv_moddest_next(int p, int dir)
{
    int i;
    for (i = 0; i < TP_N; i++) {
        p += dir;
        if (p < 0)
            p = TP_N - 1;
        if (p >= TP_N)
            p = 0;
        if (rv_is_moddest(p))
            return p;
    }
    return p;
}

/* ---------------------------------------------------------------- defaults --- */
static void track_src_defaults(int t)
{
    int s;
    for (s = rv.tp[t][TP_KIND] == KIND_PERC ? 1 : 0; s < 16; s++) {   /* PERC keeps its machine: the rest are its defaults */
        const rv_pinfo *pi = rv_pinfo_track(t, TP_S0 + s);
        rv.tp[t][TP_S0 + s] = pi->name ? pi->def : 0;
    }
}
void rv_track_src_defaults(int t) { track_src_defaults(t); }

/* a plain CZ tone: line 1 a saw whose DCW opens and settles, DCA up, held, released */
/* the track around an engine that needs one: ORGAN wants open, held pipes; MONO the SH-101's filter
 * following the keys, one voice, the bowed-or-plucked envelope (cutoff 1.1 kHz, reso 12%, env 55%,
 * key tracking full, A 22 ms D 260 ms S 48% R 120 ms, glide 18 ms) */
void rv_engine_track_defaults(int t, int engine)
{
    int16_t *tp = rv.tp[t & 7];
    if (engine == ENG_ORGAN) {
        tp[TP_POLY] = 3;
        tp[TP_FTYPE] = FT_LP;
        tp[TP_CUT] = 127;
        tp[TP_RES] = 0;
        tp[TP_EFILT] = 0;
        tp[TP_KTRK] = 0;
        tp[TP_ATK] = 8;
        tp[TP_SUS] = 127;
        tp[TP_REL] = 66;
        tp[TP_VELO] = 0;
    } else if (engine == ENG_MONO) {
        tp[TP_POLY] = 0;
        tp[TP_FTYPE] = FT_SH101;
        tp[TP_CUT] = 73;
        tp[TP_RES] = 15;
        tp[TP_EFILT] = 35;
        tp[TP_KTRK] = 127;
        tp[TP_ATK] = 50;
        tp[TP_DEC] = 82;
        tp[TP_SUS] = 61;
        tp[TP_REL] = 72;
        tp[TP_GLIDE] = 12;
    }
}

/* a kit: kick, snare, closed and open hats, clap, rim (TRX), each at its machine's defaults */
void rv_kit_init(int t)
{
    static const uint8_t m[RV_LANES] = {PM_TRX_BD, PM_TRX_SD, PM_TRX_CH, PM_TRX_OH, PM_TRX_CP, PM_TRX_RS};
    rv_kit *k = &rv.kit[t & 7];
    int l, i;
    for (i = 0; i < RV_KITLOCKS; i++)
        rv.kitlk[t & 7][i].what = 0xFF;
    for (l = 0; l < RV_LANES; l++) {
        k->mach[l] = m[l];
        for (i = 0; i < 8; i++)
            k->p[l][i] = (uint8_t)(perc_pi[m[l]][i].name ? perc_pi[m[l]][i].def : 0);
        k->level[l] = 100;
        k->len[l] = 0;
        k->chance[l] = 100;
    }
}

void rv_cz_init(int t)
{
    static const rv_czline plain = {
        1, 0, 0, 0,
        {{{99, 99, 99, 99, 99, 99, 99, 99}, {0, 0, 0, 0, 0, 0, 0, 0}, 0, 1},              /* DCO: flat */
         {{90, 60, 70, 99, 99, 99, 99, 99}, {80, 45, 0, 0, 0, 0, 0, 0}, 2, 3},            /* DCW: open, settle, close */
         {{99, 70, 60, 99, 99, 99, 99, 99}, {99, 85, 0, 0, 0, 0, 0, 0}, 2, 3}}};          /* DCA: up, hold, release */
    rv.cz[t & 7].line[0] = plain;
    rv.cz[t & 7].line[1] = plain;
}

void rv_track_defaults(int t, int kind, int engine)
{
    int p;
    for (p = 0; p < TP_N; p++)
        rv.tp[t][p] = TRACK[p].name ? TRACK[p].def : 0;
    rv.tp[t][TP_KIND] = (int16_t)kind;
    rv.tp[t][TP_ENGINE] = (int16_t)engine;
    track_src_defaults(t);
    if (kind == KIND_SYNTH && engine == ENG_CZ)
        rv_cz_init(t);
    if (kind == KIND_SYNTH && engine == ENG_SQ)
        rv.tp[t][TP_FTYPE] = FT_SQ80;
    if (kind == KIND_SYNTH && (engine == ENG_ORGAN || engine == ENG_MONO))
        rv_engine_track_defaults(t, engine);
    if (kind == KIND_PERC) {
        rv_kit_init(t);
        rv.tp[t][TP_POLY] = 0;
        rv.tp[t][TP_VELO] = 64;
    }
    if (kind == KIND_STRIKE) {
        rv.tp[t][TP_POLY] = 1;
        rv.tp[t][TP_ATK] = 0;
        rv.tp[t][TP_DEC] = 110;
        rv.tp[t][TP_SUS] = 127;
        rv.tp[t][TP_REL] = 80;
        rv.tp[t][TP_CUT] = 127;
        rv.tp[t][TP_RES] = 0;
        rv.tp[t][TP_EFILT] = 0;
        rv.tp[t][TP_FTYPE] = FT_LP;
        rv.tp[t][TP_KTRK] = 0;
        rv.tp[t][TP_DRIVE] = 0;
    } else if (kind == KIND_TAPE) {
        rv.tp[t][TP_POLY] = 0;
        rv.tp[t][TP_ATK] = 6;
        rv.tp[t][TP_DEC] = 80;
        rv.tp[t][TP_SUS] = 127;
        rv.tp[t][TP_REL] = 50;
        rv.tp[t][TP_CUT] = 127;
        rv.tp[t][TP_RES] = 0;
        rv.tp[t][TP_EFILT] = 0;
        rv.tp[t][TP_FTYPE] = FT_LP;
        rv.tp[t][TP_KTRK] = 0;
        rv.tp[t][TP_DRIVE] = 0;
    }
}

void rv_project_init(void)
{
    int t, g, i, s;
    uint8_t *p = (uint8_t *)&rv;
    for (i = 0; i < (int)sizeof(rv); i++)
        p[i] = 0;
    rv.magic = RV_MAGIC;
    rv.format = RV_FORMAT;
    put_str(rv.name, 0, 12, "INIT");
    for (t = 0; t < RV_TRACKS; t++) {
        rv_cz_init(t);                              /* every track has a CZ patch ready */
        rv_track_defaults(t, t < 4 ? KIND_SYNTH : t < 6 ? KIND_STRIKE : KIND_TAPE, t < 4 ? t : 0);
    }
    for (g = 0; g < GP_N; g++)
        rv.gp[g] = GLOBAL[g].name ? GLOBAL[g].def : 0;
    for (t = 0; t < RV_TRACKS; t++) {
        for (i = 0; i < 16; i++)
            rv.arcs[t][i] = 100;
        for (s = 0; s < RV_PATTERNS; s++)
            for (i = 0; i < RV_STEPS; i++) {
                rv_step *st = &rv.pat[t][s].s[i];
                st->note = 60;
                st->vel = 100;
                st->len = 4;
                st->chance = 100;
            }
    }
    /* the envelope library: 256 shapes from three values each (attack shape, body, death),
     * laid out so neighbours are related: the low nibble walks attack and body, the high death */
    for (i = 0; i < RV_LIB; i++) {
        int a = i & 7, b = (i >> 3) & 3, d = (i >> 5) & 7;
        rv.lib[i][0] = (uint8_t)(a * 18);                  /* attack: instant .. slow swell */
        rv.lib[i][1] = (uint8_t)(b * 42);                  /* body: percussive .. held */
        rv.lib[i][2] = (uint8_t)(d * 18);                  /* death: cut .. long tail */
    }
    for (i = 0; i < RV_MATRIX; i++)
        rv.mat[i].src = MS_OFF;
    for (i = 0; i < RV_MACROS; i++)
        for (s = 0; s < RV_MACRO_DESTS; s++) {
            rv.macro[i].d[s].dst = 0xFFFF;
            rv.macro[i].d[s].end = 63;
            rv.macro[i].d[s].hi = 100;
        }
    rv.arr_len = 1;
    rv.arr[0].scene = 0;
    rv.arr[0].bars = 4;
}
