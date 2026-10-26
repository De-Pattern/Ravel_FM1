/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: every parameter's name, range, default and display, and the project's defaults. */
#include "rv.h"
#include "rv_shapes.h"
#include "rv_wavetables.h"
#include "rv_organ.h"

/* ------------------------------------------------------------------ names --- */
const char *const rv_kind_names[] = {"SYNTH", "STRIKE", "TAPE", "PERC"};
const char *const rv_engine_names[] = {"VA", "CZ", "FM", "PHYS", "FORMANT", "PARTICLE", "CHIP", "WAVE", "LOGIC", "ORGAN", "MONO", "RESIN", "TINE"};
const char *const rv_route_names[] = {"-", "MOD", "TRIG", "EXCITE", "TAPE", "CLOCK", "RING", "SPARK"};
const char *const rv_msrc_names[] = {"OFF", "LFO1", "LFO2", "DUST", "ENV", "VELO", "WHEEL", "MACRO1", "MACRO2", "MACRO3", "MACRO4", "CLOCK", "KEY"};
const char *const rv_cond_names[] = {"ALWAYS", "FIRST", "NOT 1ST", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "NO FILL"};
const char *const rv_scale_names[] = {"CHROMA", "MAJOR", "MINOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "PENTA", "MINPENT", "WHOLE", "HIRAJO", "IWATO"};
static const char *const ftype_names[] = {"LADDER", "LOWPASS", "BANDPASS", "HIGHPASS", "LPG", "MS-20", "SH-101", "Z VOWEL", "Z TALK", "Z PHASER", "Z PEAKS", "Z SWEEP", "Z METAL", "XT LP24", "XT BP24", "XT NOTCH", "XT S&H", "XT SHAPE", "SEM"};
static const char *const ins_names[] = {"OFF", "COMB", "SHIFT", "CRUSH", "SMEAR"};
static const char *const dir_names[] = {"FWD", "REV", "PINGPONG", "SNAKE", "GRAPH"};
static const char *const seqmode_names[] = {"STEPS", "GRAPH"};
static const char *const role_names[] = {"OFF", "NOTES", "RHYTHM", "MELODY"};   /* head 2: ROLE_GATE, ROLE_PITCH */
static const char *const role1_names[] = {"NOTES", "GATE", "PITCH"};
static const char *const fg_names[] = {"OFF", "RISE", "FALL", "ARCH", "DECAY", "SWELL", "LFO 1", "LFO 2", "DUST"};
static const char *const comb_names[] = {"x CLOCK", "+ CLOCK"};
static const char *const target_names[] = {"THIS", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const rate_names[] = {"1/4", "1/3", "1/2", "2/3", "1", "3/2", "2", "3", "4"};
const char *const rv_lrate_names[] = {"X1/4", "X1/3", "X1/2", "X2/3", "TRK", "X3/2", "X2", "X3", "X4"};   /* a kit lane's, against the track's */
static const char *const clksrc_names[] = {"MASTER", "LFO1", "LFO2", "DUST"};
static const char *const branch_names[] = {"CHANCE", "TURNS", "LFO1", "LFO2", "MOTION"};
static const char *const onoff_names[] = {"OFF", "ON"};
static const char *const clkmode_names[] = {"STEADY", "RATE", "SHAPE", "PHASE", "SUM", "AND", "OR", "XOR"};
static const char *const lock_names[] = {"LOCKED", "FREE"};
static const char *const lfo_names[] = {"SINE", "TRI", "RAMP", "SAW", "SQUARE", "S&H", "DRIFT"};
static const char *const lfo_modes[] = {"LOOP", "RETRIG", "1 SHOT"};
static const char *const fbproc_names[] = {"FILTER", "COMB", "SMEAR", "SHIFT", "FOLD", "CRUSH"};
static const char *const fbret_names[] = {"MASTER", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const senv_names[RV_SENV + 1] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                                     "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32"};
typedef char senv_size_check[sizeof(rv_senv) == 24 ? 1 : -1];   /* the LIBRARY fills the triplets' room exactly */
static const char *const metro_names[] = {"OFF", "QUIET", "LOUD"};
static const char *const countin_names[] = {"OFF", "1 BAR", "2 BARS"};
static const char *const cutshape_names[] = {"EVEN", "BOUNCE", "DRAG"};
static const char *const glide_names[] = {"JUMP", "1 BAR", "2 BARS", "3 BARS", "4 BARS", "5 BARS", "6 BARS", "7 BARS", "8 BARS"};
static const char *const arcspan_names[] = {"1 BAR", "2 BARS", "4 BARS"};
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
static const char *const phys_models[] = {"STRING", "BOWED", "BLOWN", "MEMBRAN"};
static const char *const form_modes[] = {"FILTER", "FOF"};
static const char *const part_tables[] = {"SINE", "GLASS", "REED", "VOICE", "BELL", "BITS", "NOISE", "DROP"};
static const char *const exc_names[] = {"CLICK", "NOISE", "METAL", "FM", "DUST", "RAIN", "BOW", "BLOW", "EXT"};   /* EXT: only from outside (EXC_INPUT) */
static const char *const body_names[] = {"PING", "MODAL", "DISC", "STRINGS"};

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
/* RESIN: the bowed string, or (MODEL HORN, its own names below) the reed and bore; the vibrato,
 * DRIFT and QUAKE are the same on both */
static const char *const resin_models[] = {"BOW", "HORN"};
#define RS_MOVE {"VIB RT", 0, 127, 55, PF_NUM, 0}, {"VIB DP", 0, 127, 40, PF_NUM, 0}, {"ONSET", 0, 127, 50, PF_NUM, 0}, \
                {"DRIFT", 0, 127, 30, PF_NUM, 0}, {"QUAKE", 0, 127, 0, PF_NUM, 0}
static const rv_pinfo SRC_RESIN[16] = {
    {"MODEL", 0, N(resin_models), 0, E(resin_models)}, {"PRESS", 0, 127, 70, PF_NUM, 0}, {"SPEED", 0, 127, 50, PF_NUM, 0},
    {"POSITN", 0, 127, 25, PF_NUM, 0}, {"SCRAPE", 0, 127, 20, PF_NUM, 0}, {"HARMON", 0, 127, 0, PF_NUM, 0},
    {"BODY", 0, 127, 70, PF_NUM, 0}, {"SIZE", 0, 127, 30, PF_NUM, 0}, RS_MOVE,
    {"BOW ATK", 0, 127, 40, PF_NUM, 0}, {"ROSIN", 0, 127, 50, PF_NUM, 0}, {"HISS", 0, 127, 15, PF_NUM, 0}};
static const rv_pinfo SRC_RESIN_HORN[16] = {
    {"MODEL", 0, N(resin_models), 1, E(resin_models)}, {"PRESS", 0, 127, 70, PF_NUM, 0}, {"REED", 0, 127, 60, PF_NUM, 0},
    {"BORE", 0, 127, 30, PF_NUM, 0}, {"GROWL", 0, 127, 0, PF_NUM, 0}, {"MULTI", 0, 127, 0, PF_NUM, 0},
    {"THROAT", 0, 127, 50, PF_NUM, 0}, {"SIZE", 0, 127, 60, PF_NUM, 0}, RS_MOVE,
    {"ATTACK", 0, 127, 35, PF_NUM, 0}, {"KEYS", 0, 127, 40, PF_NUM, 0}, {"AIR", 0, 127, 25, PF_NUM, 0}};
/* TINE: KALIMBA (a thumb piano's rusty tines, the rings that buzz on them, its box and hole),
 * MARIMBA (tuned bars, tube resonators, rolls, a balafon's buzzing gourds) and EPIANO (a tine and
 * tonebar before a pickup, after the Rhodes); each model names its own knobs */
static const char *const tine_models[] = {"KALIMBA", "MARIMBA", "EPIANO"};
#define TN_TAIL(a, b) {"DETUNE", 0, 127, a, PF_NUM, 0}, {b, 0, 127, 0, PF_NUM, 0}, {"DAMP", 0, 127, 50, PF_NUM, 0}, {"DRIFT", 0, 127, 15, PF_NUM, 0}
static const rv_pinfo SRC_TINE[16] = {
    {"MODEL", 0, N(tine_models), 0, E(tine_models)}, {"PLUCK", 0, 127, 70, PF_NUM, 0}, {"DECAY", 0, 127, 70, PF_NUM, 0},
    {"TONE", 0, 127, 60, PF_NUM, 0}, {"RUST", 0, 127, 50, PF_NUM, 0}, {"BUZZ", 0, 127, 40, PF_NUM, 0},
    {"RATTLE", 0, 127, 60, PF_NUM, 0}, {"THICK", 0, 127, 60, PF_NUM, 0}, {"BOX", 0, 127, 60, PF_NUM, 0},
    {"HOLE", 0, 127, 64, PF_NUM, 0}, {"WAH", 0, 127, 0, PF_NUM, 0}, {"THUMB", 0, 127, 40, PF_NUM, 0}, TN_TAIL(25, "BEND")};
static const rv_pinfo SRC_TINE_MAR[16] = {
    {"MODEL", 0, N(tine_models), 1, E(tine_models)}, {"MALLET", 0, 127, 55, PF_NUM, 0}, {"DECAY", 0, 127, 60, PF_NUM, 0},
    {"TONE", 0, 127, 55, PF_NUM, 0}, {"POSITN", 0, 127, 25, PF_NUM, 0}, {"TUBE", 0, 127, 70, PF_NUM, 0},
    {"WOOD", 0, 127, 50, PF_NUM, 0}, {"STICK", 0, 127, 25, PF_NUM, 0}, {"ROLL", 0, 127, 0, PF_NUM, 0},
    {"ROLL RT", 0, 127, 64, PF_NUM, 0}, {"BUZZ", 0, 127, 0, PF_NUM, 0}, {"RATTLE", 0, 127, 60, PF_NUM, 0}, TN_TAIL(5, "HUMAN")};
static const rv_pinfo SRC_TINE_EP[16] = {
    {"MODEL", 0, N(tine_models), 2, E(tine_models)}, {"HAMMER", 0, 127, 60, PF_NUM, 0}, {"DECAY", 0, 127, 75, PF_NUM, 0},
    {"TONE", 0, 127, 55, PF_NUM, 0}, {"BELL", 0, 127, 50, PF_NUM, 0}, {"BARK", 0, 127, 45, PF_NUM, 0},
    {"PICKUP", 0, 127, 55, PF_NUM, 0}, {"TONEBAR", 0, 127, 60, PF_NUM, 0}, {"TREM", 0, 127, 0, PF_NUM, 0},
    {"TREM RT", 0, 127, 50, PF_NUM, 0}, {"DRIVE", 0, 127, 15, PF_NUM, 0}, {"THUMP", 0, 127, 30, PF_NUM, 0}, TN_TAIL(5, "KEY OFF")};
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
/* CHIP: a SID-flavoured voice that arpeggiates through offsets (after the Syntakt's SY CHIP),
 * then a decompose stage: sample-rate reduction, bit crushing and fragments that break apart */
static const rv_pinfo SRC_CHIP[16] = {
    {"WAVE", 0, N(chip_waves), CW_PULSE, E(chip_waves)}, {"PW", 0, 127, 64, PF_NUM, 0}, {"RATE", 0, 127, 80, PF_NUM, 0},
    {"MODE", 0, N(chip_modes), 0, E(chip_modes)}, {"OF2", -24, 24, 3, PF_SEMI, 0}, {"OF3", -24, 24, 7, PF_SEMI, 0},
    {"OF4", -24, 24, 12, PF_SEMI, 0}, {"STEPS", 1, 4, 1, PF_NUM, 0}, {"SUB", 0, 127, 0, PF_NUM, 0},
    {"SUB OCT", 0, N(chip_suboct), 2, E(chip_suboct)}, {"SYNC", 0, 1, 0, PF_ONOFF, 0}, {"OVER", 0, 127, 0, PF_NUM, 0},
    {"SRR", 0, 127, 0, PF_NUM, 0}, {"CRUSH", 1, 16, 16, PF_NUM, 0}, {"DECOMP", 0, 127, 0, PF_NUM, 0},
    {"FRAG", 0, 127, 50, PF_NUM, 0}};
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
static const rv_pinfo *const SRC_SYNTH[ENG_N] = {SRC_VA, SRC_CZ, SRC_FM, SRC_PHYS, SRC_FORM, SRC_PART, SRC_CHIP, SRC_WAVE, SRC_LOGIC, SRC_ORGAN, SRC_MONO, SRC_RESIN, SRC_TINE};

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
    [TP_EFILT] = {"ENV>CUT", -64, 63, 40, PF_BI, 0},
    [TP_EDST1] = {"DEST 1", 0, TP_N - 1, TP_S0, PF_DEST, 0},
    [TP_EAMT1] = {"AMOUNT 1", -64, 63, 0, PF_BI, 0},
    [TP_EDST2] = {"DEST 2", 0, TP_N - 1, TP_DRIVE, PF_DEST, 0},
    [TP_EAMT2] = {"AMOUNT 2", -64, 63, 0, PF_BI, 0},
    [TP_FTYPE] = {"FILTER", 0, FT_N - 1, FT_LADDER, E(ftype_names)},
    [TP_CUT] = {"CUTOFF", 0, 127, 90, PF_HZ, 0},
    [TP_RES] = {"RESO", 0, 127, 20, PF_NUM, 0},
    [TP_KTRK] = {"VCF KEY", 0, 127, 64, PF_NUM, 0},   /* the filter follows the note (on the PITCH page) */
    [TP_DRIVE] = {"DRIVE", 0, 127, 0, PF_NUM, 0},   /* (was 10: it saturated the voices' sum, so chords broke up) */
    [TP_FOLD] = {"FOLD", 0, 127, 0, PF_NUM, 0},
    [TP_BITS] = {"BITS", 0, 127, 0, PF_NUM, 0},
    [TP_DEREZ] = {"DEREZ", 0, 127, 0, PF_NUM, 0},
    [TP_MASK] = {"BITMASK", 0, 127, 0, PF_NUM, 0},
    [TP_INS] = {"INSERT", 0, INS_N - 1, INS_OFF, E(ins_names)},
    [TP_INA] = {"TUNE", 0, 127, 64, PF_NUM, 0},
    [TP_INB] = {"AMOUNT", 0, 127, 64, PF_NUM, 0},
    [TP_INMIX] = {"MIX", 0, 127, 64, PF_NUM, 0},
    [TP_LEVEL] = {"LEVEL", 0, 127, 100, PF_NUM, 0},
    [TP_PAN] = {"PAN", -64, 63, 0, PF_BI, 0},
    [TP_FBS] = {"FEEDBK", 0, 127, 0, PF_NUM, 0},
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
    /* the FILTER ENV: delay, attack, hold, decay, sustain, release (the volume's defaults) */
    [TP_FDLY] = {"F DELAY", 0, 127, 0, PF_TIME, 0},
    [TP_FATK] = {"F ATK", 0, 127, 4, PF_TIME, 0},
    [TP_FHOLD] = {"F HOLD", 0, 127, 0, PF_TIME, 0},
    [TP_FDEC] = {"F DEC", 0, 127, 70, PF_TIME, 0},
    [TP_FSUS] = {"F SUS", 0, 127, 90, PF_NUM, 0},
    [TP_FMODE] = {"LP-HP", 0, 127, 0, PF_NUM, 0},   /* 0 low pass, 64 notch, 127 high pass */
    [TP_FBP] = {"BP", 0, 1, 0, PF_ONOFF, 0},
    [TP_OSC1] = {"OSC LVL", 0, 127, 127, PF_NUM, 0},
    [TP_FREL] = {"F REL", 0, 127, 60, PF_TIME, 0},
    [TP_FACRV] = {"A BEND", -64, 63, 0, PF_BI, 0},         /* the attack: > 0 exponential (slow, then a rush), < 0 logarithmic */
    [TP_FDCRV] = {"D BEND", -64, 63, 0, PF_BI, 0},         /* decay and release: > 0 exponential (a fast fall, a long tail), < 0 logarithmic */
    [TP_FLOOP] = {"F LOOP", 0, 1, 0, PF_ONOFF, 0},         /* delay to decay, round again while held */
    [TP_FRATE] = {"F RATE", -64, 63, 0, PF_BI, 0},         /* every stage faster (+) or slower (-), x16 to /16 */
    /* STEP ENV: the LIBRARY envelope a step (or a note, DEFAULT) starts, into one parameter */
    [TP_SEDST] = {"SE DEST", 0, TP_N - 1, TP_CUT, PF_DEST, 0},
    [TP_FDST] = {"F DEST", 0, TP_N - 1, TP_PAN, PF_DEST, 0},   /* the FILTER ENV, looping or not, into anything of the track's */
    [TP_FAMT] = {"F AMT", -64, 63, 0, PF_BI, 0},
    [TP_GRAIN] = {"GRAIN", 0, 127, 0, PF_NUM, 0},     /* each hit a little different: pitch, length, how hard and bright */
    [TP_SEAMT] = {"SE DEPTH", -64, 63, 0, PF_BI, 0},
    [TP_SEDEF] = {"SE NOTE", 0, RV_SENV, 0, PF_ENUM, senv_names},  /* what every note starts when its step names none */
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
    [GP_LFO1RATE] = {"L1 RATE", 0, 127, 40, PF_NUM, 0},
    [GP_LFO1SHAPE] = {"L1 SHAPE", 0, N(lfo_names), 0, E(lfo_names)},
    [GP_LFO2RATE] = {"L2 RATE", 0, 127, 20, PF_NUM, 0},
    [GP_LFO2SHAPE] = {"L2 SHAPE", 0, N(lfo_names), 1, E(lfo_names)},
    [GP_DUSTRATE] = {"RATE", 0, 127, 40, PF_NUM, 0},
    [GP_DUSTDECAY] = {"DECAY", 0, 127, 40, PF_NUM, 0},
    [GP_FBTIME] = {"TIME", 0, 127, 50, PF_NUM, 0},
    [GP_FBREGEN] = {"REGEN", 0, 127, 70, PF_NUM, 0},
    [GP_FBPROC] = {"PROCESS", 0, N(fbproc_names), 0, E(fbproc_names)},
    [GP_FBAMT] = {"AMOUNT", 0, 127, 50, PF_NUM, 0},
    [GP_FBTUNE] = {"TUNE", 0, 127, 64, PF_NUM, 0},
    [GP_FBEDGE] = {"EDGE", 0, 1, 1, E(onoff_names)},
    [GP_FBRET] = {"RETURN", 0, N(fbret_names), 0, E(fbret_names)},
    [GP_FBLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_FBCUT] = {"CUTOFF", 0, 127, 80, PF_HZ, 0},
    /* (format 20: the filter pair's two slots) the feedback loop's band: BASE a high pass over the lower half
     * of the range, WIDTH a low pass over the upper half; turned in, they meet near 600 Hz */
    [GP_PAIRF1] = {"BASE", 0, 63, 0, PF_HZ, 0},
    [GP_PAIRF2] = {"WIDTH", 64, 127, 127, PF_HZ, 0},
    [GP_DLYTIME] = {"TIME", 0, 127, 60, PF_NUM, 0},
    [GP_DLYFB] = {"FEEDBK", 0, 127, 50, PF_NUM, 0},
    [GP_DLYTONE] = {"TONE", 0, 127, 70, PF_NUM, 0},
    [GP_DLYLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_REVSIZE] = {"DECAY", 0, 127, 70, PF_NUM, 0},      /* (the space is 12-BIT, format 20) */
    [GP_REVDAMP] = {"DAMP", 0, 127, 50, PF_NUM, 0},
    [GP_REVLEVEL] = {"LEVEL", 0, 127, 90, PF_NUM, 0},
    [GP_MDRIVE] = {"DRIVE", 0, 127, 0, PF_NUM, 0},     /* (off until turned up: it was 20) */
    [GP_CUTSIZE] = {"CUT SIZE", 0, 127, 50, PF_NUM, 0},
    [GP_CUTREP] = {"REPEATS", 1, 16, 4, PF_NUM, 0},
    [GP_CUTMIX] = {"CUT MIX", 0, 127, 127, PF_NUM, 0},
    [GP_TAPEQUAL] = {0, 0, 2, 2, PF_NUM, 0},   /* (the tape: gone, its globals kept for old projects, hidden) */
#ifdef RV_NO_AUDIO_IN
    [GP_TAPESRC] = {0, 0, 10, 0, PF_NUM, 0},
#else
    [GP_TAPESRC] = {0, 0, 10, 0, PF_NUM, 0},
#endif
    [GP_TAPEMON] = {0, 0, 127, 0, PF_NUM, 0},
    [GP_RESBARS] = {0, 0, 4, 0, PF_NUM, 0},
    [GP_ERODE] = {0, 0, 127, 0, PF_NUM, 0},
    [GP_MACRO1] = {"MACRO 1", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO2] = {"MACRO 2", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO3] = {"MACRO 3", 0, 127, 0, PF_NUM, 0},
    [GP_MACRO4] = {"MACRO 4", 0, 127, 0, PF_NUM, 0},
    [GP_SONGMODE] = {"PLAYS", 0, 1, 0, E(songmode_names)},
    [GP_INPUTLEVEL] = {"IN GAIN", 0, 127, 90, PF_NUM, 0},
    [GP_DLYSPREAD] = {"SPREAD", 0, 127, 127, PF_NUM, 0},
    [GP_DLYWOB] = {"WOW", 0, 127, 0, PF_NUM, 0},              /* a slow, wandering drift of the tape's speed */
    [GP_DLYFREEZE] = {"FREEZE", 0, 1, 0, E(onoff_names)},
    [GP_REVMOD] = {"MOTION", 0, 127, 20, PF_NUM, 0},
    [GP_REVDIFF] = {"CRUNCH", 0, 127, 0, PF_NUM, 0},      /* 12 bits down to 8 */
    [GP_REVLOW] = {"LOW CUT", 0, 127, 0, PF_NUM, 0},
    [GP_DLYMODE] = {"GRAINS", 0, 127, 0, PF_NUM, 0},          /* (format 18; was MODE) how much of the delay is grains */
    [GP_GRDENS] = {"DENSITY", 0, 127, 60, PF_NUM, 0},
    [GP_GRSIZE] = {"SIZE", 0, 127, 60, PF_NUM, 0},
    [GP_GRPITCH] = {"PITCH", -24, 24, 0, PF_SEMI, 0},
    [GP_METRO] = {"METRO", 0, N(metro_names), 0, E(metro_names)},
    [GP_COUNTIN] = {"COUNT-IN", 0, N(countin_names), 1, E(countin_names)},
    [GP_LFO1SYM] = {"L1 SYM", -64, 63, 0, PF_BI, 0},     /* skews the wave: - leans early, + late */
    [GP_LFO2SYM] = {"L2 SYM", -64, 63, 0, PF_BI, 0},
    [GP_LFO1PHASE] = {"L1 PHASE", 0, 359, 0, PF_NUM, 0},  /* degrees: where the wave starts */
    [GP_LFO2PHASE] = {"L2 PHASE", 0, 359, 0, PF_NUM, 0},
    [GP_LFO1MODE] = {"L1 MODE", 0, 2, 0, E(lfo_modes)},
    [GP_LFO2MODE] = {"L2 MODE", 0, 2, 0, E(lfo_modes)},
    [GP_CUTSHAPE] = {"C SHAPE", 0, N(cutshape_names), 0, E(cutshape_names)},   /* the punch-ins' cuts: EVEN, BOUNCE, DRAG */
    [GP_BEDLEVEL] = {"LEVEL", 0, 127, 0, PF_NUM, 0},
    [GP_BEDDENS] = {"DENSITY", 0, 127, 50, PF_NUM, 0},
    [GP_BEDTONE] = {"TONE", 0, 127, 64, PF_NUM, 0},      /* wood (0) to metal (127) */
    [GP_BEDSHIM] = {"SHIMMER", 0, 127, 0, PF_NUM, 0},
    [GP_BEDDUCK] = {"DUCK", 0, 127, 0, PF_NUM, 0},
    [GP_BEDDECAY] = {"DECAY", 0, 127, 0, PF_NUM, 0},
    [GP_BEDSPACE] = {"SPACE", 0, 127, 0, PF_NUM, 0},    /* how much of it goes into the SPACE */
    [GP_SCNGLIDE] = {"GLIDE", 0, 8, 0, E(glide_names)},  /* scenes: JUMP, or glide over 1-8 bars */
    [GP_ARCSPAN] = {"SPAN", 0, 2, 0, E(arcspan_names)},  /* the ARCS: each point 1, 2 or 4 bars */
    [GP_DLYFLUT] = {"FLUTTER", 0, 127, 0, PF_NUM, 0},     /* a fast waver of the tape's speed */
    [GP_DLYSAT] = {"SATURAT", 0, 127, 30, PF_NUM, 0},     /* the tape driven: warmth, then grit */
    [GP_DLYAGE] = {"AGE", 0, 127, 0, PF_NUM, 0},          /* worn tape: darker repeats, hiss, dropouts */
    [GP_DLYDBL] = {"DOUBLE", 0, 127, 0, PF_NUM, 0},       /* double tracking: a second, wavering copy close behind */
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
        tab = kind == KIND_STRIKE ? SRC_STRIKE : SRC_SYNTH[rv.tp[t & 7][TP_ENGINE] % ENG_N];
        if (tab == SRC_RESIN && rv.tp[t & 7][TP_S0] == 1)
            tab = SRC_RESIN_HORN;                     /* the reed: its own names */
        if (tab == SRC_TINE && rv.tp[t & 7][TP_S0] >= 1)
            tab = rv.tp[t & 7][TP_S0] == 1 ? SRC_TINE_MAR : SRC_TINE_EP;
        return tab[s].name ? &tab[s] : &NONE;
    }
    if (p < 0 || p >= TP_N || !TRACK[p].name)
        return &NONE;
    if (p == TP_OSC1) {                               /* named as the engine names its first oscillator */
        static const rv_pinfo op1 = {"LEVEL1", 0, 127, 127, PF_NUM, 0}, l1 = {"LEVEL 1", 0, 127, 127, PF_NUM, 0};
        int e = rv.tp[t & 7][TP_ENGINE];
        return e == ENG_FM ? &op1 : e == ENG_WAVE ? &l1 : &TRACK[p];
    }
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
float rv_cut_hz(float v) { return rv_cut_hz_i(v); }

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
    const rv_pinfo *pi;
    buf[0] = 0;
    if (pid >= RV_NPID && pid < RV_NPID + RV_MATRIX) {   /* a matrix row's AMOUNT */
        i = put_str(buf, 0, max, "ROW ");
        i = put_int(buf, i, max, pid - RV_NPID + 1, 0);
        put_str(buf, i, max, " AMT");
        return buf;
    }
    pi = rv_pinfo_pid(pid);
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
    if (p == TP_GRAIN)                                                 /* hits: strike and perc tracks */
        return tp[TP_KIND] == KIND_STRIKE || tp[TP_KIND] == KIND_PERC;
    if (tp[TP_KIND] == KIND_PERC && tp[TP_ENGINE] == 1 && (p == TP_SEDST || p == TP_SEAMT || p == TP_SEDEF))
        return 0;                                                      /* a KIT's steps keep their lanes' accents where a step envelope would go */
    if (tp[TP_KIND] == KIND_PERC && tp[TP_ENGINE] == 1 && (p == TP_POLY || p == TP_SEQMODE || p == TP_DIR || p == TP_PITCH || p == TP_FINE))
        return 0;                                                      /* a KIT: a voice per lane, steps forward */
    if (p == TP_VELO && tp[TP_KIND] != KIND_SYNTH && tp[TP_KIND] != KIND_PERC)   /* a strike takes velocity as it comes */
        return 0;
    if (tp[TP_KIND] == KIND_PERC) {                                    /* the machines make their own envelopes */
        switch (p) {
        case TP_ATK: case TP_ACURVE: case TP_OVER: case TP_DEC: case TP_SUS: case TP_LOOP: case TP_REL: case TP_TAIL:
        case TP_GIVE:
            return 0;
        default: break;
        }
    }                                                      /* take velocity as it comes */
    if ((p == TP_FTYPE || p == TP_CUT || p == TP_RES || p == TP_KTRK || p == TP_EFILT || p == TP_FMODE || p == TP_FBP) && tp[TP_KIND] != KIND_SYNTH)
        return 0;                                                      /* only synth voices have the filter */
    if (p == TP_OSC1)                                                  /* the first oscillator's level, where there are several */
        return tp[TP_KIND] == KIND_SYNTH && (tp[TP_ENGINE] == ENG_VA || tp[TP_ENGINE] == ENG_FM || tp[TP_ENGINE] == ENG_WAVE || tp[TP_ENGINE] == ENG_CHIP);
    if (p == TP_FMODE)                                                 /* the SEM's sweep; its band pass replaces it */
        return tp[TP_FTYPE] == FT_SEM && !tp[TP_FBP];
    if (p == TP_FBP)
        return tp[TP_FTYPE] == FT_SEM;
    if (p == TP_FAMT)                                                  /* the FILTER ENV's own destination: any synth */
        return tp[TP_KIND] == KIND_SYNTH;
    if (p == TP_FDST)
        return tp[TP_KIND] == KIND_SYNTH && tp[TP_FAMT] != 0;
    if ((p >= TP_FDLY && p <= TP_FSUS) || (p >= TP_FREL && p <= TP_FRATE))   /* the FILTER ENV: a synth's filter, not the LPG, or its DEST */
        return tp[TP_KIND] == KIND_SYNTH && (tp[TP_FTYPE] != FT_LPG || tp[TP_FAMT] != 0);
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
    case TP_DEREZ: case TP_MASK: case TP_INA: case TP_INB: case TP_INMIX: case TP_LEVEL:
    case TP_PAN: case TP_FBS: case TP_DLYS: case TP_REVS: case TP_FMODE: case TP_OSC1: case TP_FATK: case TP_FDEC: case TP_FSUS: case TP_FREL: case TP_FRATE: case TP_EFILT: case TP_SEAMT: case TP_FAMT: case TP_ATK: case TP_DEC:
    case TP_SUS: case TP_REL: case TP_GIVE: case TP_OVER: case TP_TAIL: case TP_GLIDE:
        return 1;
    default:
        return p >= TP_S0 && p <= TP_S15;
    }
}

/* the globals the matrix and the macros can move (GLOBAL): the clock, the LFOs and dust, and each
 * bus's main settings; the rest (modes, rows, switches) are not destinations */
int rv_is_moddest_g(int g)
{
    switch (g) {
    case GP_TEMPO: case GP_SWING: case GP_CLKDEPTH: case GP_CLKBRATE: case GP_CLKTHR:
    case GP_LFO1RATE: case GP_LFO2RATE: case GP_DUSTRATE: case GP_DUSTDECAY:
    case GP_DLYTIME: case GP_DLYFB: case GP_DLYTONE: case GP_DLYLEVEL: case GP_DLYWOB: case GP_DLYMODE:
    case GP_GRDENS: case GP_GRSIZE: case GP_GRPITCH:
    case GP_REVSIZE: case GP_REVDAMP: case GP_REVLEVEL: case GP_REVMOD:
    case GP_FBTIME: case GP_FBREGEN: case GP_FBAMT: case GP_FBTUNE: case GP_FBCUT: case GP_FBLEVEL:
    case GP_PAIRF1: case GP_PAIRF2:                   /* (the feedback loop's BASE and WIDTH) */
    case GP_BEDLEVEL: case GP_BEDDENS: case GP_BEDDUCK: case GP_BEDSPACE:
    case GP_MDRIVE: case GP_CUTMIX:
        return 1;
    default:
        return 0;
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
    } else if (engine == ENG_TINE) {                 /* struck: open, held, the engine's own decay */
        tp[TP_POLY] = 3;
        tp[TP_FTYPE] = FT_LP;
        tp[TP_CUT] = 127;
        tp[TP_RES] = 0;
        tp[TP_EFILT] = 0;
        tp[TP_KTRK] = 0;
        tp[TP_ATK] = 0;
        tp[TP_SUS] = 127;
        tp[TP_REL] = 90;
        tp[TP_VELO] = 100;
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
        rv.kit_rate[t & 7][l] = 0;
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
    if (kind == KIND_SYNTH && (engine == ENG_ORGAN || engine == ENG_MONO || engine == ENG_TINE))
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
    }
}

/* (format 21) the SQ engine (was 7) and the SQ-80 filter (was 7) left: an engine or a filter number as
 * it was before, as it is now (the SQ as VA, the SQ-80 as LADDER; those after them down one) */
int rv_up21_engine(int e) { return e == 7 ? ENG_VA : e > 7 ? e - 1 : e; }
int rv_up21_ftype(int f) { return f == 7 ? FT_LADDER : f > 7 ? f - 1 : f; }

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
        rv_track_defaults(t, t < 4 ? KIND_SYNTH : t < 6 ? KIND_STRIKE : KIND_PERC, t < 4 ? t : t == 6 ? 1 : 0);   /* T7 a kit, T8 a drum */
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
    rv_senv_factory();
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

/* the FILTER ENV as the volume's envelope was, for what was made before it existed (projects before
 * format 12, sounds kept before it, the presets): the one envelope then drove the filter too */
void rv_fenv_from_env(int16_t *tp)
{
    tp[TP_FDLY] = 0;
    tp[TP_FATK] = tp[TP_ATK];
    tp[TP_FHOLD] = 0;
    tp[TP_FDEC] = tp[TP_DEC];
    tp[TP_FSUS] = tp[TP_SUS];
    tp[TP_FREL] = tp[TP_REL];
    tp[TP_FACRV] = 0;
    tp[TP_FDCRV] = 0;
    tp[TP_FLOOP] = 0;
    tp[TP_FRATE] = 0;
    tp[TP_FDST] = TP_PAN;
    tp[TP_FAMT] = 0;
}

/* the LIBRARY's factory envelopes: twelve, synced to the steps (times in eighths of a step), the
 * rest empty. Each: start level, then segments of level, time, bend; loop; next. */
void rv_senv_factory(void)
{
    static const struct { uint8_t start, n, loop, next; uint8_t s[8][2]; int8_t c[8]; } f[12] = {
        {0, 1, 0, 0, {{127, 32}}, {0}},                                              /* 1 RAMP UP: over four steps */
        {127, 1, 0, 0, {{0, 32}}, {0}},                                              /* 2 RAMP DOWN */
        {0, 2, 0, 0, {{127, 0}, {0, 4}}, {0, -4}},                                    /* 3 BLIP: up at once, down in half a step */
        {0, 2, 0, 0, {{127, 16}, {0, 16}}, {5, -5}},                                  /* 4 SWELL: in and out over four steps */
        {0, 4, 0, 0, {{127, 0}, {0, 4}, {100, 0}, {0, 4}}, {0, -4, 0, -4}},           /* 5 DOUBLE: two blips a step apart */
        {0, 8, 0, 0, {{32, 0}, {32, 8}, {64, 0}, {64, 8}, {96, 0}, {96, 8}, {127, 0}, {127, 8}}, {0}},   /* 6 STAIRS: a step a stair */
        {0, 2, 1, 0, {{127, 8}, {0, 8}}, {0}},                                        /* 7 TRIANGLE: a two-step cycle, looping */
        {0, 2, 0, 0, {{64, 16}, {127, 16}}, {5, -5}},                                 /* 8 S CURVE: slow, quick, slow */
        {127, 2, 0, 0, {{127, 8}, {0, 2}}, {0, -6}},                                  /* 9 DROP: holds a step, then falls */
        {0, 2, 1, 0, {{127, 2}, {40, 2}}, {0}},                                       /* 10 WOBBLE: a quarter-step flutter, looping */
        {0, 1, 0, 12, {{127, 64}}, {6}},                                              /* 11 BUILD: eight steps up, then 12 */
        {127, 1, 0, 0, {{0, 8}}, {-6}},                                               /* 12 FALL: what BUILD hands over to */
    };
    int i, k;
    __builtin_memset(rv.senv, 0, sizeof(rv.senv));
    for (i = 0; i < 12; i++) {
        rv_senv *e = &rv.senv[i];
        e->start = f[i].start;
        e->flags = f[i].n;
        e->loop = f[i].loop;
        e->next = f[i].next;
        for (k = 0; k < f[i].n; k++) {
            e->lvl[k] = f[i].s[k][0];
            e->tim[k] = f[i].s[k][1];
            senv_set_crv(e, k, f[i].c[k]);
        }
    }
}
