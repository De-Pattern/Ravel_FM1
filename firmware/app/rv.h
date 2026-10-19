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
#define RV_SUB 32                   /* frames per control step: events, modulation */
#define RV_TRACKS 8
#define RV_STEPS 64
#define RV_PATTERNS 2               /* patterns per track (A, B) kept in RAM */
#define RV_SCENES 16
#define RV_ARR 32                   /* arrangement rows */
#define RV_LIB 256                  /* envelope library shapes */
#define RV_MATRIX 8
#define RV_MACROS 4
#define RV_MACRO_DESTS 8
#define RV_ROUTES_MAX 16            /* influence routes that may be active at once */
#define RV_SYNTH_VOICES 8
#define RV_STRIKE_VOICES 4
#define RV_BEADS 2                  /* grain engines for tape tracks */
#define RV_ROWS 4

/* memory: sized for the FM-1, where the instrument's state lives in the 336 KB RAM pool
 * (firmware/fm1/app.ld). The emulator uses the same. */
#ifndef RV_TAPE_BYTES
#define RV_TAPE_BYTES (120u * 1024u)  /* 1.4 s clean, 1.8 s tape, 5.5 s dirty */
#endif
#define RV_DELAY_SAMPLES 12288u     /* int16, in the FM-1's cache RAM: 0.28 s */
#define RV_FB_SAMPLES 6144u         /* int16: 0.14 s */
#define RV_CUT_SAMPLES 4096u        /* microcut ring, int16 mono: 93 ms */
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
    TP_DRIVE, TP_FOLD, TP_BITS, TP_DEREZ, TP_MASK, TP_VOC,
    /* insert */
    TP_INS, TP_INA, TP_INB, TP_INMIX,
    /* mixer */
    TP_LEVEL, TP_PAN, TP_FBS, TP_PAIRS, TP_DLYS, TP_REVS,
    /* sequencer */
    TP_LEN, TP_RATE, TP_DIR, TP_SIEVE, TP_CLKSRC, TP_BRANCH, TP_H2ROLE, TP_H2RATE, TP_H2START,
    /* the graph sequencer: mode, the two playheads and their function generators (Silt's
     * traversal shapes, which move each head's rate against the clock) */
    TP_SEQMODE, TP_H1ROLE, TP_H1SHAPE, TP_H1DEPTH, TP_H1CYCLE, TP_H1COMB, TP_H1TRANS,
    TP_H2TARGET, TP_H2SHAPE, TP_H2DEPTH, TP_H2CYCLE, TP_H2COMB, TP_H2TRANS,
    TP_N
};
#define RV_TP 80                    /* room for more track parameters without moving pids */

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
    GP_N
};
#define RV_GP 96
#define RV_GPID (RV_TRACKS * RV_TP)
#define RV_NPID (RV_GPID + RV_GP)
#define PID_T(t, p) ((t) * RV_TP + (p))
#define PID_G(g) (RV_GPID + (g))

enum { KIND_SYNTH, KIND_STRIKE, KIND_TAPE, KIND_N };
enum { ENG_VA, ENG_PD, ENG_FM, ENG_PHYS, ENG_FORMANT, ENG_PARTICLE, ENG_N };
enum { FT_LADDER, FT_LP, FT_BP, FT_HP, FT_LPG, FT_N };
enum { INS_OFF, INS_COMB, INS_SHIFT, INS_CRUSH, INS_SMEAR, INS_N };
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_SNAKE, DIR_GRAPH, DIR_N };   /* DIR_GRAPH: old projects; now TP_SEQMODE */
enum { SEQ_STEPS, SEQ_GRAPH };
enum { ROLE_OFF, ROLE_NOTES, ROLE_GATE, ROLE_PITCH };   /* what a playhead does at a step */
enum { FG_OFF, FG_RISE, FG_FALL, FG_ARCH, FG_DECAY, FG_SWELL, FG_LFO1, FG_LFO2, FG_DUST, FG_N };
enum { COMB_SCALE, COMB_ADD };                   /* function generator against the clock: times, or plus */
enum { TAPE_MLR, TAPE_BEADS, TAPE_SLICE, TAPE_MODES };
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
} rv_step;

typedef struct { rv_step s[RV_STEPS]; } rv_pattern;

/* --------------------------------------------------------------- influence --- */
enum { RT_OFF, RT_MOD, RT_TRIG, RT_EXCITE, RT_TAPE, RT_CLOCK, RT_RING, RT_N };
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
#define RV_FORMAT 3                 /* 3: graph mode and heads, more bus controls (2 loads and is upgraded) */
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
    uint8_t lib[RV_LIB][3];                /* envelope library: attack shape, body, death */
    rv_pattern pat[RV_TRACKS][RV_PATTERNS];
} rv_project;

extern rv_project rv;                      /* the project being played */

/* ------------------------------------------------------------- live state --- */
/* what the screen reads (written by the audio side, read by the control side: stale is fine) */
typedef struct {
    float meter[RV_TRACKS];                /* track peak, decaying */
    float env[RV_TRACKS];                  /* articulation level of the track's newest voice */
    float key[RV_TRACKS];
    float exc_in[RV_TRACKS];               /* what reaches a strike body from outside: EXCITE, feedback, input */                  /* the track's newest note, semitones from C4 */
    uint8_t stage[RV_TRACKS];              /* its stage: 0 onset, 1 continuant, 2 termination, 3 off */
    float stage_pos[RV_TRACKS];            /* 0..1 through the stage */
    int16_t step[RV_TRACKS][2];            /* playing step per head, -1 = none */
    float fg[RV_TRACKS][2];                /* each head's function generator: value 0..1 */
    float fg_pos[RV_TRACKS][2];            /* where in its cycle, 0..1 */
    float fg_rate[RV_TRACKS][2];           /* the head's rate now, steps per clock pulse */
    uint8_t hit[RV_TRACKS];                /* counts up on each trigger (flash the screen) */
    uint8_t route_fire[RV_TRACKS][RV_TRACKS];
    float master_l, master_r;
    float scope[RV_TRACKS][64];            /* recent output of each track, decimated */
    uint8_t scope_w;
    float lfo1, lfo2, dust, clock_env;
    float msrc[MS_N];
    float voc_band[8];
    float fb_env;
    uint32_t cpu;                          /* render time, % of the budget */
    uint8_t voices_synth, voices_strike;
    uint8_t playing, recording, rec_track, bar, beat;
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
int rv_moddest_next(int p, int dir);
extern const char *const rv_kind_names[], *const rv_engine_names[], *const rv_route_names[];
extern const char *const rv_msrc_names[], *const rv_cond_names[];
extern const char *const rv_scale_names[];
#define RV_NCOND 14
void rv_track_defaults(int t, int kind, int engine);
void rv_project_init(void);

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
float rv_param_eff(int t, int p);              /* the value after modulation, for the screen */
uint32_t rv_rng(void);

/* rv_project.c */
int rv_save(int slot);
int rv_load(int slot);
int rv_slot_used(int slot, char name[13]);
void rv_demo(int which);

/* rv_ui.c: the control side */
void ui_init(void);
void ui_frame(void);                            /* input, then a frame of the screen */
void ui_input_only(void);
