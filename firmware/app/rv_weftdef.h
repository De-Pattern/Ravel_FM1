/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: WEFT's blocks and sources, named; and its factory patches. Shared by the parameters
 * (rv_params.c: the settings' names), the engine (rv_weft.c) and the interface (rv_ui.c). */
#ifndef RV_WEFTDEF_H
#define RV_WEFTDEF_H

/* the blocks: what each one does to its inputs A and B */
enum {
    WO_NONE,
    WO_PHASE, WO_WAVE,                                  /* oscillators: a ramp, and what reads it */
    WO_SLOPE, WO_CYCLE, WO_SLEW,                        /* functions: rise and fall */
    WO_LP, WO_BP, WO_HP,                                /* filters */
    WO_ADD, WO_MULT, WO_MIX, WO_MIN, WO_MAX, WO_FOLD,   /* arithmetic */
    WO_COMP, WO_AND, WO_OR, WO_XOR, WO_FLIP,            /* logic */
    WO_DELAY, WO_BANDS, WO_HOLD,                        /* memory and spectrum */
    WO_N
};

/* the sources an input can take besides a block (1-8) */
enum { WS_NONE = 0, WS_PITCH = 9, WS_GATE, WS_TRIG, WS_VEL, WS_NOISE, WS_LFO1, WS_LFO2, WS_WHEEL, WS_ONE, WS_N };

typedef struct {
    const char *name, *p1, *p2, *a, *b;   /* (a, b: what its inputs do) */
    uint8_t d1, d2;                       /* the settings' defaults */
    uint8_t m1;                           /* p1 is a list of this many names (wf_ratio), else 0..127 */
} wf_type_t;

static const char *const wf_ratio[16] = {"1/4", "1/2", "1", "3/2", "2", "5/2", "3", "4", "5", "6", "7", "8", "9", "11", "13", "16"};
static const float wf_ratio_v[16] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 11.0f, 13.0f, 16.0f};

static const wf_type_t wf_types[WO_N] = {
    {"-", "-", "-", "-", "-", 0, 0, 0},
    {"PHASE", "RATIO", "FINE", "FM", "SYNC", 2, 64, 16},
    {"WAVE", "SHAPE", "WARP", "PHASE", "PM", 0, 64, 0},
    {"SLOPE", "RISE", "FALL", "GATE", "TIME", 4, 60, 0},
    {"CYCLE", "RATE", "SKEW", "RATE", "SYNC", 40, 64, 0},
    {"SLEW", "RISE", "FALL", "IN", "TIME", 30, 30, 0},
    {"LP", "CUTOFF", "RES", "IN", "CUTOFF", 80, 30, 0},
    {"BP", "CUTOFF", "RES", "IN", "CUTOFF", 70, 60, 0},
    {"HP", "CUTOFF", "RES", "IN", "CUTOFF", 50, 30, 0},
    {"ADD", "OFFSET", "GAIN", "A", "B", 64, 64, 0},
    {"MULT", "OFFSET", "GAIN", "A", "B", 64, 64, 0},
    {"MIX", "BLEND", "GAIN", "A", "B", 64, 64, 0},
    {"MIN", "OFFSET", "GAIN", "A", "B", 64, 64, 0},
    {"MAX", "OFFSET", "GAIN", "A", "B", 64, 64, 0},
    {"FOLD", "DRIVE", "BIAS", "IN", "DRIVE", 30, 64, 0},
    {"COMP", "LEVEL", "THRESH", "A", "B", 100, 64, 0},
    {"AND", "LEVEL", "THRESH", "A", "B", 100, 64, 0},
    {"OR", "LEVEL", "THRESH", "A", "B", 100, 64, 0},
    {"XOR", "LEVEL", "THRESH", "A", "B", 100, 64, 0},
    {"FLIP", "LEVEL", "THRESH", "CLOCK", "RESET", 100, 64, 0},
    {"DELAY", "TIME", "FEEDBK", "IN", "TIME", 0, 96, 0},
    {"BANDS", "SPREAD", "RES", "IN", "SCAN", 64, 80, 0},
    {"HOLD", "STEPS", "GLIDE", "IN", "CLOCK", 0, 0, 0},
};

/* what each block does, in a line (the screen's own words) */
static const char *const wf_desc[WO_N] = {
    "AN EMPTY BLOCK",
    "A RAMP AT THE NOTE x RATIO",
    "READS A PHASE AS A WAVE",
    "RISES WHILE A IS UP, FALLS",
    "RISES AND FALLS ON ITS OWN",
    "FOLLOWS A, SLOWLY",
    "LOW PASS: A'S HIGHS TAKEN OUT",
    "BAND PASS: A AROUND CUTOFF",
    "HIGH PASS: A'S LOWS TAKEN OUT",
    "A + B + OFFSET",
    "A x (B + OFFSET): VCA, RING",
    "A INTO B BY BLEND",
    "THE LOWER OF A AND B",
    "THE HIGHER OF A AND B",
    "A FOLDED BACK AS IT GROWS",
    "HIGH WHEN A IS ABOVE B",
    "HIGH WHEN A AND B ARE",
    "HIGH WHEN A OR B IS",
    "HIGH WHEN JUST ONE IS",
    "TURNS OVER ON EACH RISE OF A",
    "A, LATER (TIME 0: THE NOTE)",
    "4 BANDS ON THE HARMONICS",
    "TAKES A ON EACH RISE OF B",
};

static const char *const wf_src_names[WS_N] = {"-", "", "", "", "", "", "", "", "", "PITCH", "GATE", "TRIG", "VEL", "NOISE", "LFO 1", "LFO 2", "WHEEL", "ONE"};

/* the factory patches (PRESETS on the WEFT pages): blocks as {type, a, amt a, b, amt b, p1, p2} */
typedef struct { uint8_t t, a; int8_t aa; uint8_t b; int8_t ab; uint8_t p1, p2; } wf_blk_t;
typedef struct {
    const char *name;
    wf_blk_t blk[WF_BLOCKS];
    uint8_t out, poly, map[4];            /* map: 1 + the setting (block i: 2i, 2i+1) */
} wf_preset_t;

#define WM(b, s) (uint8_t)(1 + ((b) - 1) * 2 + (s))   /* block b (1-8), setting s (0, 1) */
static const wf_preset_t wf_presets[] = {
    {"INIT",          /* a saw through a low pass, an envelope on both */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_SLOPE, 0, 0, 0, 0, 4, 70}, {WO_LP, 1, 100, 2, 60, 50, 30},
      {WO_MULT, 3, 100, 2, 100, 64, 64}},
     4, 1, {WM(3, 0), WM(3, 1), WM(2, 1), WM(1, 0)}},
    {"SYNC LEAD",     /* a ramp resets a faster one; a slow cycle bends it */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 7, 12, 1, 100, 3, 70}, {WO_WAVE, 2, 100, 0, 0, 85, 64},
      {WO_SLOPE, 0, 0, 0, 0, 2, 64}, {WO_LP, 3, 100, 4, 55, 62, 70}, {WO_MULT, 5, 100, 4, 100, 64, 64},
      {WO_CYCLE, 0, 0, 0, 0, 22, 64}},
     6, 1, {WM(5, 0), WM(5, 1), WM(2, 0), WM(4, 1)}},
    {"DX BELL",       /* two-operator FM: 7:1, its index falling away */
     {{WO_PHASE, 0, 0, 0, 0, 10, 64}, {WO_WAVE, 1, 100, 0, 0, 0, 64}, {WO_SLOPE, 0, 0, 0, 0, 0, 52},
      {WO_MULT, 2, 100, 3, 100, 64, 64}, {WO_PHASE, 4, 45, 0, 0, 2, 64}, {WO_WAVE, 5, 100, 0, 0, 0, 64},
      {WO_SLOPE, 0, 0, 0, 0, 0, 92}, {WO_MULT, 6, 100, 7, 100, 64, 64}},
     8, 2, {WM(5, 0), WM(1, 0), WM(3, 1), WM(7, 1)}},
    {"PLUCK",         /* Karplus-Strong: a burst of noise into a delay tuned to the note, a low pass in its loop */
     {{WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 34}, {WO_MULT, WS_NOISE, 100, 1, 100, 64, 127}, {WO_ADD, 2, 100, 5, 100, 64, 63},
      {WO_DELAY, 3, 100, 0, 0, 0, 64}, {WO_LP, 4, 100, 0, 0, 112, 0}, {WO_SLOPE, 0, 0, 0, 0, 0, 104},
      {WO_MULT, 4, 100, 6, 100, 64, 127}},
     7, 2, {WM(5, 0), WM(3, 1), WM(1, 1), WM(6, 1)}},
    {"CMOS XOR",      /* two square-ish ramps XORed, a sub-octave from FLIP, scanned through bands */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 0, 0, 0, 0, 3, 71}, {WO_XOR, 1, 100, 2, 100, 100, 64},
      {WO_FLIP, 3, 100, 0, 0, 100, 64}, {WO_MIX, 3, 100, 4, 100, 50, 64}, {WO_BANDS, 5, 100, 7, 90, 64, 96},
      {WO_CYCLE, 0, 0, 0, 0, 30, 64}, {WO_MULT, 6, 100, WS_GATE, 100, 64, 80}},
     8, 1, {WM(2, 1), WM(6, 0), WM(6, 1), WM(7, 0)}},
    {"PHASE DIST",    /* a sine bent by an envelope, the way a Casio CZ does */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_SLOPE, 0, 0, 0, 0, 2, 58}, {WO_WAVE, 1, 100, 2, 45, 0, 30},
      {WO_SLOPE, 0, 0, 0, 0, 3, 76}, {WO_MULT, 3, 100, 4, 100, 64, 64}},
     5, 2, {WM(3, 1), WM(3, 0), WM(2, 1), WM(4, 1)}},
    {"DRONE",         /* two saws a fifth and a cent apart, a band pass swept slowly */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 0, 0, 0, 0, 3, 66}, {WO_WAVE, 1, 100, 0, 0, 85, 64},
      {WO_WAVE, 2, 100, 0, 0, 85, 64}, {WO_ADD, 3, 100, 4, 100, 64, 44}, {WO_CYCLE, 0, 0, 0, 0, 18, 64},
      {WO_BP, 5, 100, 6, 70, 70, 96}, {WO_SLEW, WS_GATE, 100, 0, 0, 60, 80}},
     7, 1, {WM(7, 0), WM(7, 1), WM(6, 0), WM(2, 1)}},
    {"TANGLE",        /* a triangle phase-modulated by its own folded, filtered self */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_WAVE, 1, 100, 6, 60, 40, 64}, {WO_FOLD, 2, 100, 5, 50, 70, 64},
      {WO_BP, 3, 100, 5, 40, 72, 80}, {WO_CYCLE, 0, 0, 0, 0, 34, 30}, {WO_SLEW, 4, 100, 0, 0, 8, 8},
      {WO_SLOPE, 0, 0, 0, 0, 3, 70}, {WO_MULT, 4, 100, 7, 100, 64, 64}},
     8, 1, {WM(3, 0), WM(2, 0), WM(4, 0), WM(5, 0)}},
    {"SPECTRA",       /* noise through four harmonic bands, a slow scan across them */
     {{WO_BANDS, WS_NOISE, 100, 2, 100, 64, 120}, {WO_CYCLE, 0, 0, 0, 0, 25, 64}, {WO_SLOPE, 0, 0, 0, 0, 30, 90},
      {WO_MULT, 1, 100, 3, 100, 64, 120}},
     4, 2, {WM(1, 0), WM(1, 1), WM(2, 0), WM(3, 0)}},
    {"S&H BLIPS",     /* a clock samples noise into the pitch; each tick strikes an envelope */
     {{WO_CYCLE, 0, 0, 0, 0, 58, 10}, {WO_HOLD, WS_NOISE, 100, 1, 100, 24, 0}, {WO_PHASE, 2, 30, 0, 0, 4, 64},
      {WO_WAVE, 3, 100, 0, 0, 20, 64}, {WO_SLOPE, 1, 100, 0, 0, 0, 30}, {WO_MULT, 4, 100, 5, 100, 64, 64},
      {WO_MULT, 6, 100, WS_GATE, 100, 64, 64}},
     7, 1, {WM(1, 0), WM(2, 0), WM(5, 1), WM(4, 0)}},
};
#define WF_NPRESETS ((int)(sizeof wf_presets / sizeof wf_presets[0]))
#endif
