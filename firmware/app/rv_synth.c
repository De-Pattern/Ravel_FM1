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
}

/* a library shape: attack shape, body, death (Avenir's Envion idea: three values make an envelope) */
static void artic_cfg_lib(artic_cfg_t *c, const uint8_t *l, const float *eff)
{
    float a = l[0] * (1.0f / 127.0f), b = l[1] * (1.0f / 127.0f), d = l[2] * (1.0f / 127.0f);
    artic_cfg_track(c, eff);                     /* keeps give, loop and tail from the track */
    c->atk = 0.0005f * fm_exp2f(a * 12.0f);      /* 0.5 ms .. 2 s */
    c->curve = 0.6f - a * 1.2f;                  /* fast attacks bow out, slow ones swell */
    c->over = a < 0.2f ? 0.15f : 0.0f;
    c->dec = 0.02f * fm_exp2f(b * 7.0f);         /* 20 ms .. 2.5 s */
    c->sus = b * b;                              /* percussive .. held */
    c->rel = 0.005f * fm_exp2f(d * 10.0f);       /* 5 ms .. 5 s */
    c->tail = d > 0.7f ? (d - 0.7f) * 2.0f : c->tail;
}

static inline float shape_up(float u, float c) { return u + c * u * (1.0f - u); }     /* c > 0: fast start */
static inline float shape_down(float u) { float r = 1.0f - u; return 1.0f - r * r * r; }  /* fast then slow */

static void artic_start(artic_t *a, const artic_cfg_t *c)
{
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

/* ---------------------------------------------------------------- PD --- */
/* Casio CZ phase distortion: a cosine read through a bent phase. d = 0 is a sine. */
static inline float pd_wave(int w, float x, float d)
{
    float m, p;
    d = clampf(d, 0.0f, 0.98f);
    switch (w) {
    case 0:                                           /* saw */
        m = 0.5f - d * 0.49f;
        p = x < m ? x * (0.5f / m) : 0.5f + (x - m) * (0.5f / (1.0f - m));
        return -sin_lut(p + 0.25f);
    case 1: {                                         /* square */
        float x2 = x * 2.0f, w2 = 1.0f - d;
        if (x < 0.5f)
            p = 0.5f * fm_minf(1.0f, x2 / w2);
        else
            p = 0.5f + 0.5f * fm_minf(1.0f, (x2 - 1.0f) / w2);
        return -sin_lut(p + 0.25f);
    }
    case 2:                                           /* pulse */
        m = 1.0f - d * 0.97f;
        p = x < m ? x / m : 1.0f;
        return -sin_lut(p + 0.25f);
    case 3: {                                         /* double sine */
        float y = x * 2.0f;
        y -= fm_floorf(y);
        m = 0.5f - d * 0.45f;
        p = y < m ? y * (0.5f / m) : 0.5f + (y - m) * (0.5f / (1.0f - m));
        return (x < 0.5f ? -1.0f : 1.0f) * sin_lut(p * 0.5f);
    }
    case 4:                                           /* saw-pulse */
        if (x < 0.5f)
            return pd_wave(0, x * 2.0f, d);
        return pd_wave(2, (x - 0.5f) * 2.0f, d);
    case 5:                                           /* resonance, saw window */
        return sin_lut(x * (1.0f + d * 15.0f)) * (1.0f - x) * 1.3f;
    case 6: {                                         /* resonance, triangle window */
        float win = x < 0.5f ? x * 2.0f : 2.0f - x * 2.0f;
        return sin_lut(x * (1.0f + d * 15.0f)) * win * 1.3f;
    }
    default: {                                        /* resonance, trapezoid window */
        float win = x < 0.25f ? x * 4.0f : x > 0.75f ? (1.0f - x) * 4.0f : 1.0f;
        return sin_lut(x * (1.0f + d * 15.0f)) * win;
    }
    }
}

static void pd_render(voice_t *v, const float *eff, float f, float *out, int n)
{
    pd_t *s = &v->e.pd;
    int w1 = clampi((int)(eff[TP_S0] + 0.5f), 0, 7), w2 = clampi((int)(eff[TP_S1] + 0.5f), 0, 7), i;
    float mix = eff[TP_S2] * (1.0f / 127.0f);
    float d = eff[TP_S3] * (1.0f / 127.0f) + eff[TP_S4] * (1.0f / 64.0f) * v->a.out;
    float dt1 = f * RV_DT, dt2 = dt1 * fm_exp2f(eff[TP_S5] * (0.5f / 127.0f / 12.0f));   /* up to 50 cents */
    float foam = eff[TP_S6] * (1.0f / 127.0f), ring = eff[TP_S7] * (1.0f / 127.0f);
    d = clampf(d, 0.0f, 1.0f);
    for (i = 0; i < n; i++) {
        float a, b, x;
        s->ph1 += dt1;
        if (s->ph1 >= 1.0f)
            s->ph1 -= 1.0f;
        s->ph2 += dt2;
        if (s->ph2 >= 1.0f)
            s->ph2 -= 1.0f;
        a = pd_wave(w1, s->ph1, d);
        b = mix > 0.001f || ring > 0.001f ? pd_wave(w2, s->ph2, d) : 0.0f;
        x = lerpf(a, b, mix);
        x = lerpf(x, a * b * 1.5f, ring);
        if (foam > 0.001f) {                          /* only the sidebands: take the fundamental away */
            float fund = -sin_lut(s->ph1 + 0.25f) * (1.0f - d * 0.5f);
            float sb = (x - fund) * (1.0f + foam * 3.0f);
            x = lerpf(x, sb, foam);
        }
        out[i] = x * 0.5f;
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
        v->svf.ic1 = v->svf.ic2 = 0.0f;
        v->lpg = 0.0f;
    } else if (eng == ENG_FM) {
        v->e.fm.menv = 1.0f;
    }
    v->on = 1;
}

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
    case ENG_PD: pd_render(v, eff, f, buf, n); break;
    case ENG_FM: fm_render(v, eff, f, buf, n); break;
    case ENG_PHYS: phys_render(v, eff, f, tr->exc, buf, n); break;
    case ENG_FORMANT: form_render(v, eff, f, buf, n); break;
    case ENG_CHIP: chip_render(v, eff, f, buf, n); break;
    default: part_render(v, eff, f, buf, n); break;
    }
    /* the filter: cutoff from its knob, the articulation, the key */
    lvl0 = v->a.out;
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
    if (ft == FT_LADDER || ft == FT_MS20 || ft == FT_SH101)
        ladder_set(&v->lad, rv_cut_hz(cut), res);
    else
        svf_set(&v->svf, rv_cut_hz(cut), 0.5f + res * res * 18.0f);
    lvl = 0.0f;
    for (i = 0; i < n; i++) {
        float a = artic_tick(&v->a, &v->cfg), x = buf[i], bp, hp, y;
        if (eng != ENG_PHYS)
            x += tr->exc[i] * 0.5f;
        if (v->a.tailv > 0.0005f)
            x += xs_bi(&v->rng) * v->a.tailv * 0.35f;
        switch (ft) {
        case FT_LADDER: y = ladder_tick(&v->lad, x); break;
        case FT_MS20: y = ms20_tick(&v->lad, x); break;
        case FT_SH101: y = sh101_tick(&v->lad, x); break;
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
    if (v->a.stage == AS_OFF && v->a.tailv < 0.0005f)
        v->on = 0;
    return lvl;
}
