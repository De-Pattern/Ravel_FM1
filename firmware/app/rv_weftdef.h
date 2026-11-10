/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: WEFT's blocks and sources, named; and its factory patches. Shared by the parameters
 * (rv_params.c: the settings' names), the engine (rv_weft.c) and the interface (rv_ui.c). */
#ifndef RV_WEFTDEF_H
#define RV_WEFTDEF_H

/* the blocks: what each one does to its inputs A and B (stored in patches: new ones only at the end) */
enum {
    WO_NONE,
    WO_PHASE, WO_WAVE,                                  /* oscillators: a ramp, and what reads it */
    WO_SLOPE, WO_CYCLE, WO_SLEW,                        /* functions: rise and fall */
    WO_LP, WO_BP, WO_HP,                                /* filters */
    WO_ADD, WO_MULT, WO_MIX, WO_MIN, WO_MAX, WO_FOLD,   /* arithmetic */
    WO_COMP, WO_AND, WO_OR, WO_XOR, WO_FLIP,            /* logic */
    WO_DELAY, WO_BANDS, WO_HOLD,                        /* memory and spectrum */
    /* (format 25) */
    WO_DUST, WO_TICK,                                   /* particles and clocks */
    WO_RING, WO_SKIN, WO_BAR, WO_PLATE, WO_BELL, WO_HARMS, WO_FIXED,   /* struck resonators: one, and six modes of a body */
    WO_SAT, WO_ABS, WO_DIFFUSE,                         /* for loops: a soft clip, a rectifier, a smear */
    WO_OCLK, WO_OMOD,                                   /* outputs: a clock for the sequencers, a source for the matrix */
    WO_SW14, WO_SW41,                                   /* sequential switches: one in to four outs (SW 1-4), four in to one out */
    WO_STRING, WO_CHAOS, WO_CRUSH, WO_EDGE,             /* a plucked string; a logistic map; fewer bits; a click at each rise */
    WO_FOLLOW, WO_SHAPE, WO_CHEBY, WO_FORMANT,          /* an envelope follower; waveshapers; a steep low pass; vowels */
    WO_VOCODER, WO_RMOD, WO_NOISE, WO_RUNGLER, WO_LISSA,   /* a vocoder; a ring modulator; coloured noise; a shift register; a 2-D LFO */
    WO_AOUT, WO_SEQ,                                    /* outputs: into the track's sound (mixed); a track's sequencer */
    WO_SHIFT, WO_DIODE,                                 /* a frequency shifter; a diode clipper */
    WO_CDIV, WO_LPG,                                    /* a clock divider (and multiplier); a low pass gate */
    WO_RIFT, WO_DRIP, WO_CATCH, WO_BEND, WO_FLEX,       /* cycles torn out; a bouncing drop; a cycle caught; time bent; a
                                                         * curve that settles */
    WO_LAMINA, WO_FASCIA, WO_VISCERA, WO_SNAP, WO_TREAD,   /* layers apart; one sound's skin on another; a body that gives;
                                                         * a gesture kept; another track read */
    WO_TRKIN,                                           /* another track's sound, into the patch (send only: rv_engine.c) */
    WO_IMPULSE, WO_CREASE, WO_LOCATE,                  /* a rise and fall on each cycle (after Mangrove); a cut at zero, a
                                                         * clipped integrator (after Cold Mac) */
    WO_N
};
#define WO_MODES(t) ((t) >= WO_SKIN && (t) <= WO_FIXED)
#define WO_ISOUT(t) ((t) == WO_OCLK || (t) == WO_OMOD || (t) == WO_AOUT || (t) == WO_SEQ)   /* (not heard as OUT AUTO's last block) */
#define WF_OUT_AUTO (WF_BLOCKS + 1)                     /* OUT: the last block that isn't an output (or the AUDIO OUTs alone) */
#define WO_SLOT(t) (WO_MODES(t) || (t) == WO_CHEBY || (t) == WO_FORMANT || (t) == WO_VOCODER)   /* (blocks with a bank of filters: three to a patch) */

/* the sources an input can take besides a block (1-12) */
enum { WS_NONE = 0, WS_PITCH = WF_BLOCKS + 1, WS_GATE, WS_TRIG, WS_VEL, WS_NOISE, WS_LFO1, WS_LFO2, WS_WHEEL, WS_ONE,
       WS_SW1, WS_SW2, WS_SW3, WS_SW4, WS_LISY, WS_TAP, WS_HELD, WS_TURN, WS_N };   /* LISY: a LISSA's second output; TAP HELD TURN:
                                                                                    * VIOLENCE, how hard the panel is being played */
#define WS_MENU WS_N                                    /* the sources offered */
/* (format 24's numbering: blocks 1-8, then PITCH at 9) */
#define WS_OLD_PITCH 9

typedef struct {
    const char *name, *p1, *p2, *a, *b;   /* (a, b: what its inputs do) */
    uint8_t d1, d2;                       /* the settings' defaults */
    uint8_t m1, m2;                       /* p1, p2: a list of this many names (n1, n2), else 0..127 */
    const char *const *n1, *const *n2;
} wf_type_t;

static const char *const wf_ratio[16] = {"1/4", "1/2", "1", "3/2", "2", "5/2", "3", "4", "5", "6", "7", "8", "9", "11", "13", "16"};
static const float wf_ratio_v[16] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 11.0f, 13.0f, 16.0f};
/* TICK's divisions, and their length in sixteenths */
static const char *const wf_div[12] = {"4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64"};
static const float wf_div_v[12] = {64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 8.0f / 3.0f, 2.0f, 4.0f / 3.0f, 1.0f, 2.0f / 3.0f, 0.5f, 0.25f};
static const char *const wf_cdiv[16] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const wf_range[2] = {"-1..1", "0..1"};
static const char *const wf_order[5] = {"UP", "DOWN", "PING", "RANDOM", "SELECT"};
static const char *const wf_curve[7] = {"TANH", "CLIP", "SINE", "CUBE", "CHEB3", "STEPS", "ASYM"};
static const char *const wf_rlen[6] = {"3", "4", "5", "6", "7", "8"};
static const char *const wf_trk[8] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const wf_smode[5] = {"STEP", "JUMP", "RESTART", "GATE", "PATTERN"};
enum { WSQ_STEP, WSQ_JUMP, WSQ_RESTART, WSQ_GATE, WSQ_PATTERN };
/* CLK DIV's ratios: below 0, that many pulses to each of A's (spread over the last gap between two) */
static const char *const wf_cdr[16] = {"x4", "x3", "x2", "1", "/2", "/3", "/4", "/5", "/6", "/7", "/8", "/12", "/16", "/24", "/32", "/64"};
static const int8_t wf_cdr_v[16] = {-4, -3, -2, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32, 64};
static const char *const wf_lpgm[3] = {"LPG", "VCA", "LP"};
static const char *const wf_skip[6] = {"ALL", "1 IN 2", "1 IN 3", "2 IN 3", "1 IN 4", "3 IN 4"};
static const uint8_t wf_skip_v[6][2] = {{1, 1}, {1, 2}, {1, 3}, {2, 3}, {1, 4}, {3, 4}};   /* cycles passed, of how many */
static const char *const wf_ncyc[3] = {"1", "2", "4"};
static const char *const wf_lorder[3] = {"LOW 1ST", "HIGH 1ST", "MID 1ST"};
static const char *const wf_snapl[5] = {"1/4", "1/2", "1 BAR", "2 BAR", "4 BAR"};
static const char *const wf_what[5] = {"LEVEL", "ENV", "PITCH", "VEL", "HIT"};
enum { WLPG_BOTH, WLPG_VCA, WLPG_LP };
static const char *const wf_lratio[9] = {"1:1", "1:2", "2:1", "2:3", "3:2", "3:4", "4:3", "3:5", "5:4"};
static const uint8_t wf_lratio_v[9][2] = {{1, 1}, {1, 2}, {2, 1}, {2, 3}, {3, 2}, {3, 4}, {4, 3}, {3, 5}, {5, 4}};   /* X turns, Y turns */
enum { WSW_UP, WSW_DOWN, WSW_PING, WSW_RANDOM, WSW_SELECT };

static const wf_type_t wf_types[WO_N] = {
    {"-", "-", "-", "-", "-", 0, 0, 0, 0, 0, 0},
    {"PHASE", "RATIO", "FINE", "FM", "SYNC", 2, 64, 16, 0, wf_ratio, 0},
    {"WAVE", "SHAPE", "WARP", "PHASE", "PM", 0, 64, 0, 0, 0, 0},
    {"SLOPE", "RISE", "FALL", "GATE", "TIME", 4, 60, 0, 0, 0, 0},
    {"CYCLE", "RATE", "SKEW", "RATE", "SYNC", 40, 64, 0, 0, 0, 0},
    {"SLEW", "RISE", "FALL", "IN", "TIME", 30, 30, 0, 0, 0, 0},
    {"LP", "CUTOFF", "RES", "IN", "CUTOFF", 80, 30, 0, 0, 0, 0},
    {"BP", "CUTOFF", "RES", "IN", "CUTOFF", 70, 60, 0, 0, 0, 0},
    {"HP", "CUTOFF", "RES", "IN", "CUTOFF", 50, 30, 0, 0, 0, 0},
    {"ADD", "OFFSET", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"MULT", "OFFSET", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"MIX", "BLEND", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"MIN", "OFFSET", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"MAX", "OFFSET", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"FOLD", "DRIVE", "BIAS", "IN", "DRIVE", 30, 64, 0, 0, 0, 0},
    {"COMP", "LEVEL", "THRESH", "A", "B", 100, 64, 0, 0, 0, 0},
    {"AND", "LEVEL", "THRESH", "A", "B", 100, 64, 0, 0, 0, 0},
    {"OR", "LEVEL", "THRESH", "A", "B", 100, 64, 0, 0, 0, 0},
    {"XOR", "LEVEL", "THRESH", "A", "B", 100, 64, 0, 0, 0, 0},
    {"FLIP", "LEVEL", "THRESH", "CLOCK", "RESET", 100, 64, 0, 0, 0, 0},
    {"DELAY", "TIME", "FEEDBK", "IN", "TIME", 0, 96, 0, 0, 0, 0},
    {"BANDS", "SPREAD", "RES", "IN", "SCAN", 64, 80, 0, 0, 0, 0},
    {"HOLD", "STEPS", "GLIDE", "IN", "CLOCK", 0, 0, 0, 0, 0, 0},
    {"DUST", "DENSITY", "CLUSTER", "DENSITY", "BURST", 60, 40, 0, 0, 0, 0},
    {"TICK", "DIV", "CHANCE", "GATE", "JITTER", 8, 127, 12, 0, wf_div, 0},
    {"RING", "RATIO", "DECAY", "STRIKE", "PITCH", 2, 60, 16, 0, wf_ratio, 0},
    {"SKIN", "DECAY", "BRIGHT", "STRIKE", "PITCH", 70, 64, 0, 0, 0, 0},
    {"BAR", "DECAY", "BRIGHT", "STRIKE", "PITCH", 70, 64, 0, 0, 0, 0},
    {"PLATE", "DECAY", "BRIGHT", "STRIKE", "PITCH", 70, 64, 0, 0, 0, 0},
    {"BELL", "DECAY", "BRIGHT", "STRIKE", "PITCH", 80, 64, 0, 0, 0, 0},
    {"HARMS", "DECAY", "BRIGHT", "STRIKE", "PITCH", 70, 64, 0, 0, 0, 0},
    {"FIXED", "DECAY", "BRIGHT", "STRIKE", "PITCH", 70, 64, 0, 0, 0, 0},
    {"SAT", "DRIVE", "BIAS", "IN", "DRIVE", 40, 64, 0, 0, 0, 0},
    {"ABS", "OFFSET", "GAIN", "A", "B", 64, 64, 0, 0, 0, 0},
    {"DIFFUSE", "SIZE", "AMOUNT", "IN", "SIZE", 64, 80, 0, 0, 0, 0},
    {"CLK OUT", "DIV", "THRESH", "CLOCK", "RESET", 0, 64, 16, 0, wf_cdiv, 0},
    {"MOD OUT", "SMOOTH", "RANGE", "IN", "LEVEL", 10, 0, 0, 2, 0, wf_range},
    {"SW 1>4", "ORDER", "SMOOTH", "IN", "STEP", 0, 0, 5, 0, wf_order, 0},
    {"SW 4>1", "ORDER", "SMOOTH", "FROM", "STEP", 0, 0, 5, 0, wf_order, 0},
    {"STRING", "DAMP", "DECAY", "EXCITE", "PITCH", 40, 90, 0, 0, 0, 0},
    {"CHAOS", "CHAOS", "RATE", "CLOCK", "CHAOS", 110, 60, 0, 0, 0, 0},
    {"CRUSH", "BITS", "RATE", "IN", "RATE", 80, 30, 0, 0, 0, 0},
    {"EDGE", "THRESH", "LEVEL", "IN", "THRESH", 64, 127, 0, 0, 0, 0},
    {"FOLLOW", "ATTACK", "RELEASE", "IN", "TIME", 10, 50, 0, 0, 0, 0},
    {"SHAPE", "CURVE", "DRIVE", "IN", "DRIVE", 0, 30, 7, 0, wf_curve, 0},
    {"CHEBY", "CUTOFF", "RIPPLE", "IN", "CUTOFF", 80, 40, 0, 0, 0, 0},
    {"FORMANT", "VOWEL", "SHIFT", "IN", "VOWEL", 0, 64, 0, 0, 0, 0},
    {"VOCODER", "SHARP", "SHIFT", "MOD", "CARRIER", 64, 64, 0, 0, 0, 0},
    {"RINGMOD", "FREQ", "MIX", "IN", "CARRIER", 64, 127, 0, 0, 0, 0},
    {"NOISE", "COLOR", "RATE", "RATE", "COLOR", 64, 127, 0, 0, 0, 0},
    {"RUNGLER", "LOOP", "LENGTH", "CLOCK", "DATA", 64, 5, 0, 6, 0, wf_rlen},
    {"LISSA", "RATE", "RATIO", "RATE", "PHASE", 30, 3, 0, 9, 0, wf_lratio},
    {"AUDIO", "LEVEL", "TONE", "IN", "LEVEL", 100, 64, 0, 0, 0, 0},
    {"SEQ OUT", "TRACK", "MODE", "TRIG", "WHERE", 1, 0, 8, 5, wf_trk, wf_smode},
    {"SHIFT", "SHIFT", "SIDE", "IN", "SHIFT", 70, 127, 0, 0, 0, 0},
    {"DIODE", "DRIVE", "PAIR", "IN", "DRIVE", 40, 0, 0, 0, 0, 0},
    {"CLK DIV", "RATIO", "WIDTH", "CLOCK", "RESET", 4, 64, 16, 0, wf_cdr, 0},
    {"LPG", "DECAY", "MODE", "IN", "OPEN", 80, 0, 0, 3, 0, wf_lpgm},
    {"RIFT", "SKIP", "FADE", "IN", "OPEN", 0, 10, 6, 0, wf_skip, 0},
    {"DRIP", "TIME", "BOUNCE", "TRIG", "SPEED", 70, 90, 0, 0, 0, 0},
    {"CATCH", "RATIO", "CYCLES", "IN", "GRAB", 2, 0, 16, 3, wf_ratio, wf_ncyc},
    {"BEND", "DEPTH", "WOW", "IN", "WARP", 40, 0, 0, 0, 0, 0},
    {"FLEXURE", "DEPTH", "SETTLE", "IN", "CURVE", 80, 60, 0, 0, 0, 0},
    {"LAMINA", "SPREAD", "ORDER", "IN", "SPREAD", 60, 0, 0, 3, 0, wf_lorder},
    {"FASCIA", "SPEED", "TONE", "IN", "SKIN", 30, 100, 0, 0, 0, 0},
    {"VISCERA", "SIZE", "FLESH", "STRIKE", "PITCH", 64, 80, 0, 0, 0, 0},
    {"SNAP", "LENGTH", "SMOOTH", "IN", "REC", 2, 10, 5, 0, wf_snapl, 0},
    {"TRACK", "TRACK", "WHAT", "SCALE", "ADD", 0, 0, 8, 5, wf_trk, wf_what},
    {"TRK IN", "TRACK", "LEVEL", "LEVEL", "ADD", 0, 100, 8, 0, wf_trk, 0},
    {"IMPULSE", "FORMANT", "BARREL", "SYNC", "FORMANT", 50, 64, 0, 0, 0, 0},
    {"CREASE", "SHIFT", "BIAS", "IN", "ADD", 64, 64, 0, 0, 0, 0},
    {"LOCATE", "RATE", "LEAK", "IN", "RESET", 60, 0, 0, 0, 0, 0},
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
    "RANDOM PARTICLES, IN CLUMPS",
    "A CLICK ON THE TEMPO'S GRID",
    "RINGS WHEN STRUCK (A)",
    "6 MODES OF A DRUM SKIN",
    "6 MODES OF A STRUCK BAR",
    "6 MODES OF A METAL PLATE",
    "6 MODES OF A BELL",
    "6 HARMONICS, STRUCK",
    "6 FIXED BANDS, STRUCK",
    "A, SOFTLY CLIPPED",
    "A + B + OFFSET, MADE POSITIVE",
    "A SMEARED BY FOUR ALLPASSES",
    "A'S RISES: THE WEFT CLOCK",
    "A: THE MATRIX'S WEFT SOURCE",
    "A TO ONE OF SW 1-4 IN TURN",
    "ONE OF 4 BLOCKS IN A ROW",
    "A PLUCKED STRING AT THE NOTE",
    "A LOGISTIC MAP: ORDER TO NOISE",
    "A IN FEWER BITS, HELD LONGER",
    "A CLICK AT EACH RISE OF A",
    "HOW LOUD A IS: 0 .. 1",
    "A THROUGH A TRANSFER CURVE",
    "A STEEP LOW PASS WITH RIPPLES",
    "A THROUGH A VOWEL'S FORMANTS",
    "B SPEAKS WITH A'S SPECTRUM",
    "A x B (NONE: A SINE x FREQ)",
    "NOISE, DARK TO BRIGHT, HELD",
    "A SHIFT REGISTER'S 3 BITS",
    "X; ITS Y IS THE SOURCE LIS Y",
    "A INTO THE TRACK, MIXED",
    "A'S RISES MOVE A TRACK'S STEPS",
    "EVERY PARTIAL MOVED BY SHIFT HZ",
    "A THROUGH A DIODE, OR A PAIR",
    "A'S PULSES DIVIDED OR MULTIPLIED",
    "A LOW PASS GATE: B OPENS IT",
    "A'S CYCLES PASSED OR TORN OUT",
    "DROPS THAT BOUNCE, FASTER",
    "ONE CYCLE OF A CAUGHT, LOOPED",
    "A, ITS TIME BENT BY B",
    "A SHAPED, SETTLING AS IT GOES",
    "A'S LOWS, MIDS, HIGHS APART",
    "A WEARS B'S SKIN",
    "A BODY THAT GIVES WHEN HIT",
    "A GESTURE KEPT AND LOOPED",
    "ANOTHER TRACK, READ",
    "ANOTHER TRACK'S SOUND IN",
    "A RISE AND FALL EACH CYCLE",
    "A CUT AT ZERO, HALVES SWAPPED",
    "A ADDED UP, HELD AT ITS EDGES"
};

static const char *const wf_src_names[WS_N] = {"-", "", "", "", "", "", "", "", "", "", "", "", "",
                                               "PITCH", "GATE", "TRIG", "VEL", "NOISE", "LFO 1", "LFO 2", "WHEEL", "ONE",
                                               "SW 1", "SW 2", "SW 3", "SW 4", "LIS Y", "V TAP", "V HELD", "V TURN"};

/* the factory patches (PRESETS on the WEFT pages): blocks as {type, a, amt a, b, amt b, p1, p2} */
typedef struct { uint8_t t, a; int8_t aa; uint8_t b; int8_t ab; uint8_t p1, p2; } wf_blk_t;
typedef struct {
    const char *name;
    wf_blk_t blk[WF_BLOCKS];
    uint8_t out, poly, map[4];            /* map: 1 + the setting (block i: 2i, 2i+1) */
    uint16_t bmove;                       /* the blocks whose B moves their second setting */
} wf_preset_t;

#define WM(b, s) (uint8_t)(1 + ((b) - 1) * 2 + (s))   /* block b (1-12), setting s (0, 1) */
static const wf_preset_t wf_presets[] = {
    {"INIT",          /* a saw through a low pass, an envelope on both */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_SLOPE, 0, 0, 0, 0, 4, 70}, {WO_LP, 1, 100, 2, 60, 50, 30},
      {WO_MULT, 3, 100, 2, 100, 64, 64}},
     4, 1, {WM(3, 0), WM(3, 1), WM(2, 1), WM(1, 0)}, 0},
    {"SYNC LEAD",     /* a ramp resets a faster one; a slow cycle bends it */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 7, 12, 1, 100, 3, 70}, {WO_WAVE, 2, 100, 0, 0, 85, 64},
      {WO_SLOPE, 0, 0, 0, 0, 2, 64}, {WO_LP, 3, 100, 4, 55, 62, 70}, {WO_MULT, 5, 100, 4, 100, 64, 64},
      {WO_CYCLE, 0, 0, 0, 0, 22, 64}},
     6, 1, {WM(5, 0), WM(5, 1), WM(2, 0), WM(4, 1)}, 0},
    {"DX BELL",       /* two-operator FM: 7:1, its index falling away */
     {{WO_PHASE, 0, 0, 0, 0, 10, 64}, {WO_WAVE, 1, 100, 0, 0, 0, 64}, {WO_SLOPE, 0, 0, 0, 0, 0, 52},
      {WO_MULT, 2, 100, 3, 100, 64, 64}, {WO_PHASE, 4, 45, 0, 0, 2, 64}, {WO_WAVE, 5, 100, 0, 0, 0, 64},
      {WO_SLOPE, 0, 0, 0, 0, 0, 92}, {WO_MULT, 6, 100, 7, 100, 64, 64}},
     8, 2, {WM(5, 0), WM(1, 0), WM(3, 1), WM(7, 1)}, 0},
    {"PLUCK",         /* Karplus-Strong: a burst of noise into a delay tuned to the note, a low pass in its loop */
     {{WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 34}, {WO_MULT, WS_NOISE, 100, 1, 100, 64, 127}, {WO_ADD, 2, 100, 5, 100, 64, 63},
      {WO_DELAY, 3, 100, 0, 0, 0, 64}, {WO_LP, 4, 100, 0, 0, 112, 0}, {WO_SLOPE, 0, 0, 0, 0, 0, 104},
      {WO_MULT, 4, 100, 6, 100, 64, 127}},
     7, 2, {WM(5, 0), WM(3, 1), WM(1, 1), WM(6, 1)}, 0},
    {"CMOS XOR",      /* two square-ish ramps XORed, a sub-octave from FLIP, scanned through bands */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 0, 0, 0, 0, 3, 71}, {WO_XOR, 1, 100, 2, 100, 100, 64},
      {WO_FLIP, 3, 100, 0, 0, 100, 64}, {WO_MIX, 3, 100, 4, 100, 50, 64}, {WO_BANDS, 5, 100, 7, 90, 64, 96},
      {WO_CYCLE, 0, 0, 0, 0, 30, 64}, {WO_MULT, 6, 100, WS_GATE, 100, 64, 80}},
     8, 1, {WM(2, 1), WM(6, 0), WM(6, 1), WM(7, 0)}, 0},
    {"PHASE DIST",    /* a sine bent by an envelope, the way a Casio CZ does */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_SLOPE, 0, 0, 0, 0, 2, 58}, {WO_WAVE, 1, 100, 2, 45, 0, 30},
      {WO_SLOPE, 0, 0, 0, 0, 3, 76}, {WO_MULT, 3, 100, 4, 100, 64, 64}},
     5, 2, {WM(3, 1), WM(3, 0), WM(2, 1), WM(4, 1)}, 0},
    {"DRONE",         /* two saws a fifth and a cent apart, a band pass swept slowly */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_PHASE, 0, 0, 0, 0, 3, 66}, {WO_WAVE, 1, 100, 0, 0, 85, 64},
      {WO_WAVE, 2, 100, 0, 0, 85, 64}, {WO_ADD, 3, 100, 4, 100, 64, 44}, {WO_CYCLE, 0, 0, 0, 0, 18, 64},
      {WO_BP, 5, 100, 6, 70, 70, 96}, {WO_SLEW, WS_GATE, 100, 0, 0, 60, 80}},
     7, 1, {WM(7, 0), WM(7, 1), WM(6, 0), WM(2, 1)}, 0},
    {"TANGLE",        /* a triangle phase-modulated by its own folded, filtered self */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_WAVE, 1, 100, 6, 60, 40, 64}, {WO_FOLD, 2, 100, 5, 50, 70, 64},
      {WO_BP, 3, 100, 5, 40, 72, 80}, {WO_CYCLE, 0, 0, 0, 0, 34, 30}, {WO_SLEW, 4, 100, 0, 0, 8, 8},
      {WO_SLOPE, 0, 0, 0, 0, 3, 70}, {WO_MULT, 4, 100, 7, 100, 64, 64}},
     8, 1, {WM(3, 0), WM(2, 0), WM(4, 0), WM(5, 0)}, 0},
    {"SPECTRA",       /* noise through four harmonic bands, a slow scan across them */
     {{WO_BANDS, WS_NOISE, 100, 2, 100, 64, 120}, {WO_CYCLE, 0, 0, 0, 0, 25, 64}, {WO_SLOPE, 0, 0, 0, 0, 30, 90},
      {WO_MULT, 1, 100, 3, 100, 64, 120}},
     4, 2, {WM(1, 0), WM(1, 1), WM(2, 0), WM(3, 0)}, 0},
    {"S&H BLIPS",     /* a clock samples noise into the pitch; each tick strikes an envelope */
     {{WO_CYCLE, 0, 0, 0, 0, 58, 10}, {WO_HOLD, WS_NOISE, 100, 1, 100, 24, 0}, {WO_PHASE, 2, 30, 0, 0, 4, 64},
      {WO_WAVE, 3, 100, 0, 0, 20, 64}, {WO_SLOPE, 1, 100, 0, 0, 0, 30}, {WO_MULT, 4, 100, 5, 100, 64, 64},
      {WO_MULT, 6, 100, WS_GATE, 100, 64, 64}},
     7, 1, {WM(1, 0), WM(2, 0), WM(5, 1), WM(4, 0)}, 0},
    {"DUST SKIN",     /* particles in clumps strike a drum skin, each at its own pitch; a smear fed back through a soft clip */
     {{WO_DUST, 9, 40, WS_TRIG, 100, 58, 75}, {WO_HOLD, WS_NOISE, 100, 1, 100, 0, 0}, {WO_SKIN, 1, 100, 2, 25, 70, 70},
      {WO_ADD, 3, 100, 8, 45, 64, 64}, {WO_DIFFUSE, 4, 100, 0, 0, 60, 85}, {WO_MULT, 5, 100, 7, 40, 80, 64},
      {WO_CYCLE, 0, 0, 0, 0, 26, 64}, {WO_SAT, 6, 100, 0, 0, 10, 64}, {WO_CYCLE, 0, 0, 0, 0, 8, 64},
      {WO_LP, 5, 100, 0, 0, 100, 20}, {WO_SLEW, WS_GATE, 100, 0, 0, 10, 70}, {WO_MULT, 10, 100, 11, 100, 64, 90}},
     12, 1, {WM(1, 0), WM(1, 1), WM(3, 0), WM(5, 1)}, 0},
    {"PLONK",         /* a click of noise strikes a bar, its pitch falling a little as it rings */
     {{WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 22}, {WO_MULT, WS_NOISE, 100, 1, 100, 64, 90}, {WO_BAR, 2, 100, 1, 18, 62, 66},
      {WO_SAT, 3, 100, 0, 0, 20, 64}},
     4, 2, {WM(3, 0), WM(3, 1), WM(1, 1), WM(4, 0)}, 0},
    {"TOM SKIN",      /* a pulse strikes a skin; its B moves BRIGHT, by the velocity (B MOVES) */
     {{WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 12}, {WO_SKIN, 1, 100, WS_VEL, 80, 64, 30}, {WO_SAT, 2, 100, 0, 0, 25, 64}},
     3, 2, {WM(2, 0), WM(2, 1), WM(1, 1), WM(3, 0)}, 0x2},
    {"PINGS",         /* two clocks on the tempo's grid strike tuned rings; held notes let them through */
     {{WO_TICK, 0, 0, 0, 0, 8, 90}, {WO_RING, 1, 100, 0, 0, 11, 30}, {WO_TICK, 0, 0, WS_NOISE, 40, 7, 70},
      {WO_RING, 3, 100, 6, 20, 13, 40}, {WO_ADD, 2, 100, 4, 80, 64, 64}, {WO_CYCLE, 0, 0, 0, 0, 20, 64},
      {WO_SAT, 5, 100, 0, 0, 40, 64}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 50}, {WO_MULT, 7, 100, 8, 100, 64, 64}},
     9, 1, {WM(1, 1), WM(3, 1), WM(2, 1), WM(4, 0)}, 0},
    {"TICK CLOCK",    /* a ticking clock with chances: the WEFT clock (choose WEFT for CLOCK on a track's TRACK page) */
     {{WO_TICK, 0, 0, 0, 0, 8, 127}, {WO_OCLK, 1, 100, 0, 0, 0, 64}, {WO_RING, 1, 100, 0, 0, 13, 62},
      {WO_SLEW, WS_GATE, 100, 0, 0, 0, 40}, {WO_MULT, 3, 100, 4, 100, 64, 64}},
     5, 1, {WM(1, 0), WM(1, 1), WM(2, 0), WM(3, 1)}, 0},
    {"DUST MOD",      /* clumps of dust, smoothed: a wandering source for the matrix (WEFT) */
     {{WO_DUST, 0, 0, 0, 0, 40, 90}, {WO_HOLD, WS_NOISE, 100, 1, 100, 0, 50}, {WO_OMOD, 2, 100, 0, 0, 40, 0}},
     0, 1, {WM(1, 0), WM(1, 1), WM(2, 1), WM(3, 0)}, 0},
    {"SCATTER",       /* clumps of dust, each particle sent to one of four rings at random by a switch; crushed */
     {{WO_DUST, 0, 0, WS_TRIG, 100, 64, 60}, {WO_SW14, 1, 100, 1, 100, WSW_RANDOM, 0}, {WO_TICK, 0, 0, 0, 0, 8, 127},
      {WO_CHAOS, 3, 100, 0, 0, 115, 60}, {WO_RING, WS_SW1, 100, 4, 30, 4, 70}, {WO_RING, WS_SW2, 100, 0, 0, 6, 60},
      {WO_RING, WS_SW3, 100, 0, 0, 8, 50}, {WO_RING, WS_SW4, 100, 4, 30, 11, 40}, {WO_ADD, 5, 100, 6, 100, 64, 64},
      {WO_ADD, 7, 100, 8, 100, 64, 64}, {WO_MIX, 9, 100, 10, 100, 64, 64}, {WO_CRUSH, 11, 100, 0, 0, 40, 10}},
     12, 1, {WM(1, 0), WM(1, 1), WM(4, 0), WM(12, 0)}, 0},
    {"PING SWITCH",   /* a clock picks, tick by tick, between two rings, a string and its clipped self (SW 4>1 reads 2-5) */
     {{WO_TICK, 0, 0, 0, 0, 8, 127}, {WO_RING, 1, 100, 0, 0, 11, 25}, {WO_RING, 1, 100, 9, 20, 13, 35},
      {WO_STRING, 10, 100, 0, 0, 30, 80}, {WO_SAT, 4, 100, 0, 0, 50, 64}, {WO_SW41, 2, 100, 1, 100, WSW_RANDOM, 0},
      {WO_CRUSH, 6, 100, 0, 0, 60, 20}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 40}, {WO_CYCLE, 0, 0, 0, 0, 20, 64},
      {WO_TICK, 0, 0, 0, 0, 7, 110}, {WO_MULT, 7, 100, 8, 100, 64, 64}},
     11, 1, {WM(1, 0), WM(6, 0), WM(4, 1), WM(7, 0)}, 0},
    {"KS STRING",     /* Karplus-Strong in one block: a click of noise into STRING */
     {{WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 20}, {WO_MULT, WS_NOISE, 100, 1, 100, 64, 127}, {WO_STRING, 2, 100, 0, 0, 30, 110}},
     3, 2, {WM(3, 0), WM(3, 1), WM(1, 1), WM(2, 1)}, 0},
    {"CHAOS STRING",  /* a logistic map plucks a string each time it crosses zero, and sets its pitch */
     {{WO_CHAOS, 0, 0, 0, 0, 118, 62}, {WO_EDGE, 1, 100, 0, 0, 64, 110}, {WO_STRING, 2, 100, 4, 12, 15, 100},
      {WO_HOLD, 1, 100, 2, 100, 20, 0}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 60}, {WO_MULT, 3, 100, 5, 100, 64, 127}},
     6, 1, {WM(1, 0), WM(1, 1), WM(3, 0), WM(3, 1)}, 0},
    {"BENJOLIN",      /* after Rob Hordijk's: two free oscillators, a rungler clocked by one and fed by the other, bending both;
                       * their comparison through a resonant filter the rungler moves */
     {{WO_CYCLE, 3, 35, 0, 0, 96, 64}, {WO_CYCLE, 3, 25, 0, 0, 88, 64}, {WO_RUNGLER, 2, 100, 1, 100, 64, 5},
      {WO_COMP, 1, 100, 2, 100, 100, 64}, {WO_LP, 4, 100, 3, 40, 70, 100}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 40},
      {WO_MULT, 5, 100, 6, 100, 64, 64}},
     7, 1, {WM(1, 0), WM(2, 0), WM(3, 0), WM(5, 0)}, 0},
    {"VOWELS",        /* a saw through a vowel's formants, the vowel wandering with a slow LISSA */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_FORMANT, 1, 100, 3, 70, 0, 64}, {WO_LISSA, 0, 0, 0, 0, 25, 3},
      {WO_SLOPE, 0, 0, 0, 0, 10, 70}, {WO_MULT, 2, 100, 4, 100, 64, 90}},
     5, 2, {WM(2, 0), WM(2, 1), WM(3, 0), WM(4, 1)}, 0},
    {"ROBOT VOX",     /* a vowel-shaped saw speaks through a square an octave down (VOCODER) */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_LISSA, 0, 0, 0, 0, 20, 1}, {WO_FORMANT, 1, 100, 2, 90, 0, 64},
      {WO_PHASE, 0, 0, 0, 0, 1, 64}, {WO_WAVE, 4, 100, 0, 0, 127, 64}, {WO_VOCODER, 3, 100, 5, 100, 80, 64},
      {WO_SLEW, WS_GATE, 100, 0, 0, 0, 40}, {WO_MULT, 6, 100, 7, 100, 64, 100}},
     8, 1, {WM(6, 0), WM(6, 1), WM(2, 0), WM(3, 1)}, 0},
    {"RUNGLE PINGS",  /* sixteenths strike a ring whose pitch a rungler steps; a Chebyshev low pass after */
     {{WO_TICK, 0, 0, 0, 0, 8, 127}, {WO_RUNGLER, 1, 100, 0, 0, 100, 5}, {WO_RING, 1, 100, 2, 50, 4, 55},
      {WO_CHEBY, 3, 100, 2, 30, 95, 60}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 40}, {WO_MULT, 4, 100, 5, 100, 64, 100}},
     6, 1, {WM(2, 0), WM(3, 1), WM(4, 0), WM(4, 1)}, 0},
    {"LISSA DRONE",   /* two saws a fifth apart, a Chebyshev low pass and a ring modulator moved by a LISSA's X and Y */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_PHASE, 0, 0, 0, 0, 3, 64}, {WO_WAVE, 2, 100, 0, 0, 85, 64},
      {WO_LISSA, 0, 0, 0, 0, 18, 3}, {WO_MIX, 1, 100, 3, 100, 64, 64}, {WO_CHEBY, 5, 100, 4, 50, 70, 80},
      {WO_RMOD, 6, 100, WS_LISY, 60, 80, 30}, {WO_SLEW, WS_GATE, 100, 0, 0, 40, 80}, {WO_MULT, 7, 100, 8, 100, 64, 64}},
     9, 1, {WM(6, 0), WM(6, 1), WM(4, 0), WM(7, 0)}, 0x40},
    {"WIND",          /* dark noise through two band passes, X and Y of a LISSA sweeping them */
     {{WO_NOISE, 0, 0, 0, 0, 30, 127}, {WO_LISSA, 0, 0, 0, 0, 15, 5}, {WO_BP, 1, 100, 2, 60, 70, 110},
      {WO_BP, 1, 100, WS_LISY, 60, 85, 115}, {WO_ADD, 3, 100, 4, 100, 64, 64}, {WO_SLEW, WS_GATE, 100, 0, 0, 60, 80},
      {WO_MULT, 5, 100, 6, 100, 64, 127}},
     7, 1, {WM(1, 0), WM(2, 0), WM(3, 1), WM(4, 1)}, 0},
    {"VIOLENCE",      /* play the panel hard: strikes thicken the dust that steps T3, turning a knob unsettles the chaos that
                       * jumps T4 about, a hard strike restarts T2; held keys bend a ring; all of it struck and clipped */
     {{WO_DUST, WS_TAP, 100, 0, 0, 30, 60}, {WO_SEQ, 1, 100, 0, 0, 2, WSQ_STEP}, {WO_TICK, 0, 0, 0, 0, 6, 127},
      {WO_CHAOS, 3, 100, WS_TURN, 100, 100, 60}, {WO_SEQ, 3, 100, 4, 100, 3, WSQ_JUMP}, {WO_SEQ, WS_TAP, 100, 0, 0, 1, WSQ_RESTART},
      {WO_RING, 1, 100, WS_HELD, 40, 9, 45}, {WO_RING, 3, 100, 4, 25, 4, 60}, {WO_ADD, 7, 100, 8, 100, 64, 64},
      {WO_SAT, 9, 100, WS_TAP, 100, 50, 64}, {WO_SLEW, WS_GATE, 100, 0, 0, 0, 50}, {WO_MULT, 10, 100, 11, 100, 64, 64}},
     12, 1, {WM(1, 0), WM(4, 0), WM(10, 0), WM(7, 1)}, 0},
    {"THREE LOOPS",   /* no oscillators: after Jaap Vink's feedback patches at the Institute of Sonology. Loop 1: a low pass, a
                       * folder and a short delay (a breath of noise starts it); loop 2: a frequency shifter, a diode and a smear,
                       * fed a little of loop 1; loop 3 ring-modulates them, crushed. Loop 2's level darkens loop 1 and moves
                       * the shift, so each one reins in the other */
     {{WO_ADD, 4, 100, WS_NOISE, 3, 64, 64}, {WO_LP, 1, 100, 11, -60, 80, 70}, {WO_FOLD, 2, 100, 0, 0, 55, 70},
      {WO_DELAY, 3, 100, 0, 0, 40, 64}, {WO_ADD, 8, 100, 4, 40, 64, 64}, {WO_SHIFT, 5, 100, 11, 30, 80, 127},
      {WO_DIODE, 6, 100, 0, 0, 20, 30}, {WO_DIFFUSE, 7, 100, 0, 0, 70, 90}, {WO_RMOD, 4, 100, 8, 100, 64, 127},
      {WO_CRUSH, 9, 100, 0, 0, 45, 15}, {WO_FOLLOW, 7, 100, 0, 0, 20, 80}, {WO_MIX, 10, 100, 4, 100, 70, 64}},
     12, 1, {WM(6, 0), WM(2, 0), WM(7, 0), WM(12, 0)}, 0},
    {"GATED BONGO",   /* sixteenths divided by three and by four, each pinging a low pass gate (a sine, a ring), every
                       * sixteenth a softer one an octave up: the threes against the fours, and the gates' tails are the
                       * envelopes */
     {{WO_TICK, WS_GATE, 100, 0, 0, 8, 127}, {WO_CDIV, 1, 100, 0, 0, 5, 0}, {WO_CDIV, 1, 100, 0, 0, 6, 0},
      {WO_WAVE, 0, 0, 0, 0, 0, 64}, {WO_LPG, 4, 100, 2, 100, 78, WLPG_BOTH}, {WO_RING, 3, 100, 0, 0, 7, 50},
      {WO_LPG, 6, 100, 3, 100, 70, WLPG_BOTH}, {WO_ADD, 5, 100, 7, 100, 64, 64}, {WO_PHASE, 0, 0, 0, 0, 4, 64},
      {WO_WAVE, 9, 100, 0, 0, 20, 64}, {WO_LPG, 10, 100, 1, 55, 50, WLPG_BOTH}, {WO_MIX, 8, 100, 11, 100, 40, 110}},
     12, 1, {WM(5, 0), WM(7, 0), WM(11, 0), WM(2, 0)}, 0},
    {"DRIP BODY",     /* each note drops a bouncing ball onto a soft body and a ring: harder at first, so the body gives */
     {{WO_DRIP, 0, 0, 0, 0, 70, 90}, {WO_VISCERA, 1, 100, 0, 0, 64, 80}, {WO_RING, 1, 100, 0, 0, 9, 55},
      {WO_MIX, 2, 100, 3, 100, 50, 90}},
     4, 1, {WM(1, 0), WM(1, 1), WM(2, 0), WM(2, 1)}, 0},
    {"FROZEN TEAR",   /* an FM tone caught two cycles at a time at each note, torn two cycles in three, its time bent */
     {{WO_PHASE, 0, 0, 0, 0, 4, 64}, {WO_WAVE, 1, 100, 0, 0, 40, 64}, {WO_PHASE, 2, 40, 0, 0, 2, 64}, {WO_WAVE, 3, 100, 0, 0, 0, 64},
      {WO_CATCH, 4, 100, 0, 0, 2, 1}, {WO_RIFT, 5, 100, 0, 0, 3, 10}, {WO_BEND, 6, 100, 0, 0, 50, 30},
      {WO_SLOPE, 0, 0, 0, 0, 4, 70}, {WO_MULT, 7, 100, 8, 100, 64, 64}},
     9, 1, {WM(5, 1), WM(6, 0), WM(7, 0), WM(7, 1)}, 0},
    {"LAYERS",        /* a saw folded at the start and settling, its lows, mids and highs slid apart; a burst of noise lends
                       * it a struck skin, under the held tone */
     {{WO_WAVE, 0, 0, 0, 0, 85, 64}, {WO_FLEX, 1, 100, 0, 0, 100, 70}, {WO_LAMINA, 2, 100, 0, 0, 90, 0},
      {WO_NOISE, 0, 0, 0, 0, 90, 127}, {WO_SLOPE, WS_TRIG, 100, 0, 0, 0, 40}, {WO_MULT, 4, 100, 5, 100, 64, 64},
      {WO_FASCIA, 3, 100, 6, 100, 30, 100}, {WO_SLOPE, 0, 0, 0, 0, 4, 70}, {WO_MULT, 3, 100, 8, 100, 64, 64},
      {WO_MIX, 7, 100, 9, 100, 40, 64}},
     10, 1, {WM(2, 0), WM(3, 0), WM(7, 1), WM(10, 0)}, 0},
    {"MANGROVE",      /* an impulse each cycle (FORMANT, BARREL) into a sine fold for AIR, after Mannequins' Mangrove */
     {{WO_IMPULSE, 0, 0, 0, 0, 50, 64}, {WO_FOLD, 1, 100, 0, 0, 40, 64}, {WO_SLOPE, 0, 0, 0, 0, 2, 70},
      {WO_MULT, 2, 100, 3, 100, 64, 64}},
     4, 2, {WM(1, 0), WM(1, 1), WM(2, 0), WM(3, 1)}, 0},
    {"SPLIT TONE",    /* the impulse longer than a cycle, LFO 1 moving it across: the pitch divides, the tone splits */
     {{WO_IMPULSE, 0, 0, WS_LFO1, 40, 26, 40}, {WO_FOLD, 1, 100, 0, 0, 25, 64}, {WO_SLOPE, 0, 0, 0, 0, 3, 80},
      {WO_MULT, 2, 100, 3, 100, 64, 64}},
     4, 2, {WM(1, 0), WM(1, 1), WM(2, 0), WM(3, 1)}, 0},
    {"CREASED RAMP",  /* a ramp cut at zero, a slow cycle moving where (after Cold Mac's CREASE), through a low pass */
     {{WO_PHASE, 0, 0, 0, 0, 2, 64}, {WO_CREASE, 1, 100, 5, 60, 64, 64}, {WO_LP, 2, 100, 0, 0, 70, 30},
      {WO_SLOPE, 0, 0, 0, 0, 3, 70}, {WO_CYCLE, 0, 0, 0, 0, 20, 64}, {WO_MULT, 3, 100, 4, 100, 64, 64}},
     6, 2, {WM(2, 0), WM(2, 1), WM(3, 0), WM(5, 0)}, 0},
};
#define WF_NPRESETS ((int)(sizeof wf_presets / sizeof wf_presets[0]))
#endif
