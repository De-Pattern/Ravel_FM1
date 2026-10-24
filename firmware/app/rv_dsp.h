/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: small DSP pieces shared by the engines, the tracks and the buses. Single-precision
 * float throughout (the FM-1 has a float FPU, no double), no C library. */
#pragma once
#include "rv_math.h"

#define RV_DT (1.0f / 44100.0f)
#define RV_NYQ 22050.0f

RV_INL float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
RV_INL float lerpf(float a, float b, float t) { return a + (b - a) * t; }
RV_INL int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* note number to Hz */
static inline float note_hz(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }

/* soft saturation with no exp: x(27+x^2)/(27+9x^2), exact at |x| = 3 */
RV_INL float sat(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* a sine wavefolder that stays bounded */
static inline float fold(float x, float amt)
{
    if (amt <= 0.0f)
        return x;
    return lerpf(x, fm_sinf(x * (1.0f + amt * 6.0f) * 1.5707963f), clampf(amt * 1.5f, 0.0f, 1.0f));
}

/* xorshift32: noise and seeded scatter */
static inline uint32_t xs32(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
RV_INL float xs_uni(uint32_t *s) { return (float)(xs32(s) >> 8) * (1.0f / 16777216.0f); }
RV_INL float xs_bi(uint32_t *s) { return (float)(int32_t)xs32(s) * (1.0f / 2147483648.0f); }

/* one-pole low pass; coefficient from a cutoff in Hz */
static inline float op_coef(float hz) { return 1.0f - fm_expf(-6.2831853f * hz * RV_DT); }

/* topology-preserving state variable filter (Zavalishin), low/band/high at once */
typedef struct { float ic1, ic2, g, k, a1, a2, a3; } svf_t;
RV_INL void svf_set(svf_t *f, float hz, float q)
{
    float g;
    hz = clampf(hz, 10.0f, RV_NYQ * 0.9f);
    g = fm_tanf(3.14159265f * hz * RV_DT);
    f->g = g;
    f->k = 1.0f / (q < 0.05f ? 0.05f : q);
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
/* returns low pass; *bp and *hp optional */
RV_INL float svf_tick(svf_t *f, float x, float *bp, float *hp)
{
    float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    if (bp)
        *bp = v1;
    if (hp)
        *hp = x - f->k * v1 - v2;
    return v2;
}

/* a 4-pole ladder with saturating stages (one tanh at the input, soft stages), g = tan(pi f / fs) */
typedef struct { float s[4], g, res; } ladder_t;
static inline void ladder_set(ladder_t *l, float hz, float res)
{
    hz = clampf(hz, 10.0f, RV_NYQ * 0.85f);
    l->g = fm_tanf(3.14159265f * hz * RV_DT);
    l->res = res * 4.2f;
}
RV_INL float ladder_tick(ladder_t *l, float x)
{
    float G = l->g / (1.0f + l->g), y, u;
    int i;
    /* zero-delay feedback estimate from the last state (one step: cheap, stable enough) */
    float fb = l->s[3];
    u = sat(x - l->res * fb);
    for (i = 0; i < 4; i++) {
        float v = (u - l->s[i]) * G;
        y = v + l->s[i];
        l->s[i] = y + v;
        u = y;
    }
    return u * (1.0f + l->res * 0.25f);
}

/* MS-20 style (Korg35 topology): two one-pole lowpasses with positive feedback taken through a
 * highpass, so the resonance peaks hard and can scream. The input is driven and the feedback
 * goes through an asymmetric clipper: hot and dirty. s[0..2]: the stages, s[3]: the feedback. */
RV_INL float ms20_tick(ladder_t *l, float x)
{
    float G = l->g / (1.0f + l->g), K = l->res * (2.05f / 4.2f), v, y1, y2, hp, fb;
    x = sat(x * 1.6f) * 0.85f;                        /* the input, already pushed */
    v = (x - l->s[0]) * G;                            /* lowpass 1 */
    y1 = v + l->s[0];
    l->s[0] = y1 + v;
    fb = l->s[3];
    fb = sat(fb * 1.8f) * (1.0f / 1.8f) + 0.08f * fb * fb;   /* the diodes: soft and lopsided */
    v = (y1 + K * fb - l->s[1]) * G;                  /* lowpass 2, fed back positively */
    y2 = v + l->s[1];
    l->s[1] = y2 + v;
    v = (y2 - l->s[2]) * G;                           /* the feedback's highpass */
    hp = y2 - (v + l->s[2]);
    l->s[2] = l->s[2] + 2.0f * v;
    l->s[3] = hp;
    return y2 * (1.0f / (1.0f + K * 0.35f));
}

/* SH-101 style (the IR3109's four OTA stages): smooth, linear stages, the resonance through a
 * gentle curve, and the passband's loss made up as resonance rises: round and liquid, with a
 * resonance that sings rather than bites */
RV_INL float sh101_tick(ladder_t *l, float x)
{
    float G = l->g / (1.0f + l->g), k = l->res * (3.85f / 4.2f), u, y = 0.0f;
    int i;
    u = x * (1.0f + k * 0.45f) - k * sat(l->s[3] * 0.9f) * 1.1f;
    u = sat(u * 0.8f) * 1.25f;                        /* a very soft input stage */
    for (i = 0; i < 4; i++) {
        float v = (u - l->s[i]) * G;
        y = v + l->s[i];
        l->s[i] = y + v;
        u = y;
    }
    return y;
}


/* polyBLEP for band-limited steps: t = phase 0..1, dt = increment */
RV_INL float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* DC blocker */
typedef struct { float x1, y1; } dcb_t;
static inline float dcb_tick(dcb_t *d, float x)
{
    float y = x - d->x1 + 0.995f * d->y1;
    d->x1 = x;
    d->y1 = fm_flush(y);
    return y;
}

/* time parameter (0..127) to seconds: 0.5 ms .. 8 s, exponential */
float rv_time_s(int v);
static inline float rv_time_f(float v) { return 0.0005f * fm_exp2f(v * (13.97f / 127.0f)); }
/* cutoff parameter (0..127) to Hz: 20 Hz .. 20 kHz */
float rv_cut_hz(float v);

/* per-sample coefficient for an exponential approach over time t (seconds) */
static inline float tc_coef(float t) { return t <= 0.00005f ? 1.0f : 1.0f - fm_expf(-RV_DT / t * 4.6f); }
