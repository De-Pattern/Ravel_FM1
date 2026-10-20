/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: articulation and the six synth engines (VA, PD, FM, PHYS, FORMANT, PARTICLE).
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

/* ----------------------------------------------------------- articulation --- */
static void artic_cfg_track(artic_cfg_t *c, const float *eff)
{
    c->atk = rv_time_f(eff[TP_ATK]);
    c->dec = rv_time_f(eff[TP_DEC]);
    c->sus = clampf(eff[TP_SUS] * (1.0f / 127.0f), 0.0f, 1.0f);
    c->rel = rv_time_f(eff[TP_REL]);
    c->curve = clampf(eff[TP_ACURVE] * (1.0f / 64.0f), -1.0f, 1.0f);
    c->over = clampf(eff[TP_OVER] * (1.0f / 127.0f), 0.0f, 1.0f);
    c->loop = eff[TP_LOOP] < 0.5f ? 0.0f : 4.0f * fm_exp2f(-eff[TP_LOOP] * (13.0f / 127.0f));   /* 4 s .. 0.5 ms */
    c->give = clampf(eff[TP_GIVE] * (1.0f / 127.0f), 0.0f, 1.0f);
    c->tail = clampf(eff[TP_TAIL] * (1.0f / 127.0f), 0.0f, 1.0f);
    c->shape = (uint8_t)clampi((int)(eff[TP_ESHAPE] + 0.5f), 0, SHP_N);
    c->smode = (uint8_t)clampi((int)(eff[TP_EMODE] + 0.5f), 0, 3);
    c->morph = clampf(eff[TP_EMORPH] * (1.0f / 127.0f), 0.0f, 1.0f);
    c->stime = rv_time_f(eff[TP_ETIME]);
    c->warp = clampf(eff[TP_EWARP] * (1.0f / 64.0f), -1.0f, 1.0f);
}

/* a library shape: attack shape, body, death (Avenir's Envion idea: three values make an envelope) */
static void artic_cfg_lib(artic_cfg_t *c, const uint8_t *l, const float *eff)
{
    float a = l[0] * (1.0f / 127.0f), b = l[1] * (1.0f / 127.0f), d = l[2] * (1.0f / 127.0f);
    artic_cfg_track(c, eff);                     /* keeps give, loop and tail from the track */
    c->shape = 0;                                /* a library recall is a classic articulation */
    c->atk = 0.0005f * fm_exp2f(a * 12.0f);      /* 0.5 ms .. 2 s */
    c->curve = 0.6f - a * 1.2f;                  /* fast attacks bow out, slow ones swell */
    c->over = a < 0.2f ? 0.15f : 0.0f;
    c->dec = 0.02f * fm_exp2f(b * 7.0f);         /* 20 ms .. 2.5 s */
    c->sus = b * b;                              /* percussive .. held */
    c->rel = 0.005f * fm_exp2f(d * 10.0f);       /* 5 ms .. 5 s */
    c->tail = d > 0.7f ? (d - 0.7f) * 2.0f : c->tail;
}

/* SHAPE: the library (rv_shapes.h), a shape read at u (0..1), WARP bending time and MORPH
 * crossfading toward the next shape */
#define SHP_DATA
#include "rv_shapes.h"
static float shp_eval(int shape, float morph, float warp, float u)
{
    float x, fr, a, b;
    int i, s0 = (shape - 1) % SHP_N, s1 = shape % SHP_N;
    u = clampf(u, 0.0f, 1.0f);
    if (warp > 0.01f || warp < -0.01f)             /* > 0: lingers early, < 0: rushes early */
        u = fm_powf(u, fm_exp2f(warp));
    x = u * (float)(SHP_LEN - 1);
    i = (int)x;
    if (i >= SHP_LEN - 1)
        i = SHP_LEN - 2;
    fr = x - (float)i;
    a = shp_tab[s0][i] + (shp_tab[s0][i + 1] - shp_tab[s0][i]) * fr;
    if (morph < 0.004f)
        return a * (1.0f / 255.0f);
    b = shp_tab[s1][i] + (shp_tab[s1][i + 1] - shp_tab[s1][i]) * fr;
    return (a + (b - a) * morph) * (1.0f / 255.0f);
}
/* where a GATED shape waits: the morphed shapes' hold points (in shape time, before the warp) */
static float shp_hold_u(const artic_cfg_t *c)
{
    float h0 = shp_hold[(c->shape - 1) % SHP_N], h1 = shp_hold[c->shape % SHP_N];
    float h = (h0 + (h1 - h0) * c->morph) * (1.0f / (float)(SHP_LEN - 1));
    if (c->warp > 0.01f || c->warp < -0.01f)
        h = fm_powf(h, fm_exp2f(-c->warp));   /* the time that maps onto it */
    return h;
}

static inline float shape_up(float u, float c) { return u + c * u * (1.0f - u); }     /* c > 0: fast start */
static inline float shape_down(float u) { float r = 1.0f - u; return 1.0f - r * r * r; }  /* fast then slow */

static void artic_start(artic_t *a, const artic_cfg_t *c)
{
    a->sp = 0.0f;
    a->stage = AS_ONSET;
    a->gate = 1;
    a->looping = 0;
    a->from = a->p;                              /* legato: from wherever it is */
    a->to = 1.0f + c->over * 0.6f;
    a->u = 0.0f;
    a->du = RV_DT / (c->atk > 0.0002f ? c->atk : 0.0002f);
    a->tailv = 0.0f;
}

static void artic_release(artic_t *a, const artic_cfg_t *c)
{
    if (a->stage == AS_OFF || a->stage == AS_TERM)
        return;
    if (c->shape && c->smode != SHM_LOOP) {          /* SHAPE: one-shot, gated and cycle play on */
        a->gate = 0;
        return;
    }
    a->gate = 0;
    a->stage = AS_TERM;
    a->from = a->p;
    a->to = 0.0f;
    a->u = 0.0f;
    a->du = RV_DT / (c->rel > 0.0005f ? c->rel : 0.0005f);
    a->tailv = c->tail;
}

/* one sample; returns the level */
static inline float artic_tick(artic_t *a, const artic_cfg_t *c)
{
    float o;
    if (c->shape && a->stage != AS_OFF && a->stage != AS_TERM) {   /* SHAPE (LOOP's release is the classic one) */
        float hold;
        a->sp += RV_DT / (c->stime > 0.002f ? c->stime : 0.002f);
        switch (c->smode) {
        case SHM_GATED:
            hold = shp_hold_u(c);
            if (a->gate && a->sp >= hold && a->sp - RV_DT / (c->stime > 0.002f ? c->stime : 0.002f) <= hold + 1e-4f)
                a->sp = hold;                        /* held: wait at the hold point */
            break;
        case SHM_LOOP:
        case SHM_CYCLE:
            if (a->sp >= 1.0f && a->gate)
                a->sp -= 1.0f;
            break;
        default:
            break;
        }
        if (a->sp >= 1.0f) {
            a->p = 0.0f;
            a->stage = AS_OFF;
        } else {
            a->p = shp_eval(c->shape, c->morph, c->warp, a->sp);
            a->stage = AS_CONT;
        }
        a->u = a->sp;
        a->tailv *= 0.9993f;
        goto spring;
    }
    switch (a->stage) {
    case AS_ONSET:
        a->u += a->du;
        if (a->u >= 1.0f) {
            a->p = a->to;
            a->stage = AS_CONT;
            a->from = a->p;
            a->to = c->sus;
            a->u = 0.0f;
            a->du = RV_DT / ((a->looping ? c->loop * 0.5f : c->dec) + 0.0002f);
        } else {
            a->p = a->from + (a->to - a->from) * shape_up(a->u, c->curve);
        }
        break;
    case AS_CONT:
        if (a->u < 1.0f) {
            a->u += a->du;
            a->p = a->from + (a->to - a->from) * shape_down(a->u > 1.0f ? 1.0f : a->u);
        } else if (c->loop > 0.0f && a->gate) {      /* loop back to the onset while held */
            a->stage = AS_ONSET;
            a->looping = 1;
            a->from = a->p;
            a->to = 1.0f + c->over * 0.6f;
            a->u = 0.0f;
            a->du = RV_DT / (c->loop * 0.5f + 0.0002f);
        } else {
            a->p += (c->sus - a->p) * 0.002f;          /* follows the sustain knob */
        }
        break;
    case AS_TERM:
        a->u += a->du;
        if (a->u >= 1.0f) {
            a->p = 0.0f;
            a->stage = AS_OFF;
        } else {
            float r = 1.0f - a->u;
            a->p = a->from * r * r * r;
        }
        a->tailv *= 0.9993f;
        break;
    default:
        a->p = 0.0f;
        a->tailv *= 0.9993f;
        break;
    }
spring:
    if (c->give > 0.001f) {                         /* the spring: 9 Hz, lightly damped */
        const float k = 3197.0f, damp = 2.0f * 0.16f * 56.5f;
        a->v += (k * (a->p - a->s) - damp * a->v) * RV_DT;
        a->s += a->v * RV_DT;
        o = a->p + c->give * 1.6f * (a->s - a->p);
    } else {
        a->s = a->p;
        a->v = 0.0f;
        o = a->p;
    }
    a->out = o < 0.0f ? 0.0f : o;
    return a->out;
}

/* ------------------------------------------------------------- tables --- */
#define WT_LEN 256
static int16_t wt[8][WT_LEN + 1];                  /* particle tables, Q14 */
static float sine_tab[1025];

static void synth_tables(void)
{
    int i, t;
    uint32_t ns = 0x1234567u;
    for (i = 0; i <= 1024; i++)
        sine_tab[i] = fm_sin_turns((float)i / 1024.0f);
    for (i = 0; i < WT_LEN; i++) {
        float x = (float)i / WT_LEN, v, w[8];
        w[0] = fm_sin_turns(x);
        w[1] = (fm_sin_turns(x) + 0.5f * fm_sin_turns(7 * x) + 0.33f * fm_sin_turns(13 * x) + 0.25f * fm_sin_turns(19 * x)) * 0.5f;
        v = 0.0f;
        for (t = 1; t < 12; t += 2)
            v += fm_sin_turns(t * x) / (float)t * (1.0f + 2.0f * fm_expf(-(t - 5) * (t - 5) * 0.2f));
        w[2] = v * 0.45f;
        v = 0.0f;
        for (t = 1; t < 16; t++) {
            float f = 130.0f * t, e = 1.0f / (1.0f + (f - 730.0f) * (f - 730.0f) / 4900.0f) + 0.5f / (1.0f + (f - 1090.0f) * (f - 1090.0f) / 8100.0f) +
                                      0.25f / (1.0f + (f - 2440.0f) * (f - 2440.0f) / 14400.0f);
            v += e * fm_sin_turns(t * x + 0.18f * t * t);
        }
        w[3] = v * 0.6f;
        w[4] = (fm_sin_turns(x) + 0.5f * fm_sin_turns(2.756f * x) + 0.3f * fm_sin_turns(5.404f * x)) * 0.6f;
        {
            int k = (int)(x * 256.0f);
            w[5] = (float)(((k & (k >> 3)) & 31)) / 15.5f - 1.0f;
        }
        w[6] = xs_bi(&ns) * 0.7f;
        w[7] = fm_sin_turns(x * (1.0f + 6.0f * x)) * (1.0f - x);
        for (t = 0; t < 8; t++)
            wt[t][i] = (int16_t)clampf(w[t] * 16384.0f, -32767.0f, 32767.0f);
    }
    for (t = 0; t < 8; t++)
        wt[t][WT_LEN] = wt[t][0];
}

static inline float sin_lut(float ph)              /* ph in turns, any range */
{
    float x = ph - fm_floorf(ph), f;
    int i;
    x *= 1024.0f;
    i = (int)x;
    f = x - (float)i;
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}

static inline float wt_read(const int16_t *w, float ph)
{
    float x = ph - fm_floorf(ph), f;
    int i;
    x *= WT_LEN;
    i = (int)x;
    f = x - (float)i;
    return ((float)w[i] + (float)(w[i + 1] - w[i]) * f) * (1.0f / 16384.0f);
}

/* ---------------------------------------------------------------- VA --- */
static const float va_ratios[8] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 7.0f};

static void va_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    va_t *s = &v->e.va;
    float dt = f * RV_DT, wave = eff[TP_S0] * (1.0f / 127.0f), pw = 0.5f + (eff[TP_S1] - 64.0f) * (0.45f / 64.0f);
    int nsup = clampi((int)(eff[TP_S2] + 0.5f), 1, 7), i, k;
    float det = eff[TP_S3] * (1.0f / 127.0f), sub = eff[TP_S4] * (1.0f / 127.0f), noise = eff[TP_S5] * (1.0f / 127.0f);
    float tz = eff[TP_S6] * (1.0f / 127.0f), fmr = va_ratios[clampi((int)(eff[TP_S7] + 0.5f), 0, 7)];
    float sync = eff[TP_S8] * (1.0f / 127.0f);
    float norm = 1.0f / fm_sqrtf((float)nsup);
    static const float spread[7] = {0.0f, -0.11f, 0.11f, -0.37f, 0.37f, -0.71f, 0.71f};
    for (i = 0; i < n; i++) {
        float x = 0.0f, ph, sl_dt;
        if (tz > 0.001f) {                            /* through-zero FM: a sine modulator, naive carrier */
            float m = sin_lut(s->ph2);
            float inc = dt * (1.0f + tz * 6.0f * m);
            s->ph2 += dt * fmr;
            s->ph2 -= fm_floorf(s->ph2);
            s->ph += inc;
            s->ph -= fm_floorf(s->ph);
            ph = s->ph;
            x = (1.0f - wave) * (2.0f * ph - 1.0f) + wave * (ph < pw ? 1.0f : -1.0f);
            x = lerpf(x, sin_lut(ph), 0.5f);
        } else if (sync > 0.001f) {                   /* hard sync: a slave saw reset by the master */
            sl_dt = dt * (1.0f + sync * 3.0f);
            s->ph += dt;
            s->ph2 += sl_dt;
            if (s->ph >= 1.0f) {
                s->ph -= fm_floorf(s->ph);
                s->ph2 = s->ph * (sl_dt / dt);
            }
            s->ph2 -= fm_floorf(s->ph2);
            x = 2.0f * s->ph2 - 1.0f - blep(s->ph2, sl_dt);
            x = lerpf(x, s->ph2 < pw ? 1.0f : -1.0f, wave);
        } else {
            for (k = 0; k < nsup; k++) {               /* one saw, or a supersaw of up to seven */
                float d = dt * (1.0f + spread[k] * det * 0.06f);
                float p = k == 0 ? s->ph : s->sph[k];
                float saw, pul, p2;
                p += d;
                p -= fm_floorf(p);
                saw = 2.0f * p - 1.0f - blep(p, d);
                p2 = p + (1.0f - pw);
                p2 -= fm_floorf(p2);
                pul = (p < pw ? 1.0f : -1.0f) + blep(p, d) - blep(p2, d);
                x += lerpf(saw, pul, wave);
                if (k == 0)
                    s->ph = p;
                else
                    s->sph[k] = p;
            }
            x *= norm;
        }
        if (sub > 0.001f) {                            /* a square an octave down */
            s->sub += dt * 0.5f;
            s->sub -= fm_floorf(s->sub);
            x += sub * (s->sub < 0.5f ? 0.7f : -0.7f);
        }
        if (noise > 0.001f)
            x += noise * xs_bi(&s->ns) * 0.6f;
        out[i] = x * 0.5f;
    }
}

/* ---------------------------------------------------------------- CZ --- */
/* The Casio CZ (CZ-101/1000/3000/5000/CZ-1): what its NEC uPD933 chip does, as worked out in
 * MAME's uPD933 device (BSD-3-Clause, Devin Acker), driven the way the CZ's firmware drives it,
 * with the panel-to-chip conversions from the CZ's MIDI tone format.
 *
 * A line is one chip voice: a cosine read through a phase transfer that DCW bends (eight waves,
 * the resonant three a sine at 1 + DCW/64 times the pitch, hard-synced, under a saw, triangle or
 * trapezoid window). Two waves alternate cycle by cycle (the CZ's octave-down cascade). Each
 * line has three 8-step envelopes in the chip's units: DCO adds pitch (6.25-cent steps up to
 * level 63, then 2-semitone steps), DCW sets the bend (0..1016), DCA the level on the chip's
 * logarithmic curve (78 dB). A step's rate code (8 | r & 7) << (r >> 3) is how far it moves
 * each 40 kHz chip sample. Line 2 can be ring-modulated by line 1 or have its pitch kicked by
 * noise (0 or +32 semitones every 8 chip samples), as the chip does. */
static const uint16_t cz_vib_delay[100] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 67, 71, 75, 79, 83, 87, 91, 95, 99, 103, 107, 111, 115, 119, 123, 127, 135, 143, 151, 159, 167, 175, 183, 191, 199, 207, 215, 223, 231, 239, 247, 255, 271, 287, 303, 319, 335, 351, 367, 383, 399, 415, 431, 447, 463, 479, 495, 511, 543, 575, 607, 639};
static const uint16_t cz_vib_rate[100] = {32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704, 736, 768, 800, 832, 864, 896, 928, 960, 992, 1024, 1120, 1184, 1248, 1312, 1376, 1440, 1504, 1568, 1632, 1696, 1760, 1824, 1888, 1952, 2016, 2080, 2272, 2400, 2528, 2656, 2784, 2912, 3040, 3168, 3296, 3424, 3552, 3680, 3808, 3936, 4064, 4192, 4576, 4832, 5088, 5344, 5600, 5856, 6112, 6368, 6624, 6880, 7136, 7392, 7648, 7904, 8160, 8416, 9184, 9696, 10208, 10720, 11232, 11744, 12256, 12768, 13280, 13792, 14304, 14816, 15328, 15840, 16352, 16864, 18400, 19424, 20448, 21472};
static const uint16_t cz_vib_depth[100] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 71, 75, 79, 83, 87, 91, 95, 99, 103, 107, 111, 115, 119, 123, 127, 131, 143, 151, 159, 167, 175, 183, 191, 199, 207, 215, 223, 231, 239, 247, 255, 263, 287, 303, 319, 335, 351, 367, 383, 399, 415, 431, 447, 463, 479, 495, 511, 527, 575, 607, 639, 768};

static const uint8_t cz_kf_dca[10] = {0x00, 0x08, 0x11, 0x1A, 0x24, 0x2F, 0x3A, 0x45, 0x52, 0x5F};
static const uint8_t cz_kf_dcw[10] = {0x00, 0x1F, 0x2C, 0x39, 0x46, 0x53, 0x60, 0x6E, 0x92, 0xFF};
#define CZ_CHIP_STEP (40000.0f / RV_FS)                 /* chip samples per Ravel sample */

static float cz_rate_units(int which, int r)
{
    int c;
    r = clampi(r, 0, 99);
    c = which == CZE_DCO ? 127 * r / 99 : which == CZE_DCW ? 119 * r / 99 + 8 : 119 * r / 99;
    return (float)((8 | (c & 7)) << (c >> 3));
}

static float cz_level_units(int which, int l)
{
    l = clampi(l, 0, 99);
    if (which == CZE_DCO)                             /* 0..63: 6.25 cents; 64..99: 2 semitones from 8 */
        return l <= 63 ? (float)(l << 16) : (float)((l - 60) << 21);
    return (float)((127 * l / 99) << (which == CZE_DCW ? 19 : 18));
}

static void cz_env_aim(czenv_t *e, const rv_czenv *p, int which, float kf)
{
    int s = clampi(e->step, 0, 7);
    e->target = cz_level_units(which, p->level[s]);
    e->rate = cz_rate_units(which, p->rate[s]) * kf;
    e->run = 1;
}

static void cz_env_start(czenv_t *e, const rv_czenv *p, int which, float kf)
{
    e->cur = 0.0f;
    e->step = 0;
    cz_env_aim(e, p, which, kf);
}

/* the key went up: on to the step after the sustain point (if it was not passed yet) */
static void cz_env_release(czenv_t *e, const rv_czenv *p, int which, float kf)
{
    int sus = clampi(p->sus, 0, 8), end = clampi(p->end, 1, 8);
    if (sus && e->step < sus && sus < end) {
        e->step = (int8_t)sus;
        cz_env_aim(e, p, which, kf);
    } else if (e->run == 2) {
        e->run = e->step + 1 < end ? 1 : 0;
        if (e->run) {
            e->step++;
            cz_env_aim(e, p, which, kf);
        }
    }
}

/* n Ravel samples: towards the step's level, then the next step, the sustain point or the end */
static void cz_env_tick(czenv_t *e, const rv_czenv *p, int which, float kf, int gate, int n)
{
    int sus = clampi(p->sus, 0, 8), end = clampi(p->end, 1, 8);
    float mv;
    if (e->run != 1)
        return;
    mv = e->rate * (float)n * CZ_CHIP_STEP;
    if (fm_fabsf(e->target - e->cur) <= mv) {
        e->cur = e->target;
        if (sus && e->step == sus - 1 && gate)
            e->run = 2;
        else if (e->step >= end - 1)
            e->run = 0;
        else {
            e->step++;
            cz_env_aim(e, p, which, kf);
        }
    } else {
        e->cur += e->target > e->cur ? mv : -mv;
    }
}

static int cz_chip_wave(int w) { static const uint8_t m[9] = {0, 0, 1, 2, 4, 5, 6, 6, 6}; return m[clampi(w, 0, 8)]; }

/* one line's sample: pos 0..4096 (two cycles), dcw 0..1023, window 0..3; 0..1 before the DCA */
static inline float cz_line_sample(int w0, int w1, int win, float pos, float dcw)
{
    float p = pos >= 2048.0f ? pos - 2048.0f : pos, ph = 0.0f, pv = 1024.0f - dcw, wv = 0.0f, s;
    int w = pos >= 2048.0f ? w1 : w0;
    if (pv < 1.0f)
        pv = 1.0f;
    switch (w) {
    case 0: ph = p < pv ? p * 1024.0f / pv : 1024.0f + (p - pv) * 1024.0f / (2048.0f - pv); break;   /* saw */
    case 1: {                                                                                       /* square */
        float q = p >= 1024.0f ? p - 1024.0f : p;
        ph = (q < pv ? q * 1024.0f / pv : 1023.0f) + (p >= 1024.0f ? 1024.0f : 0.0f);
        break;
    }
    case 2: ph = p < pv * 2.0f ? p * 2048.0f / (pv * 2.0f) : 2047.0f; break;                          /* pulse */
    case 4: ph = p < pv ? p * 2048.0f / pv : (p - pv) * 2048.0f / (2048.0f - pv); break;               /* double sine */
    case 5: ph = p < 1024.0f ? p : p < pv + 1024.0f ? 1024.0f + (p - 1024.0f) * 1024.0f / pv : 2047.0f; break;   /* saw pulse */
    case 6: ph = p + p * dcw * (1.0f / 64.0f); ph -= 2048.0f * fm_floorf(ph * (1.0f / 2048.0f)); break;    /* resonance */
    default: break;
    }
    switch (win) {                                    /* the resonant waves' windows */
    case 1: wv = p; break;                                                    /* saw: falls over the cycle */
    case 2: wv = p < 1024.0f ? 2048.0f - p * 2.0f : (p - 1024.0f) * 2.0f; break; /* triangle */
    case 3: wv = p >= 1024.0f ? (p - 1024.0f) * 2.0f : 0.0f; break;           /* trapezoid: falls in the second half */
    default: break;
    }
    s = 0.5f - 0.5f * sin_lut(ph * (1.0f / 2048.0f) + 0.25f);   /* the chip's 0..1 cosine */
    return s * (1.0f - wv * (1.0f / 2048.0f));
}

static void cz_note(voice_t *v, const float *eff, int t)
{
    cz_t *c = &v->e.cz;
    int l, k, note = v->note, mode = clampi((int)(eff[TP_S0] + 0.5f), 0, 3);
    for (l = 0; l < 2; l++) {
        const rv_czline *pl = &rv.cz[t & 7].line[mode == 1 ? 1 : mode == 3 ? l : 0];
        float kfa = fm_exp2f((float)cz_kf_dca[clampi(pl->kf_dca, 0, 9)] * (1.0f / 95.0f) * (float)(note - 60) * (1.0f / 12.0f));
        c->l[l].pos = 0.0f;                           /* the CZ restarts the oscillator on each note */
        for (k = 0; k < 3; k++)
            cz_env_start(&c->l[l].env[k], &pl->env[k], k, k == CZE_DCA ? kfa : 1.0f);
    }
    c->vib_t = 0.0f;
    c->gate = 1;
    c->done = 0;
    if (!c->ns)
        c->ns = v->rng | 1u;
}

static void cz_render(voice_t *v, const float *eff, float f, float *out, int n, int t)
{
    cz_t *c = &v->e.cz;
    const rv_cz *pz = &rv.cz[t & 7];
    int mode = clampi((int)(eff[TP_S0] + 0.5f), 0, 3), oct = clampi((int)(eff[TP_S1] + 0.5f), 0, 2) - 1;
    int mod = clampi((int)(eff[TP_S2] + 0.5f), 0, 2), lines = mode >= 2 ? 2 : 1, i, l, k;
    int gate = v->a.gate, note = v->note;
    float det = ((float)clampi((int)(eff[TP_S5] + 0.5f), 0, 3) * 12.0f + (float)clampi((int)(eff[TP_S6] + 0.5f), 0, 11)
                 + eff[TP_S7] * (1.0f / 60.0f)) * (eff[TP_S4] > 0.5f ? -1.0f : 1.0f);
    float vib = 0.0f, foam = eff[TP_S13] * (1.0f / 127.0f), dcwoff = eff[TP_S12] * 8.0f;
    float veld = (float)clampi((int)(eff[TP_S14] + 0.5f), 0, 9) * (1.0f / 9.0f), lev = 0.0f;
    float dt[2], dcw[2], a0[2], a1[2];
    int w0[2], w1[2], win[2];
    /* the key went up */
    if (c->gate && !gate) {
        for (l = 0; l < 2; l++) {
            const rv_czline *pl = &pz->line[mode == 1 ? 1 : mode == 3 ? l : 0];
            float kfa = fm_exp2f((float)cz_kf_dca[clampi(pl->kf_dca, 0, 9)] * (1.0f / 95.0f) * (float)(note - 60) * (1.0f / 12.0f));
            for (k = 0; k < 3; k++)
                cz_env_release(&c->l[l].env[k], &pl->env[k], k, k == CZE_DCA ? kfa : 1.0f);
        }
    }
    c->gate = (uint8_t)gate;
    /* vibrato: after its delay, at its rate and depth, in one of four shapes */
    c->vib_t += (float)n * RV_DT;
    if (eff[TP_S11] > 0.5f && c->vib_t >= (float)cz_vib_delay[clampi((int)(eff[TP_S9] + 0.5f), 0, 99)] * 0.01f) {
        float x;
        int vw = clampi((int)(eff[TP_S8] + 0.5f), 0, 3);
        c->vib_ph += (float)cz_vib_rate[clampi((int)(eff[TP_S10] + 0.5f), 0, 99)] * 0.00085f * (float)n * RV_DT;
        c->vib_ph -= fm_floorf(c->vib_ph);
        x = vw == 0 ? (c->vib_ph < 0.5f ? 4.0f * c->vib_ph - 1.0f : 3.0f - 4.0f * c->vib_ph)
          : vw == 1 ? (c->vib_ph < 0.5f ? 1.0f : -1.0f) : vw == 2 ? 2.0f * c->vib_ph - 1.0f : 1.0f - 2.0f * c->vib_ph;
        vib = x * (float)cz_vib_depth[clampi((int)(eff[TP_S11] + 0.5f), 0, 99)] * (12.0f / 768.0f);
    }
    /* each line's settings and envelopes for this block */
    for (l = 0; l < lines; l++) {
        int src = mode == 1 ? 1 : mode == 3 ? l : 0;   /* 1, 2, 1+1' (line 1 twice), 1+2' */
        const rv_czline *pl = &pz->line[src];
        czline_t *ln = &c->l[l];
        float kfa = fm_exp2f((float)cz_kf_dca[clampi(pl->kf_dca, 0, 9)] * (1.0f / 95.0f) * (float)(note - 60) * (1.0f / 12.0f));
        float kfw = (float)cz_kf_dcw[clampi(pl->kf_dcw, 0, 9)] * (1.0f / 255.0f), semi, fl, lim, d;
        int wa = clampi(pl->wave1, 1, 8), wb = clampi(pl->wave2, 0, 8);
        if (wa >= 6 && wb >= 6)                        /* the CZ cannot cascade two resonant waves */
            wb = 0;
        w0[l] = cz_chip_wave(wa);
        w1[l] = wb ? cz_chip_wave(wb) : w0[l];
        win[l] = wa >= 6 ? wa - 5 : wb >= 6 ? wb - 5 : 0;
        a0[l] = ln->env[CZE_DCA].cur;
        for (k = 0; k < 3; k++)
            cz_env_tick(&ln->env[k], &pl->env[k], k, k == CZE_DCA ? kfa : 1.0f, gate, n);
        a1[l] = ln->env[CZE_DCA].cur;
        semi = ln->env[CZE_DCO].cur * (1.0f / 1048576.0f) + (float)oct * 12.0f + vib + (l == 1 ? det : 0.0f);
        fl = f * fm_exp2f(semi * (1.0f / 12.0f));
        dt[l] = clampf(fl * 4096.0f * 0.5f * RV_DT, 0.0f, 1900.0f);   /* 2048 units a cycle */
        d = ln->env[CZE_DCW].cur * (1.0f / 65536.0f);
        d *= clampf(1.0f - kfw * (float)(note - 36) * (1.0f / 48.0f), 0.0f, 1.0f);   /* DCW key follow: duller up high */
        d *= 1.0f - veld * (1.0f - v->vel) * 0.9f;                                      /* VEL DCW (CZ-1 style) */
        d += dcwoff;                                                                    /* DCW +- (modulatable) */
        lim = 1024.0f - clampf(fl * (8192.0f / 40000.0f), 0.0f, 1024.0f);              /* the chip's high-note limit */
        dcw[l] = clampf(d, 0.0f, lim > 0.0f ? lim : 0.0f);
        for (k = 0; k < 3; k++)
            rv_live.cz_step[t & 7][l * 3 + k] = (int8_t)(ln->env[k].run ? ln->env[k].step : -1);
    }
    /* the DCA as amplitude, on the chip's log curve (index 0..508 of 511 over 13 bits) */
    for (l = 0; l < lines; l++) {
        float i0 = a0[l] * (1.0f / 65536.0f), i1 = a1[l] * (1.0f / 65536.0f);
        a0[l] = i0 < 1.0f ? 0.0f : fm_exp2f(i0 * (13.0f / 511.0f) - 13.0f);
        a1[l] = i1 < 1.0f ? 0.0f : fm_exp2f(i1 * (13.0f / 511.0f) - 13.0f);
    }
    for (i = 0; i < n; i++) {
        float u = (float)i / (float)n, y = 0.0f, sa = 0.0f;
        for (l = 0; l < lines; l++) {
            czline_t *ln = &c->l[l];
            float amp = a0[l] + (a1[l] - a0[l]) * u, s, step = dt[l];
            if (l == 1 && mod == 2) {                  /* noise: line 2's pitch kicked every 8 chip samples */
                if (++c->pm_n >= 9) {
                    c->pm_n = 0;
                    c->pm = (xs_uni(&c->ns) < 0.5f) ? 0.0f : 1.0f;
                }
                if (c->pm > 0.5f)
                    step *= 6.3496f;                   /* +32 semitones */
            }
            s = (cz_line_sample(w0[l], w1[l], win[l], ln->pos, dcw[l]) - 0.5f) * 2.0f * amp;
            if (l == 1 && mod == 1)                     /* ring: line 2 times line 1, as the chip multiplies */
                s *= sa;
            ln->pos += step;
            if (ln->pos >= 4096.0f)
                ln->pos -= 4096.0f;
            if (l == 0) {
                sa = s;
                if (foam > 0.0f) {                      /* FOAM: the fundamental taken out, the sidebands left */
                    float p = ln->pos >= 2048.0f ? ln->pos - 2048.0f : ln->pos;
                    s -= foam * 0.8f * amp * -sin_lut(p * (1.0f / 2048.0f) + 0.25f);
                }
            }
            y += s;
        }
        out[i] = y * (lines == 2 ? 0.4f : 0.5f);
    }
    /* the level the rest of Ravel sees (the filter's envelope, influence, the matrix's ENV) */
    for (l = 0; l < lines; l++)
        if (a1[l] > lev)
            lev = a1[l];
    c->level = fm_sqrtf(lev);
    /* done: every line's DCA has stopped at nothing, or the key is up and it has nowhere to go */
    {
        int alive = 0;
        for (l = 0; l < lines; l++) {
            czenv_t *e = &c->l[l].env[CZE_DCA];
            if (e->run == 0 && !gate && e->cur > 0.0f) {   /* ended above zero with the key up: fade out */
                e->target = 0.0f;
                e->rate = cz_rate_units(CZE_DCA, 70);
                e->run = 1;
                e->step = 7;
            }
            if (e->run || e->cur >= 65536.0f)
                alive = 1;
        }
        c->done = (uint8_t)!alive;
    }
}

/* ---------------------------------------------------------------- FM --- */
static const float fm_ratio_tab[32] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f,
                                       5.5f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f,
                                       1.4142f, 1.7321f, 2.2361f, 2.7183f, 3.1416f, 0.7071f, 1.01f, 2.01f};
/* per algorithm: which ops modulate each op (bit k = op k+1), and which are heard */
static const uint8_t fm_mod[12][4] = {
    {2, 4, 8, 0}, {2 | 8, 4, 0, 0}, {2 | 4 | 8, 0, 0, 0}, {2, 0, 8, 0}, {4, 4, 8, 0}, {8, 8, 8, 0},
    {2, 0, 0, 0}, {0, 4, 8, 0}, {2, 4, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
static const uint8_t fm_car[12] = {1, 1, 1, 1 | 4, 1 | 2, 1 | 2 | 4, 1 | 4 | 8, 1 | 2, 1 | 8, 15, 15, 0};

static inline float fm_wave(int w, float ph)
{
    float s, x;
    switch (w) {
    case 0: return sin_lut(ph);
    case 1: x = ph - fm_floorf(ph); return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
    case 2: x = ph - fm_floorf(ph); return 2.0f * x - 1.0f;
    case 3: x = ph - fm_floorf(ph); return x < 0.5f ? 0.8f : -0.8f;
    case 4: s = sin_lut(ph); return s > 0.0f ? s * 1.6f - 0.5f : -0.5f;
    case 5: s = sin_lut(ph); return (s < 0.0f ? -s : s) * 2.0f - 1.27f;
    case 6: x = ph - fm_floorf(ph); return x < 0.25f ? sin_lut(x) : x < 0.5f ? 0.0f : x < 0.75f ? -sin_lut(x - 0.5f) : 0.0f;
    default: s = sin_lut(ph); return (float)(int)(s * 7.0f) * (1.0f / 7.0f);
    }
}

static void fm_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    fm_t *s = &v->e.fm;
    int alg = clampi((int)(eff[TP_S0] + 0.5f), 0, 11), i, k;
    float fb = eff[TP_S1] * (1.0f / 127.0f) * 0.25f;
    float dt[4], lvl[4], wv[4];
    float vmod = 1.0f - eff[TP_S15] * (1.0f / 127.0f) * (1.0f - v->vel);
    float mdec = rv_time_f(eff[TP_S13] * 0.8f + 10.0f), tz = eff[TP_S14] * (1.0f / 127.0f);
    float mcoef = RV_DT / mdec;
    for (k = 0; k < 4; k++) {
        dt[k] = f * fm_ratio_tab[clampi((int)(eff[TP_S2 + k] + 0.5f), 0, 31)] * RV_DT;
        wv[k] = (float)clampi((int)(eff[TP_S9 + k] + 0.5f), 0, 7);
    }
    lvl[0] = 1.0f;
    for (k = 1; k < 4; k++) {
        float l = eff[TP_S5 + k] * (1.0f / 127.0f);
        lvl[k] = l * l;
    }
    for (i = 0; i < n; i++) {
        float o[4], ph, y = 0.0f, menv;
        s->menv += (0.25f - s->menv) * mcoef;
        menv = s->menv * vmod;
        /* compute ops 4..1: modulators feed earlier ops */
        for (k = 3; k >= 0; k--) {
            float mod = 0.0f;
            uint8_t m = fm_mod[alg][k];
            int j;
            for (j = k + 1; j < 4; j++)
                if (m & (1 << j))
                    mod += o[j];
            if (k == 3 || (alg >= 9 && k == 3))
                mod += fb * (s->fb1 + s->fb2);
            ph = s->ph[k] + mod * (alg == 11 ? 0.0f : 1.0f);
            o[k] = fm_wave((int)wv[k], ph);
            if (k == 3) {
                s->fb2 = s->fb1;
                s->fb1 = o[3];
            }
            /* modulators are scaled by level and the mod envelope; carriers by level only */
            if (!(fm_car[alg] & (1 << k)))
                o[k] *= lvl[k] * menv * 2.5f;
            else
                o[k] *= k == 0 ? 1.0f : lvl[k];
        }
        if (alg == 11) {
            y = (sin_lut(s->ph[0]) * sin_lut(s->ph[1]) * 1.0f + sin_lut(s->ph[2]) * sin_lut(s->ph[3]) * lvl[3]) * 0.8f;
        } else {
            int nc = 0;
            for (k = 0; k < 4; k++)
                if (fm_car[alg] & (1 << k)) {
                    y += o[k];
                    nc++;
                }
            if (nc > 1)
                y *= 1.0f / fm_sqrtf((float)nc);
        }
        for (k = 0; k < 4; k++) {
            float inc = dt[k];
            if (k == 0 && tz > 0.001f)
                inc *= 1.0f + tz * 4.0f * o[1];      /* through-zero: op 1's rate can go negative */
            s->ph[k] += inc;
            s->ph[k] -= fm_floorf(s->ph[k]);
        }
        out[i] = y * 0.45f;
    }
    (void)mcoef;
}

/* -------------------------------------------------------------- PHYS --- */
static const float membrane_ratio[6] = {1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f};

static void phys_strike(voice_t *v, const float *eff)
{
    phys_t *s = &v->e.ph;
    int model = clampi((int)(eff[TP_S0] + 0.5f), 0, 3), i;
    float exc = eff[TP_S4] * (1.0f / 127.0f), bright = eff[TP_S1] * (1.0f / 127.0f);
    float pos = 0.05f + eff[TP_S3] * (0.45f / 127.0f), lp = 0.0f;
    float per = 44100.0f / note_hz((float)v->note + eff[TP_PITCH]);
    int len = clampi((int)per, 4, RV_KS_LEN - 2), pdel = clampi((int)(len * pos), 1, len - 1);
    if (model == 3) {                                 /* membrane: a modal bank kicked once */
        for (i = 0; i < 6; i++)
            s->mode_s[i][0] = s->mode_s[i][1] = 0.0f;
        s->exc = exc * v->vel;
        return;
    }
    if (model != 0)
        return;                                       /* bowed and blown are excited continuously */
    for (i = 0; i < len; i++) {                        /* a burst: noise, coloured by brightness */
        float x = xs_bi(&s->ns);
        lp += (x - lp) * (0.08f + bright * 0.9f);
        s->line[i] = (int16_t)clampf(lp * exc * v->vel * 16000.0f, -32000.0f, 32000.0f);
    }
    for (i = len - 1; i >= pdel; i--)                  /* the pluck position: a comb on the burst */
        s->line[i] = (int16_t)clampi(s->line[i] - s->line[i - pdel], -32000, 32000);
    for (i = len; i < RV_KS_LEN; i++)
        s->line[i] = 0;
    s->w = len;                                       /* the read point (w - len) lands on the burst */
}

static void phys_render(voice_t *v, const float *eff, float f, const float *exc_in, float *out, int n)
{
    phys_t *s = &v->e.ph;
    int model = clampi((int)(eff[TP_S0] + 0.5f), 0, 3), i, k;
    float bright = eff[TP_S1] * (1.0f / 127.0f), damp = eff[TP_S2] * (1.0f / 127.0f);
    float exc = eff[TP_S4] * (1.0f / 127.0f), inh = eff[TP_S5] * (1.0f / 127.0f);
    float body = eff[TP_S6] * (1.0f / 127.0f), decay = eff[TP_S7] * (1.0f / 127.0f);
    float per = 44100.0f / f, lpc = 0.15f + bright * 0.84f - damp * 0.3f;
    float t60 = 0.05f + decay * decay * 12.0f;
    float g = fm_exp2f(-per / (t60 * 44100.0f) * 9.97f);   /* loop gain for that T60 */
    float apc = inh * 0.7f, lpd;
    if ((uint32_t)s->w >= RV_KS_LEN)                  /* the voice's memory is shared by every engine: */
        s->w = 0;                                     /* after a switch these may hold another's bytes */
    if ((uint32_t)s->wn >= RV_KS_LEN)
        s->wn = 0;
    if (!(s->bow == s->bow) || fm_fabsf(s->bow) > 4.0f)
        s->bow = 0.0f;
    if (model == 3) {
        float b1[6], b2[6], gn[6];
        for (k = 0; k < 6; k++) {
            float fr = f * (membrane_ratio[k] + inh * k * 0.11f);
            float r = fm_exp2f(-1.0f / ((0.03f + decay * 2.5f) / (1.0f + k * (0.5f + damp * 2.0f)) * 44100.0f) * 1.44f);
            if (fr > 18000.0f)
                fr = 18000.0f;
            /* a decaying rotation (coupled form): its energy cannot grow when the pitch moves */
            b1[k] = r * fm_cosf(6.2831853f * fr * RV_DT);
            b2[k] = r * fm_sinf(6.2831853f * fr * RV_DT);
            gn[k] = (k == 0 ? 1.0f : 0.25f + bright * 0.75f) * 0.6f;
        }
        for (i = 0; i < n; i++) {
            float x = s->exc + exc_in[i] * 0.5f, y = 0.0f;
            s->exc = 0.0f;
            for (k = 0; k < 6; k++) {
                float re = b1[k] * s->mode_s[k][0] - b2[k] * s->mode_s[k][1] + gn[k] * x;
                float im = b2[k] * s->mode_s[k][0] + b1[k] * s->mode_s[k][1];
                s->mode_s[k][0] = re;
                s->mode_s[k][1] = im;
                y += im;
            }
            out[i] = y * 0.5f;
        }
        return;
    }
    lpc = clampf(lpc, 0.05f, 0.99f);
    lpd = (1.0f - lpc) / lpc;                         /* the loss filter's delay: tuned out */
    if (model == 1) {
        /* bowed (STK's bowed string): two segments of string meet at the bow, POSITN from the
         * bridge. Each sample the bow grips or slips by the friction curve and sends the same
         * velocity both ways; the bridge end is lossy (BRIGHT, DAMP, DECAY), the nut end rigid. */
        float beta = 0.05f + eff[TP_S3] * (0.4f / 127.0f), tot = per - lpd, db, dn;
        if (tot > 2.0f * (RV_KS_LEN - 3))
            tot = 2.0f * (RV_KS_LEN - 3);
        db = clampf(tot * beta, 1.5f, RV_KS_LEN - 3.0f);
        dn = clampf(tot - db, 1.5f, RV_KS_LEN - 3.0f);
        for (i = 0; i < n; i++) {
            float br, nr, sv, dv, bt, nv, a0, a1, fr;
            int di, r0, r1;
            di = (int)db; fr = db - (float)di;            /* the bridge segment's far end */
            r0 = s->w - di; if (r0 < 0) r0 += RV_KS_LEN;
            r1 = r0 - 1; if (r1 < 0) r1 += RV_KS_LEN;
            a0 = (s->line[r0] + (s->line[r1] - s->line[r0]) * fr) * (1.0f / 16000.0f);
            di = (int)dn; fr = dn - (float)di;            /* the neck segment's far end */
            r0 = s->wn - di; if (r0 < 0) r0 += RV_KS_LEN;
            r1 = r0 - 1; if (r1 < 0) r1 += RV_KS_LEN;
            a1 = (s->neck[r0] + (s->neck[r1] - s->neck[r0]) * fr) * (1.0f / 16000.0f);
            s->lp += (a0 - s->lp) * lpc;
            br = -s->lp * g;                              /* the bridge reflects, with loss */
            nr = -a1;                                     /* the nut reflects */
            sv = br + nr;                                 /* the string's speed under the bow */
            s->bow += ((v->a.gate ? (0.03f + exc * 0.22f) * v->vel : 0.0f) - s->bow) * 0.003f;
            dv = s->bow - sv;
            bt = fm_fabsf(dv * (5.0f - inh * 3.0f) + 0.001f) + 0.75f;   /* INHARM: lighter bow pressure */
            bt = 1.0f / (bt * bt * bt * bt);
            nv = dv * (bt > 1.0f ? 1.0f : bt) + exc_in[i] * 0.3f;
            s->neck[s->wn] = (int16_t)clampf((br + nv) * 16000.0f, -32760.0f, 32760.0f);
            s->line[s->w] = (int16_t)clampf((nr + nv) * 16000.0f, -32760.0f, 32760.0f);
            s->w = (s->w + 1) % RV_KS_LEN;
            s->wn = (s->wn + 1) % RV_KS_LEN;
            s->dly += (a0 - s->dly) * 0.02f;
            out[i] = (a0 + body * s->dly * 3.0f) * 1.6f;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        /* blown: a clarinet's bore inverts at the reed, so the loop is half the period */
        float d = (model == 2 ? per * 0.5f : per) - lpd - (apc > 0.0f ? 1.0f : 0.0f), y, rd, frac, x;
        int di, r0, r1;
        if (d < 2.0f)
            d = 2.0f;
        if (d > RV_KS_LEN - 3)
            d = RV_KS_LEN - 3;
        di = (int)d;
        frac = d - (float)di;
        r0 = s->w - di;
        if (r0 < 0)
            r0 += RV_KS_LEN;
        r1 = r0 - 1;
        if (r1 < 0)
            r1 += RV_KS_LEN;
        rd = (s->line[r0] + (s->line[r1] - s->line[r0]) * frac) * (1.0f / 16000.0f);
        s->lp += (rd - s->lp) * lpc;                  /* loss: brightness, damping */
        y = s->lp * g;
        if (apc > 0.0f) {                             /* stiffness: an allpass in the loop */
            float a = apc * (y - s->ap) + s->prev;
            s->prev = y;
            s->ap = a;
            y = a;
        }
        x = exc_in[i] * 0.3f;
        if (model == 2) {                      /* blown: a reed (STK's clarinet) */
            float br, pd, r;
            s->bow += ((v->a.gate ? (0.62f + exc * 0.45f) * (0.75f + 0.25f * v->vel) : 0.0f) - s->bow) * 0.001f;   /* above the reed's threshold */
            br = s->bow * (1.0f + xs_bi(&s->ns) * 0.04f);   /* breath, with a little turbulence */
            pd = -0.95f * y - br;                     /* the bore reflects inverted */
            r = clampf(0.7f - 0.3f * pd, -1.0f, 1.0f);    /* the reed opens and closes */
            x += br + pd * r - y;                     /* the reed sets what goes back in */
        }
        y += x;
        s->line[s->w] = (int16_t)clampf(y * 16000.0f, -32760.0f, 32760.0f);
        s->w = (s->w + 1) % RV_KS_LEN;
        /* body: a little low resonance mixed in */
        s->dly += (y - s->dly) * 0.02f;
        out[i] = (y + body * s->dly * 3.0f) * 0.8f;
    }
}

/* ----------------------------------------------------------- FORMANT --- */
static const float vowels[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
static const float vowel_gain[3] = {1.0f, 0.55f, 0.3f};

static void form_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    form_t *s = &v->e.fo;
    float vw = eff[TP_S0] * (4.0f / 127.0f), sh = fm_exp2f(eff[TP_S1] * (1.0f / 64.0f));
    float bw = 0.4f + eff[TP_S2] * (8.0f / 127.0f);
    int fof = eff[TP_S3] > 0.5f, vi = clampi((int)vw, 0, 3), i, k;
    float vf = vw - (float)vi, breath = eff[TP_S4] * (1.0f / 127.0f), glot = eff[TP_S5] * (1.0f / 127.0f);
    float spread = eff[TP_S6] * (1.0f / 127.0f), vib = eff[TP_S7] * (1.0f / 127.0f);
    float F[3], dt;
    for (k = 0; k < 3; k++)
        F[k] = lerpf(vowels[vi][k], vowels[vi + 1][k], vf) * sh;
    s->vib += 5.5f * RV_DT * n;
    s->vib -= fm_floorf(s->vib);
    f *= 1.0f + vib * 0.012f * sin_lut(s->vib) * clampf(v->a.out * 2.0f, 0.0f, 1.0f);
    dt = f * RV_DT;
    if (!fof)
        for (k = 0; k < 3; k++)
            svf_set(&s->f[k], F[k], F[k] / (60.0f * bw + 30.0f));
    for (i = 0; i < n; i++) {
        float y = 0.0f, src;
        s->ph += dt;
        if (s->ph >= 1.0f) {
            s->ph -= 1.0f;
            if (fof)
                for (k = 0; k < 3; k++) {
                    s->fof_ph[k] = 0.0f;
                    s->fof_env[k] = 1.0f;
                    s->fof_t[k] = 0.0f;
                }
        }
        if (fof) {
            for (k = 0; k < 3; k++) {
                if (s->fof_env[k] < 0.0005f)
                    continue;
                {
                    float att = s->fof_t[k] < 0.002f ? 0.5f - 0.5f * sin_lut(s->fof_t[k] * 250.0f + 0.25f) : 1.0f;
                    y += sin_lut(s->fof_ph[k]) * s->fof_env[k] * att * vowel_gain[k];
                    s->fof_ph[k] += F[k] * (1.0f + spread * 0.02f * (k - 1)) * RV_DT;
                    s->fof_env[k] *= 1.0f - (0.0015f + bw * 0.0012f) * (1.0f + k * 0.4f);
                    s->fof_t[k] += RV_DT;
                }
            }
            y += breath * xs_bi(&s->ns) * 0.2f;
            out[i] = y * 0.45f;
        } else {
            /* a glottal pulse: raised cosine open phase, sharper with GLOTTAL */
            float op = 0.4f + (1.0f - glot) * 0.5f;
            src = s->ph < op ? (1.0f - sin_lut(s->ph / op * 0.5f + 0.25f)) : 0.0f;
            src = (src - 0.4f) * (0.5f + glot);
            src += breath * xs_bi(&s->ns) * 0.6f;
            for (k = 0; k < 3; k++) {
                float bp;
                svf_tick(&s->f[k], src, &bp, 0);
                y += bp * vowel_gain[k];
            }
            out[i] = y * 0.9f;
        }
    }
}

/* ---------------------------------------------------------- PARTICLE --- */
/* Silt's pulsar emitters: each emitter fires a short wavelet (a pulsaret) once per period;
 * coupling pulls the emitters' phases together (Kuramoto), mask leaves pulses out by a fixed
 * rule, scatter (optional) jitters them. */
static void part_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    part_t *s = &v->e.pa;
    const int16_t *tab = wt[clampi((int)(eff[TP_S0] + 0.5f), 0, 7)];
    float duty = 0.04f + eff[TP_S1] * (0.92f / 127.0f);
    int ne = clampi((int)(eff[TP_S2] + 0.5f), 1, 3), i, e;
    float ratio = eff[TP_S3] * (1.0f / 127.0f), couple = eff[TP_S4] * (1.0f / 127.0f);
    float mask = eff[TP_S5] * (1.0f / 127.0f), form = fm_exp2f((eff[TP_S6] - 64.0f) * (3.0f / 64.0f));
    float scat = eff[TP_S7] * (1.0f / 127.0f);
    float inc[3], keep = 1.0f - mask * 0.85f;
    inc[0] = f * RV_DT;
    inc[1] = inc[0] * (1.0f + ratio * 0.5f);
    inc[2] = inc[0] * (1.0f + ratio * 1.25f);
    for (i = 0; i < n; i++) {
        float y = 0.0f, cx = 0.0f, sy = 0.0f, psi = 0.0f, R = 0.0f;
        if (ne > 1 && couple > 0.001f) {
            for (e = 0; e < ne; e++) {
                cx += sin_lut(s->th[e] + 0.25f);
                sy += sin_lut(s->th[e]);
            }
            R = fm_sqrtf(cx * cx + sy * sy) / (float)ne;
            /* mean phase in turns, from atan2 by a cheap octant fit */
            {
                float ax = fm_fabsf(cx), ay = fm_fabsf(sy), mn = fm_minf(ax, ay), mx = fm_maxf(ax, ay) + 1e-9f, a = mn / mx;
                float r = a * (0.15915f - 0.0466f * a);     /* atan(a)/2pi, to 0.3 degrees */
                if (ay > ax)
                    r = 0.25f - r;
                if (cx < 0.0f)
                    r = 0.5f - r;
                if (sy < 0.0f)
                    r = -r;
                psi = r;
            }
        }
        for (e = 0; e < ne; e++) {
            float ie = inc[e];
            if (ne > 1 && couple > 0.001f)
                ie += couple * R * 0.02f * inc[0] * 30.0f * sin_lut(psi - s->th[e]);
            s->th[e] += ie;
            if (s->th[e] >= 1.0f) {
                s->th[e] -= fm_floorf(s->th[e]);
                s->count++;
                /* deterministic mask: keep a pulse when the running count crosses an integer */
                if ((int)((float)(s->count + 1) * keep) != (int)((float)s->count * keep) || mask < 0.01f) {
                    s->on[e] = 1;
                    s->rd[e] = 0.0f;
                    if (scat > 0.001f)
                        s->th[e] -= scat * xs_uni(&s->ns) * 0.4f;
                }
            }
            if (s->on[e]) {
                float u = s->rd[e];                     /* through the pulsaret, 0..1 */
                float w = 0.5f - 0.5f * sin_lut(u + 0.25f);
                y += wt_read(tab, u * duty * form * 4.0f + 0.0f) * w * (e == 0 ? 1.0f : 0.7f);
                s->rd[e] += ie / duty;
                if (s->rd[e] >= 1.0f)
                    s->on[e] = 0;
            }
        }
        out[i] = y * 0.55f;
    }
}

/* -------------------------------------------------------------- CHIP --- */
/* After the Syntakt's SY CHIP: a SID-style oscillator (triangle, saw, pulse, the noise shift
 * register, the combined saw&pulse wave, ring, animated PWM and two drum tables) that steps
 * through the played note and up to three offsets (OF2-OF4) at RATE, so one voice plays a
 * chord. A square sub oscillator holds the played note (octaves -3..+1), and the main oscillator
 * can restart with it (SYNC: the arp's notes then sweep a synced timbre). OVER drives it all.
 * Then a decompose stage:
 * sample-rate reduction, bit crushing, and fragments of the voice's own last moments repeated,
 * reversed, dropped, bit-flipped or doubled. */
static void chip_note(voice_t *v, const float *eff)
{
    chip_t *c = &v->e.ch;
    (void)eff;
    c->step = 0;
    c->dir = 1;
    c->acc = 0.0f;
    c->sweep = 1.0f;
    c->vib_t = 0.0f;
    if (!c->lfso)
        c->lfso = 0x7FFFF8u;                          /* the SID's noise register starts all ones */
    if (!c->ns)
        c->ns = v->rng | 1u;
    c->frag_left = 0;
    c->frag_mode = 0;
}

static inline float chip_wave(chip_t *c, int w, float ph, float pw)
{
    switch (w) {
    case CW_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case CW_SAW: return 2.0f * ph - 1.0f;
    case CW_PULSE: case CW_PWM: return ph < pw ? 1.0f : -1.0f;
    case CW_NOISE: return c->noise;
    case CW_SAWPUL: {                                 /* the SID ANDs the two waves' bits */
        int a = (int)(ph * 4095.0f), b = ph < pw ? 4095 : 0;
        return (float)(a & b) * (2.0f / 4095.0f) - 1.0f;
    }
    default: return 0.0f;
    }
}

static void chip_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    chip_t *c = &v->e.ch;
    int w = clampi((int)(eff[TP_S0] + 0.5f), 0, CW_N - 1), mode = clampi((int)(eff[TP_S3] + 0.5f), 0, 4);
    int steps = clampi((int)(eff[TP_S7] + 0.5f), 1, 4), i;
    float pw = 0.05f + eff[TP_S1] * (0.9f / 127.0f), spd = 2.0f * fm_exp2f(eff[TP_S2] * (6.0f / 127.0f));
    float ofs[4] = {0.0f, eff[TP_S4], eff[TP_S5], eff[TP_S6]};
    float sub = eff[TP_S8] * (1.0f / 127.0f), over = eff[TP_S11] * (1.0f / 127.0f);
    int sync = eff[TP_S10] > 0.5f;
    float sdt = clampf(f * fm_exp2f((float)(clampi((int)(eff[TP_S9] + 0.5f), 0, 4) - 3)) * RV_DT, 0.0f, 0.45f);
    float srr = eff[TP_S12] * (1.0f / 127.0f), hold_n = 1.0f + srr * srr * 63.0f;
    float q = (float)(1 << (clampi((int)(eff[TP_S13] + 0.5f), 1, 16) - 1));
    float dec = eff[TP_S14] * (1.0f / 127.0f);
    int flen = 64 + (int)(eff[TP_S15] * ((RV_CHIP_BUF - 80) / 127.0f));
    float semi, dt, dt2, nclk, drum = 0.0f;
    if ((uint32_t)c->w >= RV_CHIP_BUF)                /* shared memory: never trust a stale index */
        c->w = 0;
    /* the arpeggio: advance once per block (a step every 1/SPEED s) */
    if (steps > 1) {
        c->acc += spd * (float)n * RV_DT;
        while (c->acc >= 1.0f) {
            c->acc -= 1.0f;
            switch (mode) {
            case 0: c->step = (int8_t)((c->step + 1) % steps); break;
            case 1: c->step = (int8_t)((c->step + steps - 1) % steps); break;
            case 2:
                if (c->step + c->dir >= steps || c->step + c->dir < 0)
                    c->dir = (int8_t)-c->dir;
                c->step = (int8_t)(c->step + c->dir);
                break;
            case 3: c->step = (int8_t)(xs_uni(&c->ns) * (float)steps * 0.9999f); break;
            default: if (c->step < steps - 1) c->step++; break;   /* ONCE: up, then stays */
            }
        }
        c->step = (int8_t)clampi(c->step, 0, steps - 1);
    } else {
        c->step = 0;
    }
    /* pitch: the arp's offset, and KICK's own fast drop */
    c->sweep *= fm_expf(-(float)n * RV_DT / (w == CW_KICK ? 0.035f : 0.06f));
    c->vib_t += (float)n * RV_DT;
    semi = ofs[c->step] + (w == CW_KICK ? 36.0f * c->sweep : 0.0f);
    dt = clampf(f * fm_exp2f(semi * (1.0f / 12.0f)) * RV_DT, 0.0f, 0.45f);
    dt2 = dt * (1.0f + eff[TP_S1] * (3.0f / 127.0f));   /* RING: PW is the partner's ratio */
    nclk = dt * 16.0f;                                /* the noise register clocks 16 times a cycle */
    for (i = 0; i < n; i++) {
        float x, y, sq;
        c->ph += dt;
        if (c->ph >= 1.0f)
            c->ph -= 1.0f;
        c->pwm2 += sdt;                               /* the sub: a square on the played note */
        if (c->pwm2 >= 1.0f) {
            c->pwm2 -= 1.0f;
            if (sync)                                 /* SYNC: the main oscillator restarts with it */
                c->ph = c->pwm2 * (dt / sdt);
        }
        sq = c->pwm2 < 0.5f ? 1.0f : -1.0f;
        c->drift += nclk;
        while (c->drift >= 1.0f) {                    /* the SID's 23-bit noise register */
            uint32_t b = ((c->lfso >> 22) ^ (c->lfso >> 17)) & 1u;
            c->drift -= 1.0f;
            c->lfso = ((c->lfso << 1) | b) & 0x7FFFFFu;
            c->noise = (float)(((c->lfso >> 15) & 0xFFu)) * (2.0f / 255.0f) - 1.0f;
        }
        switch (w) {
        case CW_RING: {                               /* triangle, its sign flipped by a partner */
            c->ph2 += dt2;
            if (c->ph2 >= 1.0f)
                c->ph2 -= 1.0f;
            x = chip_wave(c, CW_TRI, c->ph, pw) * (c->ph2 < 0.5f ? 1.0f : -1.0f);
            break;
        }
        case CW_PWM:
            c->pwm += 2.2f * RV_DT;
            if (c->pwm >= 1.0f)
                c->pwm -= 1.0f;
            x = chip_wave(c, CW_PULSE, c->ph, 0.5f + (pw - 0.05f) * 0.5f * sin_lut(c->pwm));
            break;
        case CW_KICK:                                 /* a noise click, then a falling triangle */
            drum = c->sweep;
            x = c->vib_t < 0.004f ? c->noise : chip_wave(c, CW_TRI, c->ph, pw);
            break;
        case CW_SNARE:                                /* a triangle body under noise that outlasts it */
            drum = c->sweep;
            x = chip_wave(c, CW_TRI, c->ph, pw) * drum + c->noise * 0.7f;
            break;
        default: x = chip_wave(c, w, c->ph, pw); break;
        }
        x = (x + sq * sub * 0.8f) * (0.5f / (1.0f + sub * 0.4f));
        if (over > 0.0f)                              /* OVER: driven into a soft wall */
            x = sat(x * (1.0f + over * 7.0f)) * (0.5f + 0.2f * over) / (0.5f + over * 0.3f);
        /* SRR: hold each sample HOLD times (fractional, so the aliasing moves with it) */
        c->hold_n += 1.0f;
        if (c->hold_n >= hold_n) {
            c->hold_n -= hold_n;
            c->hold = x;
        }
        x = hold_n > 1.01f ? c->hold : x;
        if (q < 32768.0f)                             /* CRUSH: down to so many bits */
            x = fm_floorf(x * q + 0.5f) / q;
        /* DECOMP: the voice remembers its last 46 ms; at each fragment's end the dice may
         * replay the last fragment, reverse it, drop it, flip its bits or play it twice as fast */
        c->buf[c->w] = (int16_t)clampf(x * 16000.0f, -32000.0f, 32000.0f);
        y = x;
        if (dec > 0.0f) {
            if (--c->frag_left <= 0) {
                float r = xs_uni(&c->ns);
                c->frag_len = flen;
                c->frag_left = flen;
                c->frag_pos = 0;
                c->frag_mode = r < dec ? (uint8_t)(1 + (int)(xs_uni(&c->ns) * 4.999f)) : 0;
                c->flip = (uint16_t)(xs_uni(&c->ns) * 65535.0f) | 0x2000u;
            }
            if (c->frag_mode) {
                int32_t k = c->frag_pos++, r0;
                switch (c->frag_mode) {
                case 1: r0 = c->w - c->frag_len + (k % c->frag_len); break;            /* repeat */
                case 2: r0 = c->w - 2 * k - 1; break;                                     /* reverse */
                case 3: r0 = -1; break;                                                   /* drop */
                case 4: r0 = c->w; break;                                                 /* bit flip */
                default: r0 = c->w - c->frag_len + ((2 * k) % c->frag_len); break;       /* twice as fast */
                }
                if (r0 < 0 && c->frag_mode != 3)
                    r0 += RV_CHIP_BUF * 2;
                if (c->frag_mode == 3)
                    y = 0.0f;
                else if (c->frag_mode == 4)
                    y = (float)((int16_t)(c->buf[c->w] ^ (int16_t)(c->flip & 0x3FF0u))) * (1.0f / 16000.0f);
                else
                    y = (float)c->buf[r0 % RV_CHIP_BUF] * (1.0f / 16000.0f);
            }
        }
        c->w = (c->w + 1) % RV_CHIP_BUF;
        out[i] = y;
    }
    (void)drum;
}

/* --------------------------------------------------- the synth voice --- */
/* clear a voice's engine state for eng. The engines share one block of memory (a union), so only
 * eng's own fields are seeded: writing another engine's fields would land on eng's phases and
 * filter states as garbage numbers. */
/* ----------------------------------------------------------------- SQ --- */
/* After the Ensoniq SQ-80. Three oscillators read 8-bit single cycles (Ravel's own waves in the
 * SQ's families: saws, bells, piano, voices, formants, organ, reed, digital) the way its DOC
 * chip did. Osc 1 can hard-sync osc 2 and/or amplitude-modulate it (the DOC's sync and AM
 * pairs). ENV 2 (attack, decay to E2 SUS, release) is the filter's envelope; its pure decay
 * bends the pitch (P ENV) and lifts osc 3 for a struck or breathy onset (TRANS). VIBRATO is
 * the SQ's LFO with its delay: it fades in over half a second. GRIT is the hardware: at 0 the
 * waves are band-limited and interpolated; rising, the band limit opens until the raw cycle
 * aliases, the read drops to the nearest sample, then samples are held (the DOC's own slower
 * rate) and the output DAC coarsens. The track's ADSR is the SQ's ENV 4 (the level). */
#define SQ_WAVE_DATA
#include "rv_sqwaves.h"

static void sq_note(voice_t *v, const float *eff)
{
    sq_t *s = &v->e.sq;
    s->e2 = s->e2d = 1.0f;
    s->vib_t = 0.0f;
    if (!s->ns)
        s->ns = v->rng | 1u;
    if (((int)(eff[TP_S8] + 0.5f)) & SQM_SYNC)       /* sync: the pair starts together */
        s->ph[0] = s->ph[1] = 0.0f;
}

static inline float sq_read(const int8_t *tab, float ph, float smooth)
{
    int i = (int)ph;
    float a = (float)tab[i & 255], b = (float)tab[(i + 1) & 255];
    return (a + (b - a) * (ph - (float)i) * smooth) * (1.0f / 127.0f);
}

static inline int sq_mip(float hz, float open)
{
    float room = 0.45f * (float)RV_FS / (hz > 1.0f ? hz : 1.0f) * open;   /* harmonics that fit */
    int k;
    for (k = 0; k < SQ_NMIPS - 1; k++)
        if ((float)sq_mip_caps[k] <= room)
            return k;
    return SQ_NMIPS - 1;
}

static void sq_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    sq_t *s = &v->e.sq;
    int w[3], mode = clampi((int)(eff[TP_S8] + 0.5f), 0, 3), i, k;
    float grit = clampf(eff[TP_S15], 0.0f, 127.0f), det = eff[TP_S5] * (0.25f / 127.0f);   /* up to a quarter tone */
    float l2 = eff[TP_S6] * (1.0f / 127.0f), l3 = eff[TP_S7] * (1.0f / 127.0f), trans = eff[TP_S12] * (1.2f / 127.0f);
    float dec = 0.004f * fm_exp2f(eff[TP_S9] * (10.5f / 127.0f)), sus = eff[TP_S10] * (1.0f / 127.0f);
    float vib = eff[TP_S13] * (0.8f / 127.0f), vrate = 0.5f * fm_exp2f(eff[TP_S14] * (4.5f / 127.0f));
    float dt = (float)n * RV_DT, semi, hz[3], inc[3], smooth, open, hold, q, l3e, norm, vt;
    const int8_t *tab[3];
    w[0] = clampi((int)(eff[TP_S0] + 0.5f), 0, SQ_NWAVES - 1);
    w[1] = clampi((int)(eff[TP_S1] + 0.5f), 0, SQ_NWAVES - 1);
    w[2] = clampi((int)(eff[TP_S2] + 0.5f), 0, SQ_NWAVES - 1);
    /* ENV 2 and its decay: per block */
    if (v->gate) {
        s->e2 = sus + (s->e2 - sus) * fm_expf(-dt / dec);
    } else {
        s->e2 *= fm_expf(-dt / (dec * 0.7f + 0.01f));
    }
    s->e2d *= fm_expf(-dt / dec);
    /* the delayed vibrato: a triangle fading in over its first half second */
    s->vib_t += dt;
    s->vib_ph += vrate * dt;
    s->vib_ph -= fm_floorf(s->vib_ph);
    vt = s->vib_ph < 0.5f ? 4.0f * s->vib_ph - 1.0f : 3.0f - 4.0f * s->vib_ph;
    semi = eff[TP_S11] * (24.0f / 64.0f) * s->e2d + vib * vt * clampf(s->vib_t * 2.0f, 0.0f, 1.0f);
    hz[0] = f * fm_exp2f(semi * (1.0f / 12.0f));
    hz[1] = f * fm_exp2f((semi + eff[TP_S3] + det) * (1.0f / 12.0f));
    hz[2] = f * fm_exp2f((semi + eff[TP_S4] - det) * (1.0f / 12.0f));
    /* GRIT: 0-64 opens the band limit and fades out interpolation; 64-127 holds samples and
     * coarsens the DAC */
    open = fm_exp2f(grit * (1.0f / 16.0f));
    smooth = clampf(1.0f - grit * (1.0f / 64.0f), 0.0f, 1.0f);
    hold = grit > 64.0f ? fm_exp2f(-(grit - 64.0f) * (1.4f / 63.0f)) : 1.0f;   /* new samples per output sample */
    q = grit > 64.0f ? fm_exp2f(15.0f - (grit - 64.0f) * (6.0f / 63.0f)) : 0.0f;   /* DAC steps: 15 bits down to 9 */
    for (k = 0; k < 3; k++) {
        hz[k] = clampf(hz[k], 1.0f, 18000.0f);
        inc[k] = hz[k] * RV_DT * 256.0f;
        tab[k] = sq_waves[w[k]][sq_mip(hz[k], open)];
    }
    l3e = clampf(l3 + trans * s->e2d, 0.0f, 1.5f);
    norm = 1.0f / (1.0f + 0.55f * (l2 + l3e));
    for (i = 0; i < n; i++) {
        float x;
        s->hold_acc += hold;
        if (s->hold_acc >= 1.0f) {                    /* a new sample from the oscillators */
            float a, b, c;
            s->hold_acc -= 1.0f;
            a = sq_read(tab[0], s->ph[0], smooth);
            b = sq_read(tab[1], s->ph[1], smooth);
            if (mode & 2)                             /* AM: osc 1's level opens osc 2 */
                b *= 0.5f + 0.5f * a;
            c = l3e > 0.0005f ? sq_read(tab[2], s->ph[2], smooth) : 0.0f;
            x = (a + l2 * b + l3e * c) * norm;
            if (q > 0.0f)
                x = fm_floorf(x * q + 0.5f) / q;
            s->hold_y = x;
        }
        for (k = 0; k < 3; k++)
            s->ph[k] += inc[k];
        if (s->ph[0] >= 256.0f) {
            s->ph[0] -= 256.0f;
            if (mode & 1)                             /* SYNC: osc 2 restarts with osc 1 */
                s->ph[1] = s->ph[0] * (inc[1] / (inc[0] > 1e-6f ? inc[0] : 1e-6f));
        }
        for (k = 1; k < 3; k++)
            while (s->ph[k] >= 256.0f)
                s->ph[k] -= 256.0f;
        out[i] = s->hold_y * 0.55f;
    }
}

/* --------------------------------------------------------------- WAVE --- */
/* After the PPG Wave and the Waldorf Microwave. Two oscillators read wavetables: each table is
 * 8 key waves and POS (0-63) moves between them. Osc 1 can hard-sync osc 2, ring-modulate it or
 * frequency-modulate it (XMOD, by XM AMT). Both read through a phase WARP (after Serum's: SYNC,
 * BEND, PWM, MIRROR, REMAP 2/4, SQUEEZE, FLIP, QUANT). MIX balances them into a mixer that
 * saturates prettily as SAT rises. The WAVE ENV is a shape from the SHAPE library that sweeps both
 * positions (by WE AMT), looping while the key is held, as the Microwave's wave envelope loops.
 * ASIC: at 0 the positions glide and the waves are band-limited; up to 63 the positions step
 * through 64 waves as the PPG and Microwave did; above, the samples are read raw and unsmoothed,
 * the phase is truncated and the output coarsens: the early Microwave's digital sizzle. */
#define WT_DATA
#include "rv_wavetables.h"

static void wave_note(voice_t *v, const float *eff)
{
    wave_t *w = &v->e.wv;
    w->wu = 0.0f;
    w->wdone = 0;
    if (((int)(eff[TP_S8] + 0.5f) == WX_SYNC) || ((int)(eff[TP_S8] + 0.5f) == WX_SYNCFM))
        w->ph[0] = w->ph[1] = 0.0f;
}

/* one osc's sample: table t, key position kp (0..7), phase p (0..1); lvl 0 full, 1 limited, or a mix */
static inline float wtab_read(int t, float kp, float p, float lim, int raw)
{
    int k0 = (int)kp, k1, i0, i1;
    float kf = kp - (float)k0, x = p * (float)WT_N, fr, a, b;
    const int8_t *f0, *f1;
    if (k0 >= WT_KEYS - 1) {
        k0 = WT_KEYS - 1;
        kf = 0.0f;
    }
    k1 = k0 + (k0 < WT_KEYS - 1);
    i0 = (int)x & (WT_N - 1);
    i1 = (i0 + 1) & (WT_N - 1);
    fr = raw ? 0.0f : x - fm_floorf(x);
    f0 = wt_tab[t][0][k0];
    f1 = wt_tab[t][0][k1];
    a = f0[i0] + (f0[i1] - f0[i0]) * fr;
    b = f1[i0] + (f1[i1] - f1[i0]) * fr;
    a += (b - a) * kf;
    if (lim > 0.001f) {                               /* high notes: toward the 10-harmonic copy */
        float c, d;
        f0 = wt_tab[t][1][k0];
        f1 = wt_tab[t][1][k1];
        c = f0[i0] + (f0[i1] - f0[i0]) * fr;
        d = f1[i0] + (f1[i1] - f1[i0]) * fr;
        c += (d - c) * kf;
        a += (c - a) * lim;
    }
    return a * (1.0f / 127.0f);
}

static inline float wtab_warp(int warp, float a, float p)
{
    switch (warp) {
    case WW_SYNC: { float q = p * (1.0f + a * 7.0f); return q - fm_floorf(q); }
    case WW_BEND: return fm_powf(p > 1e-6f ? p : 1e-6f, fm_exp2f((0.5f - a) * 4.0f));   /* bipolar around the middle */
    case WW_PWM: { float wd = 0.5f - a * 0.48f; return p < wd ? p * 0.5f / wd : 0.5f + (p - wd) * 0.5f / (1.0f - wd); }
    case WW_MIRROR: { float m = p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f; return p + (m - p) * a; }
    case WW_SQUEEZE: {
        float e = 1.0f + a * 3.0f;
        return p < 0.5f ? 0.5f * fm_powf(p * 2.0f, e) : 1.0f - 0.5f * fm_powf(2.0f - p * 2.0f, e);
    }
    case WW_QUANT: { float st = fm_exp2f(7.0f - a * 5.5f); return fm_floorf(p * st) / st; }
    default: return p;
    }
}

/* one oscillator through its warp: REMAP and FLIP work on the output */
static inline float wtab_osc(int t, float kp, float p, int warp, float wa, float lim, int raw)
{
    float y;
    if (warp == WW_REMAP2 || warp == WW_REMAP4) {      /* the cycle read 2 or 4 times a period, blended in */
        float m = warp == WW_REMAP2 ? 2.0f : 4.0f, q = p * m;
        y = wtab_read(t, kp, p, lim, raw);
        return y + (wtab_read(t, kp, q - fm_floorf(q), lim, raw) - y) * wa;
    }
    y = wtab_read(t, kp, wtab_warp(warp, wa, p), lim, raw);
    if (warp == WW_FLIP && p > 1.0f - wa * 0.5f)       /* FLIP: the end of the cycle upside down */
        y = -y;
    return y;
}

static void wave_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    wave_t *w = &v->e.wv;
    int t1 = clampi((int)(eff[TP_S0] + 0.5f), 0, WT_TABLES - 1), t2 = clampi((int)(eff[TP_S2] + 0.5f), 0, WT_TABLES - 1);
    int xm = clampi((int)(eff[TP_S8] + 0.5f), 0, 4), warp = clampi((int)(eff[TP_S10] + 0.5f), 0, WW_N - 1);
    int wenv = clampi((int)(eff[TP_S12] + 0.5f), 0, SHP_N), i, raw;
    float asic = clampf(eff[TP_S15], 0.0f, 127.0f), mix = eff[TP_S6] * (1.0f / 127.0f), satv = eff[TP_S7] * (1.0f / 127.0f);
    float xa = eff[TP_S9] * (1.0f / 127.0f), wa = eff[TP_S11] * (1.0f / 127.0f), det = eff[TP_S5] * (0.3f / 127.0f);
    float f2 = f * fm_exp2f((eff[TP_S4] + det) * (1.0f / 12.0f)), d1, d2, p1, p2, kp1, kp2, l1, l2, g1, g2, drive, q;
    /* the wave envelope: a shape that loops while held, then finishes its cycle */
    if (wenv > 0 && !w->wdone) {
        w->wu += (float)n * RV_DT / rv_time_f(eff[TP_S14]);
        if (w->wu >= 1.0f) {
            if (v->gate)
                w->wu -= fm_floorf(w->wu);
            else {
                w->wu = 1.0f;
                w->wdone = 1;
            }
        }
        w->wv = shp_eval(wenv, 0.0f, 0.0f, w->wu);
    } else if (!wenv) {
        w->wv = 0.0f;
    }
    p1 = clampf(eff[TP_S1] + eff[TP_S13] * (63.0f / 64.0f) * w->wv, 0.0f, 63.0f);
    p2 = clampf(eff[TP_S3] + eff[TP_S13] * (63.0f / 64.0f) * w->wv, 0.0f, 63.0f);
    if (asic >= 1.0f) {                                /* the PPG's stepping: 64 whole waves */
        p1 = fm_floorf(p1 + 0.5f);
        p2 = fm_floorf(p2 + 0.5f);
    }
    kp1 = p1 * (7.0f / 63.0f);
    kp2 = p2 * (7.0f / 63.0f);
    raw = asic >= 64.0f;
    /* band limit (smooth only): the share of the 10-harmonic copy each osc needs */
    l1 = raw ? 0.0f : clampf((63.0f - 0.45f * (float)RV_FS / (f > 1.0f ? f : 1.0f)) / 53.0f, 0.0f, 1.0f);
    l2 = raw ? 0.0f : clampf((63.0f - 0.45f * (float)RV_FS / (f2 > 1.0f ? f2 : 1.0f)) / 53.0f, 0.0f, 1.0f);
    d1 = clampf(f * RV_DT, 0.0f, 0.45f);
    d2 = clampf(f2 * RV_DT, 0.0f, 0.45f);
    g1 = fm_cosf(mix * 1.5707963f);                  /* equal-power MIX */
    g2 = fm_sinf(mix * 1.5707963f);
    if (xm == WX_OFF && mix < 0.002f)
        g2 = 0.0f;
    drive = 1.0f + satv * 5.0f;
    q = raw ? fm_exp2f(12.0f - (asic - 64.0f) * (4.0f / 63.0f)) : 0.0f;   /* the output: 12 bits down to 8 */
    for (i = 0; i < n; i++) {
        float y1, y2, ph1 = w->ph[0], ph2, y;
        if (raw) {                                     /* the phase truncated: a jitter that sizzles */
            float tr = fm_exp2f(9.0f + (127.0f - asic) * (4.0f / 63.0f));
            ph1 = fm_floorf(ph1 * tr) / tr;
        }
        y1 = wtab_osc(t1, kp1, ph1, warp, wa, l1, raw);
        ph2 = w->ph[1];
        if (xm == WX_FM || xm == WX_SYNCFM) {          /* osc 1 bends osc 2's phase */
            ph2 += xa * 1.5f * y1;
            ph2 -= fm_floorf(ph2);
        }
        y2 = g2 > 0.0f || xm == WX_RING ? wtab_osc(t2, kp2, ph2, warp, wa, l2, raw) : 0.0f;
        if (xm == WX_RING)
            y2 += (y1 * y2 - y2) * xa;
        y = y1 * g1 + y2 * (g2 > 0.0f ? g2 : (xm == WX_RING ? 0.7f : 0.0f));
        y = sat(y * drive) * (1.0f / sat(drive));       /* the mixer: rounds off as it is pushed */
        if (q > 0.0f)
            y = fm_floorf(y * q + 0.5f) / q;
        out[i] = y * 0.5f;
        w->ph[0] += d1;
        if (w->ph[0] >= 1.0f) {
            w->ph[0] -= 1.0f;
            if (xm == WX_SYNC || xm == WX_SYNCFM)       /* hard sync: osc 2 restarts with osc 1 */
                w->ph[1] = w->ph[0] * (d2 / (d1 > 1e-7f ? d1 : 1e-7f));
        }
        w->ph[1] += d2;
        w->ph[1] -= fm_floorf(w->ph[1]);
    }
}

/* -------------------------------------------------------------- LOGIC --- */
/* Oscillators from function blocks, after Kurzweil's VAST (whose FUNs compute a function of two
 * inputs) and E-mu's patch-cord functions. Three sources: A on the note, B and C at a ratio of it
 * (B DRIFT pulls B off the ratio, so the pattern slowly turns). OP 1 combines A and B, OP 2 that
 * result with C (OP 2 AMT blends it in): arithmetic (A+B, A*B, MIN, MAX, DIFF), bit logic on BITS-
 * bit words (AND, OR, XOR, NAND), a comparator (A>B), GATE (A where B is high), QUANT (A in steps B
 * sets), MOD (A wrapped by B), SIN(A+B), and A(B) (A's wave read at B). Run at twice the rate and
 * smoothed (SMOOTH) to tame the edges. Then GRAIN: pitch-synchronous grains of the voice's own last
 * moments, GRAIN long (short: pulsar-like gaps; long: overlapping clouds), sprayed and transposed. */
static inline float lg_src(int wv, float ph, float pw, uint32_t *ns)
{
    switch (wv) {
    case LW_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    case LW_SQR: return ph < 0.5f ? 1.0f : -1.0f;
    case LW_SINE: return fm_sin_turns(ph);
    case LW_PULSE: return ph < pw ? 1.0f : -1.0f;
    case LW_NOISE: return xs_bi(ns);
    default: return 2.0f * ph - 1.0f;
    }
}

static inline float lg_op(int op, float x, float y, int bits, int wa, float pw, uint32_t *ns)
{
    float top = (float)((1 << bits) - 1);
    int ux, uy;
    switch (op) {
    case LO_ADD: return (x + y) * 0.5f;
    case LO_MULT: return x * y;
    case LO_MIN: return x < y ? x : y;
    case LO_MAX: return x > y ? x : y;
    case LO_AND: case LO_OR: case LO_XOR: case LO_NAND: {
        int r;
        ux = (int)((x * 0.5f + 0.5f) * top + 0.5f);
        uy = (int)((y * 0.5f + 0.5f) * top + 0.5f);
        r = op == LO_AND ? (ux & uy) : op == LO_OR ? (ux | uy) : op == LO_XOR ? (ux ^ uy) : ~(ux & uy) & ((1 << bits) - 1);
        return (float)r * (2.0f / top) - 1.0f;
    }
    case LO_COMP: return x > y ? 1.0f : -1.0f;
    case LO_GATE: return y > 0.0f ? x : 0.0f;
    case LO_DIFF: return fm_fabsf(x - y) - 1.0f;
    case LO_QUANT: {
        float st = 1.0f + (y * 0.5f + 0.5f) * top;
        return fm_floorf(x * st + 0.5f) / st;
    }
    case LO_MOD: {
        float m = 0.1f + (y * 0.5f + 0.5f) * 1.9f, u = x + 1.0f;
        return (u - fm_floorf(u / m) * m) * (2.0f / m) - 1.0f;
    }
    case LO_SINE: return fm_sin_turns((x + y) * 0.5f);
    default: return lg_src(wa, y * 0.5f + 0.5f, pw, ns);   /* A(B): A's wave at B */
    }
}

static const float lg_ratio[20] = {0.25f, 0.3333333f, 0.5f, 0.6666667f, 0.75f, 1.0f, 1.25f, 1.3333333f, 1.5f, 1.6666667f,
                                   2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 11.0f, 16.0f};

static void logic_note(voice_t *v, const float *eff)
{
    logic_t *l = &v->e.lg;
    (void)eff;
    l->ph[0] = l->ph[1] = l->ph[2] = 0.0f;           /* the pattern starts the same each note */
    if (!l->ns)
        l->ns = v->rng | 1u;
}

static void logic_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    logic_t *l = &v->e.lg;
    int wa = clampi((int)(eff[TP_S0] + 0.5f), 0, LW_N - 1), wb = clampi((int)(eff[TP_S2] + 0.5f), 0, LW_N - 1);
    int wc = clampi((int)(eff[TP_S4] + 0.5f), 0, LW_N - 1), op1 = clampi((int)(eff[TP_S5] + 0.5f), 0, LO_N - 1);
    int op2 = clampi((int)(eff[TP_S6] + 0.5f), 0, LO_N - 1), bits = clampi((int)(eff[TP_S8] + 0.5f), 1, 12), i, s, k;
    float rb = lg_ratio[clampi((int)(eff[TP_S1] + 0.5f), 0, 19)] * fm_exp2f(eff[TP_S10] * (30.0f / 127.0f / 1200.0f));   /* B DRIFT: up to 30 cents off */
    float rc = lg_ratio[clampi((int)(eff[TP_S3] + 0.5f), 0, 19)], amt2 = eff[TP_S7] * (1.0f / 127.0f);
    float pw = 0.05f + eff[TP_S9] * (0.9f / 127.0f), dry = eff[TP_S11] * (1.0f / 127.0f);
    float sm = 1.0f - 0.97f * eff[TP_S12] * (1.0f / 127.0f), grain = eff[TP_S13] * (1.0f / 127.0f);
    float spray = eff[TP_S14] * (1.0f / 127.0f), gp = fm_exp2f(eff[TP_S15] * (1.0f / 12.0f));
    float dt = clampf(f * RV_DT * 0.5f, 0.0f, 0.24f);   /* half a sample's step: twice the rate */
    for (i = 0; i < n; i++) {
        float acc = 0.0f, y;
        int wrapped = 0;
        for (s = 0; s < 2; s++) {
            float a, b, c, r;
            l->ph[0] += dt;
            if (l->ph[0] >= 1.0f) {
                l->ph[0] -= 1.0f;
                wrapped = 1;
            }
            l->ph[1] += dt * rb;
            l->ph[1] -= fm_floorf(l->ph[1]);
            l->ph[2] += dt * rc;
            l->ph[2] -= fm_floorf(l->ph[2]);
            a = lg_src(wa, l->ph[0], pw, &l->ns);
            b = lg_src(wb, l->ph[1], pw, &l->ns);
            r = lg_op(op1, a, b, bits, wa, pw, &l->ns);
            if (amt2 > 0.001f) {
                c = lg_src(wc, l->ph[2], pw, &l->ns);
                r += (lg_op(op2, r, c, bits, wa, pw, &l->ns) - r) * amt2;
            }
            acc += r + (a - r) * dry;
        }
        l->lp += (acc * 0.5f - l->lp) * sm;
        y = l->lp;
        /* the grains: the voice's last moments, one grain a period */
        l->buf[l->w] = (int16_t)clampf(y * 16000.0f, -32000.0f, 32000.0f);
        l->w = (l->w + 1) & (RV_LOGIC_BUF - 1);
        if (grain > 0.001f) {
            float gsum = 0.0f;
            if (wrapped) {
                for (k = 0; k < 4 && l->gon[k]; k++)
                    ;
                if (k < 4) {
                    float per = 1.0f / (f > 20.0f ? f : 20.0f), len = per * (0.15f + grain * 3.85f);
                    float back = (per * (1.0f + spray * 6.0f * (xs_bi(&l->ns) * 0.5f + 0.5f))) * (float)RV_FS;
                    if (back + len * gp * (float)RV_FS > (float)(RV_LOGIC_BUF - 8))
                        back = (float)(RV_LOGIC_BUF - 8) - len * gp * (float)RV_FS;
                    l->gpos[k] = (float)l->w - clampf(back, 4.0f, (float)(RV_LOGIC_BUF - 8));
                    l->gt[k] = 0.0f;
                    l->gdt[k] = RV_DT / clampf(len, 0.0005f, 0.02f);
                    l->ginc[k] = gp;
                    l->gon[k] = 1;
                }
            }
            for (k = 0; k < 4; k++) {
                float p, wdw, fr;
                int32_t i0;
                if (!l->gon[k])
                    continue;
                p = l->gpos[k];
                while (p < 0.0f)
                    p += (float)RV_LOGIC_BUF;
                i0 = (int32_t)p;
                fr = p - (float)i0;
                wdw = 4.0f * l->gt[k] * (1.0f - l->gt[k]);
                gsum += (l->buf[i0 & (RV_LOGIC_BUF - 1)] + (l->buf[(i0 + 1) & (RV_LOGIC_BUF - 1)] - l->buf[i0 & (RV_LOGIC_BUF - 1)]) * fr) * (1.0f / 16000.0f) * wdw;
                l->gpos[k] += l->ginc[k];
                if (l->gpos[k] >= (float)RV_LOGIC_BUF)
                    l->gpos[k] -= (float)RV_LOGIC_BUF;
                l->gt[k] += l->gdt[k];
                if (l->gt[k] >= 1.0f)
                    l->gon[k] = 0;
            }
            y += (gsum * 1.2f - y) * clampf(grain * 3.0f, 0.0f, 1.0f);
        }
        out[i] = y * 0.5f;
    }
}

static void voice_engine_reset(voice_t *v, int eng)
{
    __builtin_memset(&v->e, 0, sizeof(v->e));
    switch (eng) {
    case ENG_VA: v->e.va.ns = v->rng | 1u; break;
    case ENG_FM: v->e.fm.menv = 1.0f; break;
    case ENG_PHYS: v->e.ph.ns = v->rng | 1u; break;
    case ENG_FORMANT: v->e.fo.ns = v->rng | 1u; break;
    case ENG_PARTICLE: v->e.pa.ns = v->rng | 1u; break;
    default: break;
    }
    v->e_eng = (uint8_t)(eng + 1);
}

/* eng: the engine that will render the voice (a stolen voice may hold another engine's state) */
static void synth_voice_note_on(voice_t *v, int note, float vel, int legato, int eng)
{
    v->note = (int8_t)note;
    v->vel = vel;
    v->target = (float)note;
    if (!legato)
        v->pitch = (float)note;
    if (!v->on || v->e_eng != eng + 1) {
        voice_engine_reset(v, eng);
        v->a.p = v->a.s = v->a.v = v->a.out = 0.0f;
        v->lad.s[0] = v->lad.s[1] = v->lad.s[2] = v->lad.s[3] = 0.0f;
        __builtin_memset(v->zp.s, 0, sizeof(v->zp.s));
        v->svf2.ic1 = v->svf2.ic2 = 0.0f;
        v->xs_y = v->xs_acc = 0.0f;
        v->svf.ic1 = v->svf.ic2 = 0.0f;
        v->lpg = 0.0f;
    } else if (eng == ENG_FM) {
        v->e.fm.menv = 1.0f;
    }
    v->on = 1;
}

#include "rv_zplane.h"

/* renders n samples of a synth voice, filtered and enveloped, added to out; returns its level */
static float synth_voice_render(voice_t *v, track_t *tr, float *out, int n, float bend)
{
    const float *eff = tr->eff;
    float buf[RV_SUB], f, lvl0, cut, res, ktrk, env_amt, lvl;
    int i, eng = clampi((int)(eff[TP_ENGINE] + 0.5f), 0, ENG_N - 1), ft = clampi((int)(eff[TP_FTYPE] + 0.5f), 0, FT_N - 1);
    float glide = eff[TP_GLIDE] < 0.5f ? 1.0f : 1.0f - fm_expf(-(float)n * RV_DT / (0.002f + eff[TP_GLIDE] * eff[TP_GLIDE] * 0.00012f));
    float velamt = eff[TP_VELO] * (1.0f / 127.0f);
    v->pitch += (v->target - v->pitch) * glide;
    f = note_hz(v->pitch + eff[TP_PITCH] + eff[TP_FINE] * 0.01f + bend);
    if (f > 16000.0f)
        f = 16000.0f;
    if (v->e_eng != eng + 1)                          /* the engine changed under a sounding voice */
        voice_engine_reset(v, eng);
    switch (eng) {
    case ENG_VA: va_render(v, eff, f, buf, n); break;
    case ENG_CZ: cz_render(v, eff, f, buf, n, v->track); break;
    case ENG_FM: fm_render(v, eff, f, buf, n); break;
    case ENG_PHYS: phys_render(v, eff, f, tr->exc, buf, n); break;
    case ENG_FORMANT: form_render(v, eff, f, buf, n); break;
    case ENG_CHIP: chip_render(v, eff, f, buf, n); break;
    case ENG_SQ: sq_render(v, eff, f, buf, n); break;
    case ENG_WAVE: wave_render(v, eff, f, buf, n); break;
    case ENG_LOGIC: logic_render(v, eff, f, buf, n); break;
    default: part_render(v, eff, f, buf, n); break;
    }
    /* the filter: cutoff from its knob, the articulation (CZ: its DCA), the key */
    lvl0 = eng == ENG_CZ ? v->e.cz.level : eng == ENG_SQ ? v->e.sq.e2 : v->a.out;   /* SQ: ENV 2 */
    ktrk = eff[TP_KTRK] * (1.0f / 127.0f);
    env_amt = eff[TP_EFILT] * (1.0f / 64.0f);
    cut = eff[TP_CUT] + env_amt * 64.0f * lvl0 + ktrk * ((float)v->note - 60.0f) * (127.0f / 120.0f);
    res = eff[TP_RES] * (1.0f / 127.0f);
    if (ft == FT_LPG) {
        float target = clampf(lvl0, 0.0f, 1.0f);
        v->lpg += (target - v->lpg) * 0.35f;          /* a vactrol lags a little */
        cut = eff[TP_CUT] * v->lpg + 6.0f;
    }
    cut = clampf(cut, 0.0f, 135.0f);
    if (ft >= FT_XLP24) {                             /* the XT filters */
        float q = 0.5f + res * res * 24.0f;
        svf_set(&v->svf, rv_cut_hz(cut), ft == FT_XNOTCH ? 0.5f + res * 6.0f : q);
        svf_set(&v->svf2, rv_cut_hz(cut), ft == FT_XLP24 || ft == FT_XBP24 ? 0.55f : q);
    } else if (ft >= FT_ZVOWEL)                       /* MORPH and XFORM */
        zp_design(ft - FT_ZVOWEL, cut * (1.0f / 127.0f), res, v->zp.c, &v->zp.g);
    else if (ft == FT_LADDER || ft == FT_MS20 || ft == FT_SH101 || ft == FT_SQ80)
        ladder_set(&v->lad, rv_cut_hz(cut), res);
    else
        svf_set(&v->svf, rv_cut_hz(cut), 0.5f + res * res * 18.0f);
    lvl = 0.0f;
    for (i = 0; i < n; i++) {
        float a = eng == ENG_CZ ? 1.0f : artic_tick(&v->a, &v->cfg), x = buf[i], bp, hp, y;   /* CZ: its DCA is the envelope */
        if (eng != ENG_PHYS)
            x += tr->exc[i] * 0.5f;
        if (v->a.tailv > 0.0005f)
            x += xs_bi(&v->rng) * v->a.tailv * 0.35f;
        switch (ft) {
        case FT_LADDER: y = ladder_tick(&v->lad, x); break;
        case FT_MS20: y = ms20_tick(&v->lad, x); break;
        case FT_SH101: y = sh101_tick(&v->lad, x); break;
        case FT_SQ80: y = sq80_tick(&v->lad, x); break;
        case FT_ZVOWEL: case FT_ZTALK: case FT_ZPHASER: case FT_ZPEAKS: case FT_ZSWEEP: case FT_ZMETAL:
            y = zp_tick(&v->zp, x);
            break;
        case FT_XLP24:                                /* two clean stages, the resonance in the first */
            y = svf_tick(&v->svf2, svf_tick(&v->svf, x, 0, 0), 0, 0);
            break;
        case FT_XBP24:
            svf_tick(&v->svf, x, &bp, 0);
            svf_tick(&v->svf2, bp * v->svf.k, &hp, 0);
            y = hp * v->svf2.k * (1.0f + res * 2.0f);
            break;
        case FT_XNOTCH:
            svf_tick(&v->svf, x, &bp, 0);
            y = x - v->svf.k * bp * (1.0f + res * 0.8f);
            break;
        case FT_XSH:                                  /* sample and hold: CUTOFF is the clock, RESO smooths less */
            v->xs_acc += rv_cut_hz(cut) * 2.0f * RV_DT;
            if (v->xs_acc >= 1.0f) {
                v->xs_acc -= fm_floorf(v->xs_acc);
                v->xs_y = x;
            }
            y = svf_tick(&v->svf2, v->xs_y, 0, 0) * (1.0f - res) + v->xs_y * res;
            break;
        case FT_XSHAPE: {                             /* a wave shaper: CUTOFF drives it, RESO folds it */
            float g = 1.0f + cut * (6.0f / 127.0f);
            y = fm_sinf(x * g * (1.0f + res * 2.0f)) * (0.7f + 0.3f / g);
            break;
        }
        case FT_LP: y = svf_tick(&v->svf, x, 0, 0); break;
        case FT_BP: svf_tick(&v->svf, x, &bp, 0); y = bp * (1.0f + res); break;
        case FT_HP: svf_tick(&v->svf, x, 0, &hp); y = hp; break;
        default: y = svf_tick(&v->svf, x, 0, 0) * v->lpg; break;
        }
        y *= a * (1.0f - velamt + velamt * v->vel);
        y = 2.0f * sat(y * 0.5f);                     /* a resonant filter or body driven hard: rounded, not runaway */
        out[i] += y;
        v->last = y;
        lvl = a;
    }
    if (eng == ENG_CZ) {
        if (v->e.cz.done)
            v->on = 0;
        return v->e.cz.level;
    }
    if (v->a.stage == AS_OFF && v->a.tailv < 0.0005f)
        v->on = 0;
    return lvl;
}
