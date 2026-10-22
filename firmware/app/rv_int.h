/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel engine internals: the running state of voices, tracks, the tape, the buses and the
 * sequencer. Only the engine's modules include this (they build as one unit, ravel_app.c). */
#pragma once
#include "rv.h"
#include "rv_dsp.h"
#include "../../core/beads/bd.h"

#ifndef RV_BIG
#define RV_BIG                              /* the device may place big buffers in a section of its own */
#endif
#ifndef RV_CACHE_RAM
#define RV_CACHE_RAM                        /* the FM-1: the data cache's spare ways, 28 KB */
#endif

/* --------------------------------------------------------- articulation --- */
/* Silt's three states: onset (arrive, with overshoot), continuant (hold; may loop back to the
 * onset), termination (die, with a release texture). Give couples the path to a spring. */
enum { AS_ONSET, AS_CONT, AS_TERM, AS_OFF };
typedef struct {
    float atk, dec, sus, rel;       /* seconds, seconds, 0..1, seconds */
    float curve;                    /* -1..1 */
    float over;                     /* 0..1: how far past full the onset aims */
    float loop;                     /* loop period, seconds; 0 = no loop */
    float give;                     /* 0..1 */
    float tail;                     /* 0..1 release texture */
} artic_cfg_t;
typedef struct {
    uint8_t stage, gate, looping;
    float u, du;                    /* progress through the segment */
    float from, to;                 /* the segment's ends */
    float p;                        /* the path */
    float s, v;                     /* the spring */
    float out;                      /* the level: path, or the spring as far as give allows */
    float tailv;                    /* the release texture's envelope */
} artic_t;

/* the FILTER ENV: delay, attack, hold, decay, sustain, release, worked out a block at a time from
 * the track's settings (so it follows them live), its own bends, looping while held */
typedef struct {
    uint8_t stage;                  /* FE_* */
    float u, from, lvl;             /* through the stage 0..1, where the stage started, the level */
} fenv_t;
enum { FE_OFF, FE_DLY, FE_ATK, FE_HOLD, FE_DEC, FE_SUS, FE_REL };

/* -------------------------------------------------------------- voices --- */
typedef struct { float ph, ph2, sub, drift, drift_t; float sph[7]; uint32_t ns; } va_t;
/* CZ: one envelope (the CZ firmware's step sequencer over the chip's linear ramp) */
typedef struct { float cur, target, rate; int8_t step; uint8_t run; } czenv_t;   /* run: 0 stopped, 1 moving, 2 sustain */
typedef struct { float pos; czenv_t env[3]; } czline_t;   /* pos: 0..4096, two cycles (wave 1, then wave 2) */
typedef struct {
    czline_t l[2];
    float vib_t, vib_ph, pm, level, amp[2];
    uint32_t ns;
    uint8_t gate, done, pm_n;
} cz_t;
typedef struct { float ph[4], fb1, fb2, menv; } fm_t;
typedef struct { int16_t line[RV_KS_LEN], neck[RV_KS_LEN]; int32_t w, wn; float lp, ap, bow, exc, dly, prev; uint32_t ns; float mode_s[6][2]; } phys_t;   /* neck: BOWED's second segment */
typedef struct { float ph, vib; svf_t f[3]; float fof_ph[3], fof_env[3], fof_t[3]; uint32_t ns; } form_t;
typedef struct { float th[3], since[3], rd[3]; uint8_t on[3]; uint32_t ns; uint32_t count; } part_t;
#define RV_CHIP_BUF 2048                    /* CHIP's decompose memory: 46 ms of the voice */
typedef struct {
    float ph, ph2, pwm, pwm2, noise, sweep, vib_t, acc, hold, hold_n, drift;   /* pwm2: the sub's phase */
    uint32_t lfso, ns;                      /* the noise register, the decompose dice */
    int8_t step, dir;                       /* the arpeggio: where it is, which way */
    int16_t buf[RV_CHIP_BUF];
    int32_t w, frag_left, frag_len, frag_pos;
    uint8_t frag_mode;
    uint16_t flip;
} chip_t;

/* SQ: three wavetable oscillators (the DOC chip's way: 8-bit cycles, read without interpolation as
 * GRIT rises), ENV 2 (the filter's) and its pure decay (pitch, the osc 3 transient), a delayed vibrato */
typedef struct { float ph[3], e2, e2d, vib_t, vib_ph, hold_acc, hold_y; uint32_t ns; } sq_t;
/* WAVE: two wavetable oscillators and the wave envelope (where in its shape, 0..1) */
typedef struct { float ph[2], wu, wv; uint8_t wdone; } wave_t;
/* ORGAN: seven ranks and a celeste, each pipe's own onset, the chiff, air, wind and tremulant */
typedef struct { float ph[8], dly[8], cents[8], t, chiff, air_lp, wind, wind_v, trem_ph; svf_t ch; uint32_t ns; } organ_t;
/* MONO: one oscillator (saw, pulse, sub, noise), the scoop, delayed vibrato and drift */
typedef struct { float ph, sub, scoop, vib_t, vib_ph, drift, drift_v; uint32_t ns; } mono_t;
/* TINE: a struck or plucked bar or tine as a bank of modes (KALIMBA, MARIMBA, EPIANO): re/im
 * rotate per mode; rat is each mode's ratio to the note, lr its log2, w how hard it is struck */
#define TN_M 8
typedef struct { float re[TN_M], im[TN_M], w[TN_M], rat[TN_M], lr[TN_M], dk[TN_M];
  float exc_n, exc_len, exc_amp, bend, drift, drift_v, detune, roll_t, trem_ph, wah_ph, blp, click, thump, thlp, offn, held;
  uint8_t nm, gate_was; svf_t box[2]; uint32_t ns; } tine_t;

/* RESIN: a bowed string (BOW) or a reed and bore (HORN) in two waveguide segments that meet at the bow
 * or the reed; its own vibrato, drift and QUAKE; a small body (three modes) or a throat filter */
#define RS_BR 640                           /* the bridge (or bell) side: the short one */
#define RS_NK 1024                          /* the nut (or bore) side: down to about 43 Hz */
typedef struct {
    int16_t br[RS_BR], nk[RS_NK];
    int32_t wb, wk;
    float lp, bow, dly, held;               /* the bridge's loss filter, the bow (or breath) now, the reed's memory, time held */
    float vib_ph, vib_env, drift, drift_v, quiv, quiv_v, jit, jit_v, gph, sq, sq_t;   /* vibrato, drift, quiver, pressure jitter, growl, squeal */
    float body[3][2], hp, click;            /* the body's modes; air's high pass; a key click */
    svf_t thr;                              /* HORN's throat */
    uint32_t ns;
} resin_t;
/* LOGIC: three sources, the smoothing, and the grains' memory (the voice's own last 23 ms) */
#define RV_LOGIC_BUF 1024
typedef struct {
    float ph[3], lp, gpos[4], gt[4], ginc[4], gdt[4];
    uint32_t ns;
    int16_t buf[RV_LOGIC_BUF];
    int32_t w;
    uint8_t gon[4];
} logic_t;

/* a hit's own shading (a ratchet's R MOVE, a strike or perc track's GRAIN): how far it is
 * transposed, darkened, quietened and shortened, against the voice's own sound. on = 0: as it is */
typedef struct { uint8_t on; float semi, lpc, lps, gain, dk, env; } hitshade_t;

/* a Z filter (rv_zplane.h): six sections' coefficients, their state, the level hold */
typedef struct { float c[6][5], s[6][2], g; } zp_t;

typedef struct {
    uint8_t on, track, gate, held_key;
    int8_t note;
    float vel;
    uint32_t age;
    float pitch, target;            /* semitones (glide) */
    artic_t a;
    artic_cfg_t cfg;
    fenv_t fe;                      /* the FILTER ENV */
    uint8_t e_eng;                  /* the engine e's state belongs to, + 1 (0 = none) */
    ladder_t lad;
    svf_t svf, svf2;               /* svf2: the XT filters' second stage */
    float xs_y, xs_acc;            /* XT S&H: the held sample, the clock */
    zp_t zp;
    float lpg;
    float last;
    uint32_t rng;
    hitshade_t hs;
    union { va_t va; cz_t cz; fm_t fm; phys_t ph; form_t fo; part_t pa; chip_t ch; sq_t sq; wave_t wv; logic_t lg; organ_t og; mono_t mo; resin_t rs; tine_t tn; } e;
} voice_t;

/* PERC: one drum hit of a Machinedrum-style machine (rv_perc.c) */
typedef struct {
    uint8_t on, track, gate, mach, cnt, quiet;
    int8_t note, lane;              /* lane: a KIT's lane, -1 for a SINGLE machine */
    uint16_t lk_mask;               /* this hit's locks: bits 0-7 the parameters, 8 the level */
    uint8_t lk_val[9];
    float vel, t, level;
    uint32_t age, rng;
    float ph[6], aenv, penv, menv, nenv, xenv, exc, next, fbv, last, hp_s, hp_s2, am_ph;
    float my1[8], my2[8], mb1[8], mb2[8], mg[8];   /* modal resonators */
    svf_t f1, f2, hpf, lpf, eq;
    hitshade_t hs;
} perc_t;

/* strike: exciter into body, with a transient layer */
typedef struct {
    uint8_t on, track, gate;
    int8_t note;
    float vel;
    uint32_t age;
    artic_t a;
    artic_cfg_t cfg;
    float t;                        /* seconds since the hit */
    float exc_env, trans_env, pitch_env;
    uint32_t rng, dust_rng;
    float dust_acc, dust_next;
    float mph[6];                   /* metallic squares */
    float fmph[2];
    float md_y1[8], md_y2[8];       /* modal resonators: state (re, im) */
    float md_b1[8], md_b2[8], md_g[8];  /* r cos w, r sin w, input gain */
    svf_t ping;
    int16_t s1[RV_KS_LEN], s2[RV_KS_LEN];
    int32_t w1, w2;
    float lp1, lp2;
    float ring_ph;
    float bow_v;
    float last;
    float lim;                      /* the voice's level limiter: its envelope */
    uint8_t spark;                  /* rung by a SPARK route: a bare tap on the body, no exciter, no transient */
    hitshade_t hs;
} strike_t;

/* ---------------------------------------------------------------- tape --- */
typedef struct {
    float x;                        /* position within the row, 0..1 */
    float rate;
    uint8_t active;
    float old_x, fade;              /* the jump crossfade: where it was, how much remains */
} mlr_head_t;
typedef struct {
    mlr_head_t h[2];
    uint8_t running, rev, held_rate;
    float loop_a, loop_b;           /* loop span within the row (0..1), a >= b means the whole row */
    int8_t pend_pos, pend_loop;     /* a quantized jump waiting for its boundary */
    float rate_mul;
    /* slices */
    float slice[33];                /* slice starts within the row, 0..1 */
    uint8_t nslices, slices_valid;
    float sl_x, sl_end, sl_rate, sl_gain;
    uint8_t sl_on;
    float sl_old_x, sl_old_fade;
    artic_t a;
    artic_cfg_t cfg;
    float note_ofs;
} tapetrk_t;

/* a sample voice: plays a span of the tape, as KEYS (a sample across the keys), STRETCH (its
 * speed apart from its pitch) or SHARDS (the span broken in pieces, one at a time, in an order) */
typedef struct {
    uint8_t on, track, gate, mode, play, order, n_sh;
    int8_t lane, note, dir;
    float vel, gain;
    float s, e;                     /* a kit lane's span, in tape time (a track reads its own, live) */
    float pos, inc;                 /* where it reads (tape time) and its step per sample (the pitch) */
    float base, gph;                /* STRETCH: the window's place and its phase */
    float sh_u, gap;                /* SHARDS: how far into the current shard; a kit lane's GAP */
    uint8_t sh_k;                   /* SHARDS: which shard of the order */
    float env, dk;                  /* a kit lane's decay (a track's notes use the articulation) */
    uint32_t age;
    artic_t a;
    artic_cfg_t cfg;
    hitshade_t hs;
} samp_t;

/* --------------------------------------------------------------- track --- */
typedef struct {
    float eff[TP_N];                /* parameters after locks and modulation */
    int16_t lockv[TP_N];
    uint8_t lockon[TP_N];
    float buf[RV_SUB];              /* this control step's output (mono, post insert) */
    float prev[RV_SUB];             /* the last one: influence audio reads it */
    float exc[RV_SUB];              /* excitation coming in (influence, feedback return) */
    float ring[RV_SUB];
    float ring_amt;
    float lead_env, lead_vel;       /* newest voice's articulation level, velocity */
    float lead_key;                 /* newest note, semitones from C4 (the KEY source) */
    float meter;
    /* skin */
    float derez_hold, derez_ph;
    dcb_t dc;
    /* insert */
    int16_t comb[256];
    int32_t comb_w;
    float comb_lp;
    int16_t smear[300];
    int32_t sm_i[4];
    float sh_ph, sh_z, hilb_i[16], hilb_q[16];
    float crush_hold;
    int32_t crush_n;
    /* tape */
    tapetrk_t tape;
    /* pan */
    float pan_l, pan_r;
    float scope_acc;
    int scope_n;
    /* the CZ's chorus (Juno-60 style): one delay line, its output added left and taken away right */
    int16_t ch_buf[256];
    int32_t ch_w;
    float ch_ph, ch_lp;
} track_t;

/* ---------------------------------------------------------- sequencer --- */
enum { EV_ON, EV_OFF, EV_ADV, EV_JUMP, EV_CUT, EV_CATCH, EV_TRIG, EV_SPARK };
typedef struct {
    uint32_t when;                  /* sample clock */
    uint8_t type, track, note, vel;
    uint8_t step, head;
    uint16_t chain;                 /* tracks already triggered in this chain */
    uint8_t env;
    uint8_t rmv;                    /* a ratchet's repeat: bits 0-1 R MOVE, 2-4 which repeat, 5-7 of how many - 1 */
    float len_s;
} event_t;
#define RV_EVQ 128

typedef struct {
    int16_t step[2];                /* playing step per head */
    int8_t dir[2];                  /* pingpong direction */
    float next_at[2];               /* next advance, in master pulses */
    uint32_t passes;                /* how many times the pattern has looped */
    uint32_t step_time;             /* sample clock of the last advance */
    float step_dur;                 /* seconds per step at this track's rate */
    uint8_t alt;                    /* graph TURNS: alternates each pass */
    uint8_t first_take;             /* recording into an empty pattern */
    int16_t take_steps;
    int8_t held_note[16];           /* live notes being recorded: step they wrote, -1 none */
    int8_t held_key[16];            /* and the key each is */
    uint32_t held_on[16];
    uint32_t travelled[2];          /* steps each head has moved (its function generator's time) */
    int16_t pitch_reg;              /* the pitch a PITCH head set, for a GATE head to play */
    uint8_t pitch_set;
    uint8_t tied;                   /* a tied note still sounding, + 1: it ends when the next note starts */
} seqtrk_t;
