/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the instrument. Everything the modules share: sizes, parameters, the project, the
 * running state, and the functions each module offers.
 *
 * Ravel is portable C (C99, single-precision float, no C library): the same files build the
 * FM-1 firmware (bare metal, firmware/fm1), the desktop simulator and the browser emulator.
 * The platform it needs is plat.h. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "rv_math.h"

#define RAVEL_VERSION_STR "0.2"

/* ------------------------------------------------------------------ sizes --- */
#define RV_FS 44100                 /* the FM-1's codec runs at about 44.1 kHz */
#define RV_BLOCK 128                /* frames per audio interrupt */
#ifndef RV_SUB
#define RV_SUB 128                   /* frames per control step: events, modulation */
#endif
#define RV_TRACKS 8
#define RV_STEPS 64
#define RV_PATTERNS 2               /* patterns per track (A, B) kept in RAM */
#define RV_SCENES 16
#define RV_ARR 32                   /* arrangement rows */
#define RV_SENV 32                  /* the LIBRARY: step envelopes a step can trigger */
#define RV_SENV_SEG 8               /* segments in one */
#define RV_MATRIX 8
#define RV_MACROS 4
#define RV_MACRO_DESTS 8
#define RV_ROUTES_MAX 16            /* influence routes that may be active at once */
#define RV_SYNTH_VOICES 8
#define RV_STRIKE_VOICES 4
#define RV_PERC_VOICES 12
#define RV_BEADS 2                  /* grain engines for tape tracks */
#define RV_ROWS 4

/* memory: sized for the FM-1, where the instrument's state lives in the 336 KB RAM pool
 * (firmware/fm1/app.ld). The emulator uses the same. */
#ifndef RV_TAPE_BYTES
#define RV_TAPE_BYTES (120u * 1024u)  /* 1.4 s clean, 1.8 s tape, 5.5 s dirty */
#endif
#define RV_DELAY_SAMPLES 12288u     /* int16, in the FM-1's cache RAM: 0.28 s */
#define RV_FB_SAMPLES 6144u         /* int16: 0.14 s */
#define RV_CUT_SAMPLES 8192u        /* microcut ring, int16 mono: 186 ms (a cut is at most half) */
#define RV_KS_LEN 640               /* waveguide line, int16: 69 Hz lowest */

/* ------------------------------------------------------------- parameters --- */
/* A parameter is an int16 in [min, max]. Track parameters exist once per track; global
 * parameters once. A parameter id (pid) names either: track t, local p is t * RV_TP + p;
 * globals start at RV_GPID. */
enum {
    /* source */
    TP_KIND, TP_ENGINE,
    TP_S0, TP_S1, TP_S2, TP_S3, TP_S4, TP_S5, TP_S6, TP_S7,
    TP_S8, TP_S9, TP_S10, TP_S11, TP_S12, TP_S13, TP_S14, TP_S15,
    TP_PITCH, TP_FINE, TP_GLIDE, TP_POLY, TP_VELO,
    /* articulation */
    TP_ATK, TP_ACURVE, TP_OVER, TP_DEC, TP_SUS, TP_LOOP, TP_REL, TP_TAIL, TP_GIVE,
    TP_EFILT, TP_EDST1, TP_EAMT1, TP_EDST2, TP_EAMT2,
    /* filter */
    TP_FTYPE, TP_CUT, TP_RES, TP_KTRK,
    /* skin */
    TP_DRIVE, TP_FOLD, TP_BITS, TP_DEREZ, TP_MASK, TP_VOC,   /* (TP_VOC, TP_PAIRS, TP_BANDS and the VOC, PAIR, BAND globals: unused since format 20, kept so pids stay) */
    /* insert */
    TP_INS, TP_INA, TP_INB, TP_INMIX,
    /* mixer */
    TP_LEVEL, TP_PAN, TP_FBS, TP_PAIRS, TP_DLYS, TP_REVS,
    /* sequencer */
    TP_LEN, TP_RATE, TP_DIR, TP_INTERVAL, TP_CLKSRC, TP_BRANCH, TP_H2ROLE, TP_H2RATE, TP_H2START,
    /* the graph sequencer: mode, the two playheads and their function generators (Silt's
     * traversal shapes, which move each head's rate against the clock) */
    TP_SEQMODE, TP_H1ROLE, TP_H1SHAPE, TP_H1DEPTH, TP_H1CYCLE, TP_H1COMB, TP_H1TRANS,
    TP_H2TARGET, TP_H2SHAPE, TP_H2DEPTH, TP_H2CYCLE, TP_H2COMB, TP_H2TRANS,
    TP_BANDS,                       /* (format 5) the send into the BANDS bus */
    /* (format 12) the FILTER ENV, in the slots SHAPE had (formats 6-11): delay, attack, hold,
     * decay, sustain; release, the attack's and the decay's bends, loop and rate follow below */
    TP_FDLY, TP_FATK, TP_FHOLD, TP_FDEC, TP_FSUS,
    TP_FMODE, TP_FBP,               /* the SEM filter: low pass to notch to high pass, and its band pass (0: LP, off) */
    TP_OSC1,                        /* (format 11) the first oscillator's level: VA, FM (op 1), WAVE, CHIP */
    TP_FREL, TP_FACRV, TP_FDCRV, TP_FLOOP, TP_FRATE,   /* the FILTER ENV, continued */
    TP_SEDST, TP_SEAMT, TP_SEDEF,   /* (format 13) STEP ENV: where the step envelopes go, how far, the one a note starts */
    TP_FDST, TP_FAMT,               /* (format 15) the FILTER ENV into a second destination, any of the track's */
    TP_GRAIN,                       /* a strike or perc hit's own small differences, each time (0: none; old projects load 0) */
    TP_N
};
#define RV_TP 112                   /* room for more track parameters without moving pids (format 15: was 96; format 6: was 80) */

enum {
    GP_TEMPO, GP_SWING, GP_QUANT,
    GP_CLKMODE, GP_CLKSHAPE, GP_CLKBRATE, GP_CLKBSHAPE, GP_CLKDEPTH, GP_CLKTHR, GP_CLKLOCK,
    GP_ROOT, GP_SCALE, GP_TRANSPOSE,
    GP_LFO1RATE, GP_LFO1SHAPE, GP_LFO2RATE, GP_LFO2SHAPE, GP_DUSTRATE, GP_DUSTDECAY,
    GP_VOCBODY, GP_VOCTILT, GP_VOCREL, GP_VOCLEVEL,
    GP_FBTIME, GP_FBREGEN, GP_FBPROC, GP_FBAMT, GP_FBTUNE, GP_FBEDGE, GP_FBRET, GP_FBLEVEL, GP_FBCUT,
    GP_PAIRMODE, GP_PAIRF1, GP_PAIRF2, GP_PAIRRES, GP_PAIRLFO1, GP_PAIRLFO2, GP_PAIRDEPTH, GP_PAIRLEVEL,
    GP_DLYTIME, GP_DLYFB, GP_DLYTONE, GP_DLYLEVEL,
    GP_REVSIZE, GP_REVDAMP, GP_REVLEVEL,
    GP_MDRIVE, GP_CUTSIZE, GP_CUTREP, GP_CUTMIX,
    GP_TAPEQUAL, GP_TAPESRC, GP_TAPEMON, GP_RESBARS, GP_ERODE,
    GP_ROW1S, GP_ROW1E, GP_ROW2S, GP_ROW2E, GP_ROW3S, GP_ROW3E, GP_ROW4S, GP_ROW4E,
    GP_MACRO1, GP_MACRO2, GP_MACRO3, GP_MACRO4,
    GP_SONGMODE, GP_INPUTLEVEL,
    /* more of each bus */
    GP_DLYSPREAD, GP_DLYWOB, GP_DLYFREEZE, GP_REVMOD, GP_REVDIFF, GP_REVLOW,
    GP_VOCATK, GP_VOCSHIFT, GP_VOCQ, GP_PAIRT1, GP_PAIRT2,
    /* (format 5) BANDS: eight fixed resonant bands (after the Moog MuRF) and the sweep across them */
    GP_BAND1, GP_BAND2, GP_BAND3, GP_BAND4, GP_BAND5, GP_BAND6, GP_BAND7, GP_BAND8,
    GP_BANDQ, GP_BANDSHIFT, GP_BANDLEVEL, GP_BANDSHAPE, GP_BANDRATE, GP_BANDDEPTH, GP_BANDWIDTH,
    /* the delay's GRAINS mode (after Beads) and the space's CLOUDS mode */
    GP_DLYMODE, GP_GRDENS, GP_GRSIZE, GP_GRPITCH, GP_REVMODE,
    /* (format 9) the metronome and the count-in before recording */
    GP_METRO, GP_COUNTIN,
    GP_LFO1SYM, GP_LFO2SYM, GP_LFO1PHASE, GP_LFO2PHASE, GP_LFO1MODE, GP_LFO2MODE,   /* the LFOs' symmetry, start phase, LOOP/RETRIG/1 SHOT (0: as before) */
    /* (all 0 = as before, so older projects load unchanged) the punch-in cuts' shape; the BED under
     * the space; scenes that glide; density arcs over more bars */
    GP_CUTSHAPE,
    GP_BEDLEVEL, GP_BEDDENS, GP_BEDTONE, GP_BEDSHIM, GP_BEDDUCK, GP_BEDDECAY, GP_BEDSPACE,
    GP_SCNGLIDE, GP_ARCSPAN,
    /* (format 18) the tape delay's wear: flutter, saturation, age, and a doubler */
    GP_DLYFLUT, GP_DLYSAT, GP_DLYAGE, GP_DLYDBL,
    GP_N
};
#define RV_GP 128                   /* (format 5: was 96) */
_Static_assert(GP_N <= RV_GP && TP_N <= RV_TP, "the parameters fit their rows");
#define RV_GPID (RV_TRACKS * RV_TP)
#define RV_NPID (RV_GPID + RV_GP)
#define PID_T(t, p) ((t) * RV_TP + (p))
#define PID_G(g) (RV_GPID + (g))
#define PID_MAT(k) (RV_NPID + (k))     /* a matrix destination: row k's AMOUNT (only the matrix uses these) */

enum { KIND_SYNTH, KIND_STRIKE, KIND_TAPE, KIND_PERC, KIND_N };   /* PERC: drum machines after the Machinedrum */
enum { ENG_VA, ENG_CZ, ENG_FM, ENG_PHYS, ENG_FORMANT, ENG_PARTICLE, ENG_CHIP, ENG_WAVE, ENG_LOGIC, ENG_ORGAN, ENG_MONO, ENG_RESIN, ENG_TINE, ENG_N };   /* ENG_CZ: was PD */
enum { LW_SAW, LW_TRI, LW_SQR, LW_SINE, LW_PULSE, LW_NOISE, LW_N };   /* LOGIC: the sources' waves */
enum { LO_ADD, LO_MULT, LO_MIN, LO_MAX, LO_AND, LO_OR, LO_XOR, LO_NAND, LO_COMP, LO_GATE, LO_DIFF, LO_QUANT, LO_MOD, LO_SINE, LO_AOFB, LO_N };
enum { PM_TRX_BD, PM_TRX_B2, PM_TRX_SD, PM_TRX_XT, PM_TRX_CP, PM_TRX_RS, PM_TRX_CB, PM_TRX_CH, PM_TRX_OH, PM_TRX_CY,
       PM_TRX_MA, PM_TRX_CL, PM_TRX_XC, PM_EFM_BD, PM_EFM_SD, PM_EFM_XT, PM_EFM_CP, PM_EFM_RS, PM_EFM_CB, PM_EFM_HH,
       PM_EFM_CY, PM_PI_BD, PM_PI_SD, PM_PI_MT, PM_PI_RS, PM_PI_ML, PM_PI_MA, PM_PI_HH, PM_PI_RC, PM_PI_CC,
       PM_GND_SN, PM_GND_NS, PM_GND_IM, PM_N };   /* PERC machines */
enum { WX_OFF, WX_SYNC, WX_RING, WX_FM, WX_SYNCFM };   /* WAVE: osc 1 on osc 2 */
enum { WW_OFF, WW_SYNC, WW_BEND, WW_PWM, WW_MIRROR, WW_REMAP2, WW_REMAP4, WW_SQUEEZE, WW_FLIP, WW_QUANT, WW_N };   /* WAVE: phase warps */
enum { CW_TRI, CW_SAW, CW_PULSE, CW_NOISE, CW_SAWPUL, CW_RING, CW_PWM, CW_KICK, CW_SNARE, CW_N };   /* CHIP waves */
enum { FT_LADDER, FT_LP, FT_BP, FT_HP, FT_LPG, FT_MS20, FT_SH101, FT_ZVOWEL, FT_ZTALK, FT_ZPHASER, FT_ZPEAKS, FT_ZSWEEP, FT_ZMETAL, FT_XLP24, FT_XBP24, FT_XNOTCH, FT_XSH, FT_XSHAPE, FT_SEM, FT_N };   /* Z: morphing; X: digital (XT) */
#define FT_IS_Z(f) ((f) >= FT_ZVOWEL && (f) <= FT_ZMETAL)
#define FT_IS_XT(f) ((f) >= FT_XLP24 && (f) <= FT_XSHAPE)
enum { INS_OFF, INS_COMB, INS_SHIFT, INS_CRUSH, INS_SMEAR, INS_N };
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_SNAKE, DIR_GRAPH, DIR_N };   /* DIR_GRAPH: old projects; now TP_SEQMODE */
enum { SEQ_STEPS, SEQ_GRAPH };
enum { ROLE_OFF, ROLE_NOTES, ROLE_GATE, ROLE_PITCH };   /* what a playhead does at a step */
enum { FG_OFF, FG_RISE, FG_FALL, FG_ARCH, FG_DECAY, FG_SWELL, FG_LFO1, FG_LFO2, FG_DUST, FG_N };
enum { COMB_SCALE, COMB_ADD };                   /* function generator against the clock: times, or plus */
enum { TAPE_MLR, TAPE_KEYS, TAPE_SLICE, TAPE_STRETCH, TAPE_SHARDS, TAPE_MODES };   /* (KEYS was BEADS before format 18) */
enum { TPLAY_FWD, TPLAY_LOOP, TPLAY_PING, TPLAY_REV, TPLAY_REVLOOP, TPLAY_N };     /* how a sample plays its START..END */
#define RV_SAMP_VOICES 6                /* sample voices, shared by the tape tracks (KEYS, STRETCH, SHARDS, kit lanes) */
enum { EXC_CLICK, EXC_NOISE, EXC_METAL, EXC_FM, EXC_DUST, EXC_RAIN, EXC_BOW, EXC_BLOW, EXC_INPUT, EXC_N };
enum { BODY_PING, BODY_MODAL, BODY_DISC, BODY_STRINGS, BODY_N };

/* parameter display formats */
enum { PF_NUM, PF_BI, PF_ENUM, PF_PCT, PF_SEMI, PF_CENT, PF_HZ, PF_TIME, PF_NOTE, PF_TRACK, PF_DEST, PF_BPM, PF_ONOFF };

typedef struct {
    const char *name;               /* at most 7 characters: it shares the knob row with three more */
    int16_t min, max, def;
    uint8_t fmt;
    const char *const *names;       /* PF_ENUM */
} rv_pinfo;

/* ----------------------------------------------------------------- pattern --- */
enum { ST_ON = 1, ST_TIE = 2, ST_JUMP = 4 };   /* ST_JUMP: a tape jump; note = position (0..15) */
typedef struct {
    uint8_t flags, note, vel, len;  /* len in eighths of a step, 8 = one step */
    uint8_t chance;                 /* 0..100, 100 = always */
    int8_t micro;                   /* ticks of 1/24 step, -12..12 */
    uint8_t ratchet;                /* bits 0-2 count-1, bits 3-4 curve (even, faster, slower), bit 5 fade */
    uint8_t cond;                   /* trig condition, see rv_cond_names */
    uint8_t env;                    /* articulation from the library: shape + 1, 0 = the track's own */
    uint8_t cut;                    /* microcut: 0 none, 1 loop, 2 reverse, 3 stutter */
    uint8_t lp[2], lv[2];           /* parameter locks: local id + 1 (0 = none), value - min */
    uint8_t to[2];                  /* graph cords: step + 1, 0 = the next step */
    int8_t chord[3];                /* (format 17) more notes on the step, in semitones from note (0 = none): a chord of up to four */
} rv_step;
#define RV_STEP_OLD 16                  /* a step's size before format 17 */
_Static_assert(sizeof(rv_step) == RV_STEP_OLD + 3, "a step: 16 bytes, then its chord");

typedef struct { rv_step s[RV_STEPS]; } rv_pattern;

/* a step envelope (the LIBRARY): up to eight segments, each to a level in a time along a bend,
 * from a start level; then it loops back to a segment, or hands over to the NEXT envelope (a
 * chain), or holds. Times are in eighths of a step (synced) or 0.5 ms to 8 s (FREE). 24 bytes,
 * so the 32 of them take exactly the room the old triplet library had (formats 1-12). */
typedef struct {
    uint8_t lvl[RV_SENV_SEG];       /* 0..127 */
    uint8_t tim[RV_SENV_SEG];       /* 0..127: eighths of a step, or the time scale (FREE) */
    uint8_t crv[RV_SENV_SEG / 2];   /* two bends a byte, each -7..7 (low nibble: the even segment) */
    uint8_t flags;                  /* bits 0-3: segments 1..8 (0: empty), bit 4: FREE time */
    uint8_t loop;                   /* 0: no loop, 1..8: back to that segment at the end */
    uint8_t next;                   /* 0: none, 1..32: the envelope that takes over at the end */
    uint8_t start;                  /* 0..127: where it starts (a chained one starts where the last ended) */
} rv_senv;
#define SENV_NSEG(e) ((e)->flags & 15)
#define SENV_FREE(e) (((e)->flags >> 4) & 1)
static inline int senv_crv(const rv_senv *e, int i) { int v = (e->crv[(i >> 1) & 3] >> ((i & 1) * 4)) & 15; return v > 7 ? v - 16 : v; }
static inline void senv_set_crv(rv_senv *e, int i, int v)
{
    int sh = (i & 1) * 4;
    e->crv[(i >> 1) & 3] = (uint8_t)((e->crv[(i >> 1) & 3] & ~(15 << sh)) | (((v < -7 ? -7 : v > 7 ? 7 : v) & 15) << sh));
}

/* --------------------------------------------------------------- influence --- */
enum { RT_OFF, RT_MOD, RT_TRIG, RT_EXCITE, RT_TAPE, RT_CLOCK, RT_RING, RT_SPARK, RT_N };
typedef struct {
    uint8_t type;
    int8_t amt;                     /* -64..63 */
    uint8_t arg;                    /* MOD: target local parameter; TRIG: delay steps; TAPE: position (16 = catch) */
    uint8_t chance;                 /* TRIG: 0..100 */
} rv_route;

/* --------------------------------------------------------- matrix, macros --- */
enum { MS_OFF, MS_LFO1, MS_LFO2, MS_DUST, MS_ENV, MS_VEL, MS_WHEEL, MS_M1, MS_M2, MS_M3, MS_M4, MS_CLOCK, MS_KEY, MS_N };
typedef struct { uint8_t src; uint16_t dst; int8_t amt; } rv_mslot;
typedef struct { uint16_t dst; int8_t start, end; uint8_t curve, lo, hi; } rv_mdest;   /* curve: lin, exp, log, s; lo/hi: window 0..100 */
typedef struct { rv_mdest d[RV_MACRO_DESTS]; } rv_macro;

/* ------------------------------------------------------------- scene, song --- */
typedef struct {
    uint8_t used, pattern[RV_TRACKS], mute, resample;
    uint8_t level[RV_TRACKS];
    int8_t macro[RV_MACROS];
    rv_route infl[RV_TRACKS][RV_TRACKS];
} rv_scene;
typedef struct { uint8_t scene, bars; } rv_arrrow;

/* ----------------------------------------------------------------- project --- */
#define RV_MAGIC 0x4A505652u        /* "RVPJ" */
#define RV_FORMAT 21                /* 21: the SQ engine and the SQ-80 filter gone (their tracks load as VA and LADDER; later engines and filters move down one); 20: the FM-1 build without the vocoder, the filter pair and BANDS (their sends to 0), the space only 12-BIT, the pair's FREQ 1 and 2 now the feedback loop's BASE and WIDTH; 19: BLACKHOLE gone from the space (it loads as CLOUDS, 12-BIT moves down one); 18: TAPE plays samples (KEYS, STRETCH, SHARDS, kits; Beads gone), the delay is a granular tape delay; 17: chords on steps; 16: a rate per kit lane; 15: 112 track parameters (the FILTER ENV's DEST and AMOUNT); 14: DRIVE defaults to 0 (old projects at the old default 10 move to 0); 13: the LIBRARY is step envelopes (32, chainable), STEP ENV; 12: the FILTER ENV (SHAPE gone, its slots reused); 11: OSC LVL (old projects at full); 10: a tempo per scene; 9: metronome, count-in, swing in %; 8: kit locks; 7: PERC kits; 6: 96 track parameters (SHAPE); 5: 128 globals (BANDS); 4: the CZ patches; 3: graph mode and heads (2 and 3 load and are upgraded) */

/* the CZ engine's patch (a Casio CZ tone): per line, two waves, key follow and three 8-step
 * envelopes, all in the CZ's own panel units */
enum { CZE_DCO, CZE_DCW, CZE_DCA };
typedef struct {
    uint8_t rate[8], level[8];      /* 0..99 */
    uint8_t sus, end;               /* sustain step 1..8 (0 = none), end step 1..8 */
} rv_czenv;
typedef struct {
    uint8_t wave1, wave2;           /* 1..8 (SAW, SQUARE, PULSE, DBL SINE, SAW PULSE, RESO 1-3); wave2 0 = none */
    uint8_t kf_dca, kf_dcw;         /* key follow 0..9 */
    rv_czenv env[3];                /* DCO (pitch), DCW (waveform), DCA (level) */
} rv_czline;
typedef struct { rv_czline line[2]; } rv_cz;
/* a PERC track in KIT mode: six lanes, each its own machine, sequenced together (format 7) */
#define RV_LANES 6
typedef struct {
    uint8_t mach[RV_LANES];
    uint8_t p[RV_LANES][8];                /* each lane's machine parameters, 0..127 */
    uint8_t level[RV_LANES];               /* 0..127 */
    uint8_t len[RV_LANES];                 /* the lane's own length in steps, 0 = the track's LENGTH */
    uint8_t chance[RV_LANES];              /* 0..100 */
} rv_kit;
/* a KIT's parameter locks: a lane's sound set for one step (format 8). what = lane << 4 | field
 * (0-7 the machine's parameters, 8 its level); 0xFF = an empty entry. step: bits 0-5 the step,
 * bit 6 pattern B */
#define RV_KITLOCKS 48
#define KL_LEVEL 8
typedef struct { uint8_t step, what, val; } rv_kitlock;
typedef struct {
    uint32_t magic, format;
    char name[12];
    int16_t tp[RV_TRACKS][RV_TP];
    int16_t gp[RV_GP];
    rv_mslot mat[RV_MATRIX];
    rv_macro macro[RV_MACROS];
    rv_route infl[RV_TRACKS][RV_TRACKS];   /* the working grid */
    rv_scene scene[RV_SCENES];
    rv_arrrow arr[RV_ARR];
    uint8_t arr_len, scene_cur, mute, solo;
    uint8_t pat_cur[RV_TRACKS];
    uint8_t arcs[RV_TRACKS][16];           /* density per bar, 0..100 (100 = every step) */
    rv_senv senv[RV_SENV];                 /* (format 13) the LIBRARY of step envelopes, where the triplets were */
    rv_pattern pat[RV_TRACKS][RV_PATTERNS];
    rv_cz cz[RV_TRACKS];                   /* (format 4) last, so older projects load in front of it */
    rv_kit kit[RV_TRACKS];                 /* (format 7) after that */
    rv_kitlock kitlk[RV_TRACKS][RV_KITLOCKS];   /* (format 8) then the kits' locks */
    uint8_t scene_tempo[RV_SCENES];        /* (format 10) the tempo a scene sets, 0 = it keeps the tempo */
    uint8_t kit_rate[RV_TRACKS][RV_LANES]; /* (format 16) each kit lane's rate against the track's (rv_lane_rate; 0 = the same) */
} rv_project;
/* a kit lane's rate: knob value 0..8 = 1/4, 1/3, 1/2, 2/3, 1, 3/2, 2, 3, 4 times the track's; stored so that 0 is 1 */
#define RV_LRATE_KNOB(s) (((s) + 4) % 9)
#define RV_LRATE_STORE(v) (((v) + 5) % 9)
extern const char *const rv_lrate_names[];
#define RV_ARR_END 16                      /* an arrangement row's scene: the song stops there */

extern rv_project rv;                      /* the project being played */

/* state that may live outside the FM-1's RAM pool (app.ld puts .rv_ram_bss in plain RAM) */
#ifdef RV_FM1
#define RV_RAMBSS __attribute__((section(".rv_ram_bss")))
#else
#define RV_RAMBSS
#endif
/* a step's note length (rv_step.len): 1-128 in eighths of a step (to 16 steps, as it always was), then
 * 129-224 in half steps, to 64 steps */
#define RV_LEN_MAX 224
static inline float rv_len_steps(int len)
{
    if (len < 1)
        len = 1;
    return len <= 128 ? (float)len * 0.125f : 16.0f + (float)(len - 128) * 0.5f;
}
static inline int rv_len_code(float steps)
{
    int c = steps <= 16.0f ? (int)(steps * 8.0f + 0.5f) : 128 + (int)((steps - 16.0f) * 2.0f + 0.5f);
    return c < 1 ? 1 : c > RV_LEN_MAX ? RV_LEN_MAX : c;
}
void rv_fenv_from_env(int16_t *tp);
void rv_senv_factory(void);                 /* the LIBRARY's twelve shapes, the rest empty */         /* the FILTER ENV from the volume envelope (older work) */
void moom_start(void);                     /* the hidden game (rv_moom.c) */
int moom_active(void);
void moom_exit(void);
void moom_warp(int lv);                     /* straight to a level (tests) */
void moom_pose(float x, float y, float dir);

/* ------------------------------------------------------------- live state --- */
/* what the screen reads (written by the audio side, read by the control side: stale is fine) */
typedef struct {
    float meter[RV_TRACKS];                /* track peak, decaying */
    float env[RV_TRACKS];                  /* articulation level of the track's newest voice */
    float key[RV_TRACKS];
    float exc_in[RV_TRACKS];
    int8_t cz_step[RV_TRACKS][6];
    int8_t lane_step[RV_TRACKS][RV_LANES];  /* a KIT: the step each lane is on */          /* the newest CZ voice: each envelope's step (line 1 DCO DCW DCA, line 2) */               /* what reaches a strike body from outside: EXCITE, feedback, input */                  /* the track's newest note, semitones from C4 */
    uint8_t stage[RV_TRACKS];              /* its stage: 0 onset, 1 continuant, 2 termination, 3 off */
    float stage_pos[RV_TRACKS];            /* 0..1 through the stage */
    float fenv[RV_TRACKS], fenv_pos[RV_TRACKS];   /* the FILTER ENV of that voice: its level, 0..1 through its stage */
    uint8_t fenv_stage[RV_TRACKS];         /* FE_* (rv_int.h) */
    int16_t step[RV_TRACKS][2];            /* playing step per head, -1 = none */
    float fg[RV_TRACKS][2];                /* each head's function generator: value 0..1 */
    float fg_pos[RV_TRACKS][2];            /* where in its cycle, 0..1 */
    float fg_rate[RV_TRACKS][2];           /* the head's rate now, steps per clock pulse */
    uint8_t hit[RV_TRACKS];                /* counts up on each trigger (flash the screen) */
    uint8_t route_fire[RV_TRACKS][RV_TRACKS];
    float master_l, master_r;
    int16_t scope[RV_TRACKS][64];          /* recent output of each track, decimated (x 8192: half the RAM of floats) */
    uint8_t scope_w;
    float lfo1, lfo2, dust, clock_env;
    float msrc[MS_N];
    int8_t mat_amt[RV_MATRIX];          /* each matrix row's AMOUNT as other rows move it */
    float fb_env;
    uint32_t cpu;                          /* render time, % of the budget */
    uint8_t voices_synth, voices_strike;
    uint8_t playing, recording, rec_track, bar, beat;
    uint8_t countin;                       /* beats of the count-in still to come (0: none) */
    float bed_env, bed_duck;               /* the BED: its level, and how far the tracks hold it down (1: not at all) */
    uint8_t bed_hit;                       /* counts up on each grain of its dust */
    float glide;                           /* a scene gliding in: 0..1 (1: arrived) */
    float prof[16];                        /* GLO > CPU: each part of the render, % of the time it has (smoothed) */
    float load;                            /* the overload guard's reading: the render, % of the time it has */
    uint16_t shed;                         /* voices the guard has faded out to keep up (counts up) */
    uint8_t cap;                           /* the guard's voice limit now (255: none) */
    uint8_t guard_off;                     /* the stress test turns the guard off, to find the real edge */
    uint8_t cores2;                        /* GLO > CPU, CORES: the second core takes half the synth voices */
    uint8_t c2_fail;                       /* it failed (or would not take work): one core since */
    uint8_t c2_used;                       /* it took part in the last render */
    float c2_load;                         /* its work (the buses, its synth voices), % of the time it has (smoothed) */
    float c2_wait;                         /* this core waiting for it, % of the time (smoothed) */
    uint8_t fetch_on;                      /* GLO > CPU, FLASH WAIT: counting the code fetch's waits */
    float fetch_pct;                       /* the render's time spent waiting for its code (smoothed) */
    uint32_t tick;
} rv_live_t;
extern volatile rv_live_t rv_live;

/* --------------------------------------------------------------- functions --- */
/* rv_params.c */
const rv_pinfo *rv_pinfo_track(int t, int p);   /* depends on the track's kind and engine */
const rv_pinfo *rv_pinfo_global(int g);
const rv_pinfo *rv_pinfo_pid(int pid);
int rv_pid_get(int pid);
void rv_pid_set(int pid, int v);
void rv_param_format(int pid, int v, char *out, int max);
const char *rv_pid_label(int pid, char *buf, int max);   /* "T3 CUT" */
int rv_is_moddest(int p);                       /* a track parameter that modulation may move */
int rv_tp_applies(int t, int p);                /* 0: in the track's current setup this knob does nothing */
int rv_moddest_next_t(int t, int p, int dir);
int rv_moddest_next(int p, int dir);
extern const char *const rv_kind_names[], *const rv_engine_names[], *const rv_route_names[];
extern const char *const rv_msrc_names[], *const rv_cond_names[];
extern const char *const rv_scale_names[];
#define RV_NCOND 14
void rv_track_defaults(int t, int kind, int engine);
void rv_track_src_defaults(int t);              /* the engine's own knobs back to their defaults */
void rv_cz_init(int t);
void rv_engine_track_defaults(int t, int engine);
void rv_kit_init(int t);
int rv_is_moddest_g(int g);                     /* a global the matrix and macros can move */
void rv_tape_kit_init(int t);                    /* a TAPE kit: six lanes, each a sixth of row 1 */
extern const rv_pinfo tape_lane_pi[8];
extern const char *const rv_tape_row_names[];
void rv_tape_span(int t, float *a, float *b);
int rv_tape_voices(int t);                         /* a PERC kit's six lanes at their defaults */   /* ORGAN, MONO: the filter and envelope they want */                         /* a plain CZ patch on track t */
void rv_project_init(void);
int rv_up21_engine(int e);                      /* (format 21) an old engine or filter number, as it is now */
int rv_up21_ftype(int f);

/* rv_engine.c: the audio side */
void rv_engine_init(void);
void rv_render(int32_t *out, int frames);       /* interleaved stereo, Q23 */
void rv_note(int t, int note, int vel);         /* vel 0 = off; from the control side */
void rv_all_off(void);
void rv_transport(int run);                     /* start/stop */
void rv_catch(void);                            /* toggle the tape's catch */
void rv_resample(void);
void rv_tape_jump(int t, int pos, int loop_to);  /* tape track: to one of 16 positions; loop_to >= 0 loops the span */
void rv_tape_row(int t, int row);
void rv_tape_rate(int t, int how);              /* 0 reverse, 1 halve, 2 double, 3 hold/release */
void rv_punch(int fx, int on);                  /* punch-in effects */
void rv_cut(int action);                        /* microcut now */
void rv_scene_launch(int s);
void rv_scene_store(int s);
void rv_pattern_launch(int t, int p);
void rv_tap_tempo(void);
int rv_tape_frozen(void);
int rv_tape_overview(uint8_t *peaks, int n, int row);   /* row < 0: the whole tape */
int rv_tape_heads(int t, float *pos, int max);  /* positions 0..1 within the track's row */
int rv_tape_grains(int t, float *pos, float *lvl, int max);
float rv_tape_seconds(void);
int rv_tape_slices(int t, float *pos, int max);
void rv_record_arm(int t, int on);
void rv_motion(int pid, int v);                 /* a knob moved: motion recording */
void rv_live_note(int t, int note, int vel);    /* a key: plays and, when armed, records */
void rv_pattern_clear(int t);
int rv_kitlock_find(int t, int pat, int step, int lane, int field);   /* index, or -1 */
int rv_kitlock_set(int t, int pat, int step, int lane, int field, int v);   /* 0: the table is full */
void rv_kitlock_clear(int t, int pat, int step, int lane);
void rv_pattern_copy(int t);                    /* the current pattern onto the clipboard */
int rv_pattern_paste(int t);                    /* 0, or -1 nothing copied, -2 kit and notes don't mix */   /* lane -1: every lane */
void rv_track_set_kind(int t, int kind);           /* KIND changed: defaults, and steps cleared when lanes and notes swap */
float rv_param_eff(int t, int p);              /* the value after modulation, for the screen */
uint32_t rv_rng(void);

/* rv_project.c */
int rv_save(int slot);
int rv_load(int slot);
int rv_slot_used(int slot, char name[13]);
/* projects and sounds to and from a computer (rv_xfer.c) */
void rv_xfer_msg(const uint8_t *m, uint32_t d);
int rv_xfer_wire(const uint8_t *p, uint32_t n);
void ui_storage_changed(int slot);
void rv_bank_settle(void);
void rv_bank_reload(int slot);
void rv_demo(int which);
/* rv_stress.c: the stress test (GLO > CPU) */
#define RV_STRESS_N 17
void rv_stress(int step, int engine);          /* 0: stop; else build the project for that step and play it */
const char *rv_stress_name(int step);

/* rv_ui.c: the control side */
void ui_init(void);
void ui_frame(void);                            /* input, then a frame of the screen */
void ui_input_only(void);
