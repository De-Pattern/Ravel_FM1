/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the shared buses. The feedback bus (a delay loop with a processor and a band filter inside
 * and an edge guard, after Silt's feedback network), a tape-style delay, the 12-BIT space, the BED and
 * the microcuts (Silt: slivers of the output looped, reversed, stuttered). The FM-1 build keeps only
 * these; the vocoder, the filter pair, BANDS and the other spaces live on in Ravel Software.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

/* a bus setting as the matrix and the macros move it (GLOBAL destinations) */
RV_INL float gv(int g) { return geff[g]; }

/* ----------------------------------------------------------- feedback --- */
static int16_t fb_buf[RV_FB_SAMPLES] RV_BIG;
static int32_t fb_w;
static float fb_dt, fb_env, fb_edge = 1.0f, fb_dcx, fb_dcy;
static svf_t fb_f;
static int16_t fb_comb[1024];
static int32_t fb_cw;
static float fb_comblp, fb_shph, fb_hz1, fb_crush;
static int32_t fb_crc;
static int16_t fb_ap[4][256];
static int32_t fb_api[4];
static float fb_hi[4][4], fb_hq[4][4];             /* Hilbert pair: 4 second-order allpasses each (x1 x2 y1 y2) */
static const float hil_ci[4] = {0.47940086f, 0.87621849f, 0.97659759f, 0.99749926f};
static const float hil_cq[4] = {0.16175850f, 0.73302893f, 0.94534970f, 0.99059916f};

static inline float ap2(float *s, float c, float x)
{
    float y = c * x - s[1] + c * s[3];
    s[1] = s[0];
    s[0] = x;
    s[3] = s[2];
    s[2] = y;
    return y;
}

RV_INL float fb_read(float d)
{
    float rp = (float)fb_w - d, fr;
    int32_t i0, i1;
    while (rp < 0.0f)
        rp += (float)RV_FB_SAMPLES;
    i0 = (int32_t)rp;
    fr = rp - (float)i0;
    if (i0 >= (int32_t)RV_FB_SAMPLES)                 /* (rp can round up to the size) */
        i0 -= (int32_t)RV_FB_SAMPLES;
    i1 = i0 + 1 >= (int32_t)RV_FB_SAMPLES ? 0 : i0 + 1;
    return (fb_buf[i0] + (fb_buf[i1] - fb_buf[i0]) * fr) * (1.0f / 16384.0f);
}

/* the processor's settings, worked out once a step (fb_prep) */
typedef struct { int proc; float amt, tune, a, b; int32_t i, L[4], hold; } fbp_t;
RV_INL void fb_prep(fbp_t *p, int proc, float amt, float tune)
{
    static const int lens[4] = {61, 113, 173, 241};
    int k;
    p->proc = proc;
    p->amt = amt;
    p->tune = tune;
    p->a = p->b = 0.0f;
    p->i = p->hold = 0;
    for (k = 0; k < 4; k++)
        p->L[k] = 2;
    switch (proc) {
    case 1: {                                         /* comb: its length */
        float fr = 40.0f * fm_exp2f(tune * 7.0f), d = clampf(44100.0f / fr, 2.0f, 1020.0f);
        p->i = (int32_t)d;
        break;
    }
    case 2:                                           /* smear: the allpasses' lengths */
        for (k = 0; k < 4; k++)
            p->L[k] = clampi((int)((float)lens[k] * (0.3f + tune * 0.7f)), 2, 255);
        break;
    case 3: {                                         /* frequency shift: its rate */
        float c = (tune - 0.5f) * 2.0f, ac = fm_fabsf(c), hz = 0.0f;
        if (ac > 0.03f) {
            hz = 0.5f * fm_powf(400.0f, (ac - 0.03f) / 0.97f);
            if (c < 0.0f)
                hz = -hz;
        }
        p->a = hz * RV_DT;
        break;
    }
    case 4:
        p->a = 1.0f + amt * 8.0f * (0.5f + tune);
        break;
    case 5:
        p->hold = 1 + (int)(tune * tune * 40.0f);
        p->a = fm_exp2f(15.0f - amt * 13.0f);
        break;
    default:
        break;
    }
}

RV_INL float fb_insert(float x, const fbp_t *p)
{
    int k;
    float amt = p->amt;
    switch (p->proc) {
    case 1: {                                         /* comb, 40 Hz .. 5 kHz */
        float z, y;
        int32_t i0 = fb_cw - p->i;
        if (i0 < 0)
            i0 += 1024;
        z = fb_comb[i0] * (1.0f / 16384.0f);
        fb_comblp += (z - fb_comblp) * 0.6f;
        y = x + fb_comblp * amt * 0.95f;
        fb_comb[fb_cw] = (int16_t)clampf(sat(y) * 16384.0f, -32000.0f, 32000.0f);
        fb_cw = (fb_cw + 1) & 1023;
        return y * (1.0f - amt * 0.5f);
    }
    case 2: {                                         /* smear: four allpasses */
        float g = amt * 0.75f, y = x;
        for (k = 0; k < 4; k++) {
            int ri = (fb_api[k] - p->L[k] + 256) & 255;
            float z = fb_ap[k][ri] * (1.0f / 16384.0f), v = y + z * g;
            fb_ap[k][fb_api[k]] = (int16_t)clampf(v * 16384.0f, -32000.0f, 32000.0f);
            y = z - v * g;
            fb_api[k] = (fb_api[k] + 1) & 255;
        }
        return y;
    }
    case 3: {                                         /* frequency shift, -200 .. +200 Hz */
        float ai = x, aq = x, s;
        for (k = 0; k < 4; k++) {
            ai = ap2(fb_hi[k], hil_ci[k], ai);
            aq = ap2(fb_hq[k], hil_cq[k], aq);
        }
        s = fb_hz1;
        fb_hz1 = ai;
        fb_shph += p->a;
        fb_shph -= fm_floorf(fb_shph);
        s = s * sin_lut(fb_shph + 0.25f) + aq * sin_lut(fb_shph);
        return lerpf(x, s, amt);
    }
    case 4:
        return lerpf(x, fm_sinf(x * p->a), amt);
    case 5: {
        float q = p->a;
        if (++fb_crc >= p->hold) {
            fb_crc = 0;
            fb_crush = x;
        }
        return (float)(int32_t)(fb_crush * q) / q;
    }
    default:
        return x;
    }
}

/* in: the bus's input this step; out: its output (the loop signal) */
/* BASE and WIDTH: a high pass and a low pass in the loop (no resonance), each over its half of the
 * range; at BASE 0 and WIDTH full the band is open and they are skipped */
static svf_t fb_bp[2];
RV_HOT static void feedback_run(const float *in, int n, float *out)
{
    int hp_on = gv(GP_PAIRF1) > 0.5f, lp_on = gv(GP_PAIRF2) < 126.5f;   /* (as moved by the matrix and macros) */
    int i, proc = clampi(rv.gp[GP_FBPROC], 0, 5);
    float target = 40.0f + fm_exp2f(gv(GP_FBTIME) * (7.2f / 127.0f)) * 40.0f;   /* samples: ~2 ms .. 0.14 s */
    float regen = gv(GP_FBREGEN) * (1.05f / 127.0f), amt = gv(GP_FBAMT) * (1.0f / 127.0f);
    float tune = gv(GP_FBTUNE) * (1.0f / 127.0f), lvl = gv(GP_FBLEVEL) * (1.0f / 127.0f);
    int edge = rv.gp[GP_FBEDGE];
    if (target > (float)RV_FB_SAMPLES - 4.0f)
        target = (float)RV_FB_SAMPLES - 4.0f;
    svf_set(&fb_f, rv_cut_hz_i((float)gv(GP_FBCUT)), 0.9f);
    if (hp_on)
        svf_set(&fb_bp[0], rv_cut_hz_i(clampf(gv(GP_PAIRF1), 0.0f, 63.0f)), 0.707f);
    if (lp_on)
        svf_set(&fb_bp[1], rv_cut_hz_i(clampf(gv(GP_PAIRF2), 64.0f, 127.0f)), 0.707f);
    fbp_t fp;
    fb_prep(&fp, proc, amt, tune);
    if (fb_dt <= 0.0f)
        fb_dt = target;
    for (i = 0; i < n; i++) {
        float dly, ff, y, d, a, r = regen;
        fb_dt += (target - fb_dt) * 0.0008f;
        dly = fb_read(fb_dt);
        ff = proc == 0 ? svf_tick(&fb_f, dly, 0, 0) : fb_insert(svf_tick(&fb_f, dly, 0, 0), &fp);
        if (hp_on) {
            float hp;
            svf_tick(&fb_bp[0], ff, 0, &hp);
            ff = hp;
        }
        if (lp_on)
            ff = svf_tick(&fb_bp[1], ff, 0, 0);
        y = sat(ff * 1.2f);
        d = y - fb_dcx + 0.996f * fb_dcy;             /* DC out of the loop */
        fb_dcx = y;
        fb_dcy = fm_flush(d);
        a = fm_fabsf(d);
        fb_env += (a - fb_env) * (a > fb_env ? 0.01f : 0.0005f);
        if (edge) {                                    /* the edge guard: hold just short of runaway */
            float tg = fb_env > 0.35f ? 0.35f / fb_env : 1.0f;
            fb_edge += (tg - fb_edge) * (tg < fb_edge ? 0.01f : 0.0002f);
            r *= fb_edge;
        }
        fb_buf[fb_w] = (int16_t)clampf(sat(in[i] + d * r) * 16384.0f, -32000.0f, 32000.0f);
        if (++fb_w >= (int32_t)RV_FB_SAMPLES)
            fb_w = 0;
        out[i] = d * lvl;
    }
    rv_live.fb_env = fb_env;
}

/* -------------------------------------------------------------- delay --- */
/* A granular tape delay. One short tape loop, written at half rate and 12 bits (about 0.55 s),
 * saturated on the way in like a driven tape (SATURAT). Echoes read it at TIME, the tape's speed
 * wandering slowly (WOW) and quickly (FLUTTER), each pass through TONE, the tape's age (AGE: darker,
 * hiss, now and then a dropout) and the saturation again, so the repeats wear away. GRAINS reads
 * the same tape as a cloud of up to eight grains around the echo (after Mutable Instruments'
 * Beads, lofi): DENSITY, SIZE and PITCH set them, WOW scatters them, SPREAD widens them. DOUBLE
 * adds a second, wavering copy of what comes in, close behind it (double tracking). FREEZE stops
 * the tape recording: the echoes and the cloud play what it holds. The main settings follow the
 * matrix and the macros (GLOBAL). */
static int16_t dly_buf[RV_DELAY_SAMPLES] RV_CACHE_RAM;
static int32_t dly_w;
static float dly_lp, dly_t, dly_fbv, dly_half, dly_age_lp, dly_drop = 1.0f, dly_drop_t;
static float dly_wph, dly_fph, dly_wdrift, dly_wdt, dly_dph;
static uint8_t dly_freeze, dly_ph;
static uint32_t dly_rng = 0xDE1A7u;

#define GR_N 8
typedef struct { float pos, inc, t, dt, pl, pr; uint8_t on; } grain_t;
static grain_t grains[GR_N];
static float gr_acc, gr_lpl, gr_lpr;
static uint32_t gr_rng = 0x6EA1u;


/* the tape at p (in half-rate samples, fractional) */
RV_INL float dly_read(float p)
{
    int32_t i0, i1;
    float fr;
    while (p < 0.0f)
        p += (float)RV_DELAY_SAMPLES;
    i0 = (int32_t)p;
    fr = p - (float)i0;
    while (i0 >= (int32_t)RV_DELAY_SAMPLES)               /* (no division: the % it was, for what p can be) */
        i0 -= (int32_t)RV_DELAY_SAMPLES;
    i1 = i0 + 1 >= (int32_t)RV_DELAY_SAMPLES ? 0 : i0 + 1;
    return (dly_buf[i0] + (dly_buf[i1] - dly_buf[i0]) * fr) * (1.0f / 16384.0f);
}

/* the tape's saturation: unity for quiet signals, rounding the loud ones off, a little lopsided
 * (even harmonics), so it can never make the loop gain more than FEEDBK */
RV_INL float dly_sat(float x, float drv)
{
    return drv > 1.01f ? sat(x * drv + 0.08f * (drv - 1.0f) * x * x) / drv : x;
}

RV_HOT static void delay_run(const float *in, int n, float *l, float *r)
{
    int i, k;
    float spb = 60.0f / gv(GP_TEMPO), tv = gv(GP_DLYTIME), target;
    float fb = gv(GP_DLYFB) * (0.95f / 127.0f), tone = 0.05f + gv(GP_DLYTONE) * (0.9f / 127.0f);
    float lvl = gv(GP_DLYLEVEL) * (1.0f / 127.0f);
    float wow = gv(GP_DLYWOB) * (1.0f / 127.0f), flut = gv(GP_DLYFLUT) * (1.0f / 127.0f);
    float drv = 1.0f + gv(GP_DLYSAT) * (5.0f / 127.0f), age = gv(GP_DLYAGE) * (1.0f / 127.0f);
    float dbl = gv(GP_DLYDBL) * (1.0f / 127.0f), gmix = gv(GP_DLYMODE) * (1.0f / 127.0f);
    float spread = 1.0f + rv.gp[GP_DLYSPREAD] * (0.5f / 127.0f), stereo = rv.gp[GP_DLYSPREAD] * (1.0f / 127.0f);
    float rate = 0.7f * fm_exp2f(gv(GP_GRDENS) * (6.5f / 127.0f));             /* 0.7 to 64 grains a second */
    float size = 0.02f * fm_exp2f(gv(GP_GRSIZE) * (4.3f / 127.0f));            /* 20 ms to 0.4 s */
    float agec = 1.0f - age * 0.75f, span = (float)RV_DELAY_SAMPLES - 64.0f;
    int frz = dly_freeze || rv.gp[GP_DLYFREEZE];
    /* TIME: free below the middle (10 ms to 0.5 s), then divisions of the beat */
    if (tv < 64.0f)
        target = 0.01f + tv * (0.49f / 64.0f);
    else {
        static const float div[8] = {0.125f, 0.1667f, 0.25f, 0.333f, 0.375f, 0.5f, 0.6667f, 0.75f};
        target = spb * div[clampi((int)(tv - 64.0f) / 8, 0, 7)];
    }
    target *= (float)RV_FS * 0.5f;                     /* in the tape's own (half-rate) samples */
    if (target * spread * 1.03f > span)
        target = span / (spread * 1.03f);
    if (dly_t <= 0.0f)
        dly_t = target;
    if (frz)
        fb = 1.0f;
    for (i = 0; i < n; i++) {
        float x = in[i], tt, wp, a, b, y, mod, gl = 0.0f, gr = 0.0f, d1 = 0.0f, d2 = 0.0f;
        dly_t += (target - dly_t) * 0.0005f;
        /* WOW: a slow sway that also drifts; FLUTTER: a quick, shallow waver */
        dly_wph += (0.45f + dly_wdrift * 0.3f) * RV_DT;
        dly_wph -= fm_floorf(dly_wph);
        dly_fph += 7.3f * RV_DT;
        dly_fph -= fm_floorf(dly_fph);
        if ((dly_wdt -= RV_DT) <= 0.0f) {
            dly_wdt = 0.4f + xs_uni(&dly_rng);
            dly_wdrift = xs_bi(&dly_rng);
        }
        mod = 1.0f + wow * 0.014f * sin_lut(dly_wph) + flut * 0.0025f * (sin_lut(dly_fph) + 0.4f * sin_lut(dly_fph * 2.7f + 0.3f));
        tt = dly_t * mod;
        wp = (float)dly_w + (float)dly_ph * 0.5f;     /* where the tape is being written, between its samples */
        a = dly_read(wp - tt);
        b = dly_read(wp - tt * spread);
        /* each pass: TONE, the tape's age, its saturation */
        dly_lp += (b - dly_lp) * tone;
        dly_age_lp += (dly_lp - dly_age_lp) * agec;
        y = dly_age_lp;
        if (age > 0.0f) {
            y += xs_bi(&dly_rng) * age * age * 0.004f;      /* hiss */
            if ((dly_drop_t -= RV_DT) <= 0.0f) {          /* a dropout now and then, more as it ages */
                dly_drop_t = 0.05f + xs_uni(&dly_rng) * (3.0f - age * 2.5f);
                dly_drop = xs_uni(&dly_rng) < age * 0.6f ? 0.2f + xs_uni(&dly_rng) * 0.5f : 1.0f;
            }
            y *= dly_drop;
            a *= 0.5f + 0.5f * dly_drop;
        }
        dly_fbv = y * fb;
        /* GRAINS: a cloud read from the same tape, around the echo */
        if (gmix > 0.004f) {
            gr_acc += rate * RV_DT;
            if (gr_acc >= 1.0f) {
                gr_acc -= 1.0f;
                for (k = 0; k < GR_N; k++)
                    if (!grains[k].on)
                        break;
                if (k < GR_N) {
                    grain_t *g = &grains[k];
                    float semi = gv(GP_GRPITCH) + wow * 5.0f * xs_bi(&gr_rng);
                    float len = size * (1.0f + wow * 0.5f * xs_bi(&gr_rng)), pan = 0.5f + stereo * 0.5f * xs_bi(&gr_rng);
                    float back = tt * (1.0f + (0.15f + wow * 0.6f) * xs_bi(&gr_rng));
                    g->inc = 0.5f * fm_exp2f(semi * (1.0f / 12.0f));          /* the tape runs at half rate */
                    if (g->inc > 0.5f && back < (g->inc - 0.5f) * len * (float)RV_FS + 8.0f)
                        back = (g->inc - 0.5f) * len * (float)RV_FS + 8.0f;   /* a grain reading faster than the tape fills starts further back */
                    g->pos = wp - clampf(back, 8.0f, span);
                    g->t = 0.0f;
                    g->dt = RV_DT / clampf(len, 0.01f, 0.6f);
                    g->pl = fm_sqrtf(1.0f - pan);
                    g->pr = fm_sqrtf(pan);
                    g->on = 1;
                }
            }
            for (k = 0; k < GR_N; k++) {
                grain_t *g = &grains[k];
                float w, z;
                if (!g->on)
                    continue;
                w = 4.0f * g->t * (1.0f - g->t);
                z = dly_read(g->pos) * w * w;
                gl += z * g->pl;
                gr += z * g->pr;
                g->pos += g->inc;
                g->t += g->dt;
                if (g->t >= 1.0f)
                    g->on = 0;
            }
            gr_lpl += (gl - gr_lpl) * tone;
            gr_lpr += (gr - gr_lpr) * tone;
            dly_fbv += (gr_lpl + gr_lpr) * 0.5f * fb * gmix * 0.6f;
        }
        /* DOUBLE: two copies of the input 18 and 27 ms behind, wavering apart */
        if (dbl > 0.0f) {
            dly_dph += 0.31f * RV_DT;
            dly_dph -= fm_floorf(dly_dph);
            d1 = dly_read(wp - (0.018f + 0.003f * sin_lut(dly_dph)) * (float)RV_FS * 0.5f);
            d2 = dly_read(wp - (0.027f + 0.004f * sin_lut(dly_dph + 0.37f)) * (float)RV_FS * 0.5f);
        }
        /* the tape: half rate, a pair of samples at a time, driven, 12 bits */
        dly_half += (frz ? 0.0f : x) * 0.5f + dly_fbv * 0.5f;
        if (++dly_ph >= 2) {
            dly_ph = 0;
            int v = (int)clampf(dly_sat(dly_half, drv) * 16384.0f, -32000.0f, 32000.0f);
            dly_buf[dly_w] = (int16_t)(v >= 0 ? v & ~15 : -((-v) & ~15));   /* 12 bits, toward zero: a tail can die away */
            if (++dly_w >= (int32_t)RV_DELAY_SAMPLES)
                dly_w = 0;
            dly_half = 0.0f;
        }
        l[i] += (a * (1.0f - gmix * 0.7f) + gr_lpl * gmix * 1.3f) * lvl + d1 * dbl * 0.8f;   /* ping left, pong right */
        r[i] += (y * (1.0f - gmix * 0.7f) + gr_lpr * gmix * 1.3f) * lvl + d2 * dbl * 0.8f;
    }
}

/* -------------------------------------------------------------- space --- */
/* the space is 12-BIT alone (format 20; ROOM and CLOUDS are in Ravel Software): its lines need 5620 samples */
#define SPACE_MEM 5632
static int16_t space_mem[SPACE_MEM] RV_BIG;

/* 12-BIT runs at half the sample rate (a pair of samples in, one tank step, a pair out), so the same
 * memory holds twice the time. */
static int16_t hr_w[12];                                  /* write positions, a line each */
static int16_t hr_base[12];
static float hr_in, hr_out[2][2], hr_lp[8], hr_ph[4], hr_hp;
static uint8_t hr_odd;
RV_INL void hr_layout(const int16_t *len, int n)
{
    int k, b = 0;
    for (k = 0; k < n; k++) {
        hr_base[k] = (int16_t)b;
        b += len[k];
    }
}
static inline float hr_rd(const int16_t *len, int k, float d)   /* line k, d samples ago (fractional) */
{
    float p = (float)hr_w[k] - d;
    int32_t a, b, L = len[k];
    const int16_t *m = space_mem + hr_base[k];
    float fr;
    while (p < 0.0f)
        p += (float)L;
    a = (int32_t)p;
    fr = p - (float)a;
    if (a >= L)                                           /* (p rounds up to L at most) */
        a -= L;
    b = a + 1 >= L ? 0 : a + 1;
    return (m[a] + (m[b] - m[a]) * fr) * (1.0f / 8192.0f);
}
/* q: the step a sample is kept in, a power of two (1: 16 bits); truncated toward zero, as cheap
 * converters did */
static inline void hr_wr(const int16_t *len, int k, float x, int32_t q)
{
    int32_t v = (int32_t)clampf(x * 8192.0f, -32000.0f, 32000.0f);
    if (q > 1)
        v = v >= 0 ? v & -q : -((-v) & -q);
    space_mem[hr_base[k] + hr_w[k]] = (int16_t)v;
    if (++hr_w[k] >= len[k])
        hr_w[k] = 0;
}
static inline float hr_ap(const int16_t *len, int k, float x, float g, int32_t q)
{
    float z = hr_rd(len, k, (float)len[k] - 1.0f), v = x + z * g;
    hr_wr(len, k, v, q);
    return z - v * g;
}

/* 12-BIT (after the Alesis Quadraverb's era): a bright, metallic plate of four combs and two allpasses a
 * side, kept in 12 bits (CRUNCH takes it down to 8) at half rate with its steps held, so it lisps and
 * aliases the way early digital reverbs did. SIZE is how long it rings, DAMP how dark, MOTION a little
 * chorus in the combs. */
static const int16_t q12_len[12] = {558, 594, 639, 678, 581, 617, 662, 701, 113, 171, 124, 182};
typedef struct { float fb, damp, modd; int32_t q; } q12_set_t;
RV_INL void q12_tank(const q12_set_t *st, float x, float *ol, float *or_)
{
    float fb = st->fb, damp = st->damp, modd = st->modd, a = 0.0f, b = 0.0f;
    int32_t q = st->q;
    int k;
    hr_ph[0] += 0.6f * 2.0f * RV_DT;
    hr_ph[0] -= fm_floorf(hr_ph[0]);
    for (k = 0; k < 8; k++) {
        float z = hr_rd(q12_len, k, (float)q12_len[k] - 2.0f - modd * (1.0f + sin_lut(hr_ph[0] + (float)k * 0.125f)));
        hr_lp[k] += (z - hr_lp[k]) * damp;
        hr_wr(q12_len, k, x * 0.6f + hr_lp[k] * fb, q);
        if (k < 4)
            a += z;
        else
            b += z;
    }
    a = hr_ap(q12_len, 8, a * 0.5f, 0.6f, q);
    a = hr_ap(q12_len, 9, a, 0.6f, q);
    b = hr_ap(q12_len, 10, b * 0.5f, 0.6f, q);
    b = hr_ap(q12_len, 11, b, 0.6f, q);
    *ol = (float)((int32_t)(a * 2048.0f)) * (1.0f / 2048.0f);   /* out through a 12-bit converter */
    *or_ = (float)((int32_t)(b * 2048.0f)) * (1.0f / 2048.0f);
}

RV_HOT static void halfrate_run(const float *in, int n, float *l, float *r)
{
    int i;
    float lvl = gv(GP_REVLEVEL) * (1.0f / 127.0f), lowc = rv.gp[GP_REVLOW] > 0 ? clampf(rv_cut_hz_i(rv.gp[GP_REVLOW] * 0.55f) * 6.2832f * RV_DT, 0.0f, 0.9f) : 0.0f;
    q12_set_t st;
    st.fb = 0.80f + gv(GP_REVSIZE) * (0.185f / 127.0f);   /* (the settings, once a step) */
    st.damp = 0.15f + (127 - gv(GP_REVDAMP)) * (0.7f / 127.0f);
    st.modd = gv(GP_REVMOD) * (6.0f / 127.0f);
    st.q = 1 << (2 + (rv.gp[GP_REVDIFF] * 4) / 127);    /* 12 bits of full scale, down to 8 */
    if (!hr_base[1])
        hr_layout(q12_len, 12);
    for (i = 0; i < n; i++) {
        float x = in[i];
        if (lowc > 0.0f) {
            hr_hp += (x - hr_hp) * lowc;
            x -= hr_hp;
        }
        hr_in += x * 0.5f;
        if (hr_odd) {                                     /* a pair in: one step of the tank */
            float ol, or_, xin = hr_in;
            hr_in = 0.0f;
            q12_tank(&st, xin, &ol, &or_);
            hr_out[0][0] = hr_out[0][1];
            hr_out[1][0] = hr_out[1][1];
            hr_out[0][1] = ol;
            hr_out[1][1] = or_;
        }
        l[i] += hr_out[0][1] * lvl * 1.2f;                /* held, step by step, as the old units did */
        r[i] += hr_out[1][1] * lvl * 1.2f;
        hr_odd ^= 1u;
    }
}

/* ---------------------------------------------------------------- BED --- */
/* a quiet layer under everything: grains of dust (and a breath of noise) striking a small body tuned
 * to the scale's root. TONE turns it from wood (bar ratios, short) to metal (inharmonic, long); SHIMMER
 * adds the body an octave and two up, slowly beating; DUCK lets the tracks hold it down, so it comes
 * up in the gaps; SPACE sends it into the reverb. Its dust is random: it is texture, not notes */
#define BED_M 10
static float bed_y1[BED_M], bed_y2[BED_M], bed_b1[BED_M], bed_b2[BED_M], bed_g[BED_M], bed_pan[BED_M];
static float bed_wait, bed_follow, bed_gain = 1.0f, bed_nlp, bed_ph, bed_lvl;
static uint32_t bed_rng = 0x51ED27u;
static int16_t bed_key[4] = {-1, -1, -1, -1};

static void bed_setup(void)
{
    static const float wood[6] = {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f};
    static const float metal[6] = {1.0f, 1.483f, 1.801f, 2.546f, 2.630f, 3.897f};
    float tone = rv.gp[GP_BEDTONE] * (1.0f / 127.0f), f0 = note_hz(48.0f + (float)(rv.gp[GP_ROOT] % 12));
    float t60 = (0.25f + tone * tone * 2.6f) * (1.0f + rv.gp[GP_BEDDECAY] * (4.0f / 127.0f));
    float shim = rv.gp[GP_BEDSHIM] * (1.0f / 127.0f);
    int k;
    for (k = 0; k < BED_M; k++) {
        float rr, td, g, r;
        if (k < 6) {
            rr = wood[k] + (metal[k] - wood[k]) * tone;
            td = t60 / (1.0f + (float)k * (1.2f - tone * 0.9f));
            g = fm_powf(0.5f, (float)k * (1.0f - tone * 0.6f));
        } else {                                      /* the shimmer: the lowest two an octave and two up */
            rr = (k & 1 ? metal[1] + (wood[1] - metal[1]) * (1.0f - tone) : 1.0f) * (k < 8 ? 2.0f : 4.0f) * (1.0f + 0.003f * (float)(k - 6));
            td = t60 * 1.6f;
            g = shim * (k < 8 ? 0.5f : 0.3f);
        }
        if (f0 * rr > 17000.0f)
            g = 0.0f;
        r = fm_exp2f(-1.0f / (td * 44100.0f) * 9.97f);
        bed_b1[k] = r * fm_cosf(6.2831853f * f0 * rr * RV_DT);
        bed_b2[k] = r * fm_sinf(6.2831853f * f0 * rr * RV_DT);
        bed_g[k] = g * 0.05f;
        bed_pan[k] = (k & 1) ? 0.3f : 0.7f;
    }
}

/* act: how loud the tracks were (it ducks under them); adds itself to l, r and to the space's input */
RV_HOT static void bed_run(float *rev_in, float *l, float *r, int n, float act)
{
    float lvl = gv(GP_BEDLEVEL) * (1.0f / 127.0f), dens = gv(GP_BEDDENS) * (1.0f / 127.0f);
    float duck = gv(GP_BEDDUCK) * (1.0f / 127.0f), send = gv(GP_BEDSPACE) * (1.0f / 127.0f), want;
    float hz = 0.4f + dens * dens * 40.0f, breath = 0.004f + dens * 0.012f, env = rv_live.bed_env;
    int i, k, nlive;
    uint8_t live[BED_M];
    if (lvl <= 0.0f && bed_lvl < 0.0001f) {
        bed_lvl = 0.0f;
        rv_live.bed_env *= 0.9f;
        return;
    }
    if (bed_key[0] != rv.gp[GP_BEDTONE] || bed_key[1] != rv.gp[GP_ROOT] || bed_key[2] != rv.gp[GP_BEDDECAY] || bed_key[3] != rv.gp[GP_BEDSHIM]) {
        bed_key[0] = rv.gp[GP_BEDTONE];
        bed_key[1] = rv.gp[GP_ROOT];
        bed_key[2] = rv.gp[GP_BEDDECAY];
        bed_key[3] = rv.gp[GP_BEDSHIM];
        RV_FAR(bed_setup)();                         /* (in the flash) */
    }
    bed_follow += (act - bed_follow) * (act > bed_follow ? 0.25f : 0.012f);   /* quick to duck, slow to come back */
    want = 1.0f - duck * 0.92f * clampf(bed_follow * 5.0f, 0.0f, 1.0f);   /* (at most down about 22 dB) */
    bed_ph += (float)n * RV_DT * 0.11f;
    bed_ph -= fm_floorf(bed_ph);
    {   /* the partials that sound: one with no gain and nothing left ringing adds nothing (skipped) */
        int m = 0;
        for (k = 0; k < BED_M; k++) {
            if (fm_fabsf(bed_y1[k]) + fm_fabsf(bed_y2[k]) < 1e-15f)
                bed_y1[k] = bed_y2[k] = 0.0f;
            if (bed_g[k] != 0.0f || bed_y1[k] != 0.0f || bed_y2[k] != 0.0f)
                live[m++] = (uint8_t)k;
        }
        nlive = m;
    }
    {   /* a sample at a time, the dust and the breath that strike it; then each partial over the whole
         * step (its state in registers), summed in the same order as ever; then the level */
        static float xin[RV_SUB] RV_RAMBSS, yl[RV_SUB] RV_RAMBSS, yr[RV_SUB] RV_RAMBSS;   /* (off the stack: the second core's is small) */
        float trem = 0.75f + 0.25f * sin_lut(bed_ph);
        int j;
        for (i = 0; i < n; i++) {
            float x = 0.0f;
            bed_wait -= RV_DT;
            if (bed_wait <= 0.0f) {                   /* a grain of dust */
                float u = xs_uni(&bed_rng);
                x = 0.25f + 0.75f * xs_uni(&bed_rng);
                bed_wait = -0.6931472f * fm_log2f(u > 0.001f ? u : 0.001f) / hz;
                rv_live.bed_hit++;
            }
            bed_nlp += (xs_bi(&bed_rng) - bed_nlp) * 0.05f;   /* and a breath of soft noise */
            xin[i] = x + bed_nlp * breath * 8.0f;
            yl[i] = yr[i] = 0.0f;
        }
        for (j = 0; j < nlive; j++) {
            float b1, b2, g, pl, pr, y1, y2;
            k = live[j];
            b1 = bed_b1[k];
            b2 = bed_b2[k];
            g = bed_g[k];
            pl = bed_pan[k];
            pr = 1.0f - bed_pan[k];
            y1 = bed_y1[k];
            y2 = bed_y2[k];
            if (k >= 6) {                             /* the shimmer, beating */
                for (i = 0; i < n; i++) {
                    float re = b1 * y1 - b2 * y2 + g * xin[i], im = b2 * y1 + b1 * y2, o = im * trem;
                    y1 = re;
                    y2 = im;
                    yl[i] += o * pl;
                    yr[i] += o * pr;
                }
            } else {
                for (i = 0; i < n; i++) {
                    float re = b1 * y1 - b2 * y2 + g * xin[i], im = b2 * y1 + b1 * y2;
                    y1 = re;
                    y2 = im;
                    yl[i] += im * pl;
                    yr[i] += im * pr;
                }
            }
            bed_y1[k] = y1;
            bed_y2[k] = y2;
        }
        for (i = 0; i < n; i++) {
            float a, b;
            bed_lvl += (lvl - bed_lvl) * 0.002f;
            bed_gain += (want - bed_gain) * 0.0008f;
            a = sat(yl[i] * 2.0f) * bed_lvl * bed_lvl * bed_gain;
            b = sat(yr[i] * 2.0f) * bed_lvl * bed_lvl * bed_gain;
            l[i] += a;
            r[i] += b;
            rev_in[i] += (a + b) * send;
            env = fm_maxf(fm_fabsf(a + b) * 0.5f, env * 0.9995f);
        }
    }
    rv_live.bed_env = env;
    rv_live.bed_duck = bed_gain;
}

/* ---------------------------------------------------------- microcuts --- */
/* a sliver of the master, repeated. Its shape: EVEN (as cut), BOUNCE (each repeat shorter and a
 * little higher, a ball settling) or DRAG (each slower and lower, a tape winding down). While a cut
 * plays the ring stops recording, so every repeat is of the same sliver */
static int16_t cut_buf[RV_CUT_SAMPLES] RV_RAMBSS;   /* (in plain RAM: the pool keeps its room) */
static int32_t cut_w, cut_start, cut_seg, cut_reps, cut_fade;
static float cut_pos, cut_rate;
static uint8_t cut_rev, cut_shape;
static uint32_t cut_rng = 0xA5A5A5u;

/* action: bits 0-1 LOOP, REVERSE, STUTTER; 2-3 the shape (EVEN, BOUNCE, DRAG); 4-6 the size
 * (0: the MASTER page's CUT SIZE, 1-7: 1/32, 1/16, 1/8, 1/6, 1/4, 1/3, 1/2 of step_s) */
static void cut_now(int action, float step_s)
{
    static const float frac[8] = {0.0f, 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.5f};
    int type = action & 3, size = (action >> 4) & 7;
    float sz = size && step_s > 0.0f ? step_s * frac[size] : 0.008f + (float)rv.gp[GP_CUTSIZE] * (0.08f / 127.0f);
    int32_t back;
    if (type == 0)
        return;
    cut_shape = (uint8_t)clampi((action >> 2) & 3, 0, 2);
    cut_seg = clampi((int)(sz * (float)RV_FS), 64, (int)RV_CUT_SAMPLES / 2);
    back = type == 3 ? cut_seg * (1 + (int32_t)(xs32(&cut_rng) % 3u)) : cut_seg;
    if (back > (int32_t)RV_CUT_SAMPLES - 2)
        back = cut_seg;
    cut_start = cut_w - back;
    while (cut_start < 0)
        cut_start += (int32_t)RV_CUT_SAMPLES;
    cut_rev = type == 2;
    cut_pos = 0.0f;
    cut_rate = 1.0f;
    cut_reps = clampi(rv.gp[GP_CUTREP], 1, 16) + (cut_shape == 1 ? 3 : 0);   /* a bounce settles: a few more */
    cut_fade = clampi(cut_seg / 8, 16, 400);
}

/* in place on the master: writes the ring, plays the cut over it */
static void cuts_run(float *l, float *r, int n)
{
    int i;
    float mix = gv(GP_CUTMIX) * (1.0f / 127.0f);
    for (i = 0; i < n; i++) {
        if (cut_reps <= 0) {
            cut_buf[cut_w] = (int16_t)clampf((l[i] + r[i]) * 8192.0f, -32000.0f, 32000.0f);
            cut_w = (cut_w + 1) % (int32_t)RV_CUT_SAMPLES;
            continue;
        }
        {
            float p = cut_rev ? (float)cut_seg - 1.0f - cut_pos : cut_pos, fr, a, b, c, e;
            int32_t ip = (int32_t)p, i0, i1;
            if (ip < 0)
                ip = 0;
            fr = p - (float)ip;
            i0 = (cut_start + ip) % (int32_t)RV_CUT_SAMPLES;
            i1 = (i0 + 1) % (int32_t)RV_CUT_SAMPLES;
            a = cut_buf[i0];
            b = cut_buf[i1];
            c = (a + (b - a) * fr) * (1.0f / 16384.0f);
            e = fm_minf(1.0f, fm_minf((cut_pos + 1.0f) / (float)cut_fade, ((float)cut_seg - cut_pos) / (float)cut_fade));
            if (e < 0.0f)
                e = 0.0f;
            l[i] = lerpf(l[i], c * e, mix);
            r[i] = lerpf(r[i], c * e, mix);
            cut_pos += cut_rate;
            if (cut_pos >= (float)cut_seg) {          /* the next repeat */
                cut_pos = 0.0f;
                cut_reps--;
                if (cut_shape == 1) {
                    cut_seg = cut_seg * 3 / 4 > 64 ? cut_seg * 3 / 4 : 64;
                    cut_rate *= 1.06f;
                    cut_fade = clampi(cut_seg / 8, 16, 400);
                } else if (cut_shape == 2) {
                    cut_rate = fm_maxf(0.25f, cut_rate * 0.84f);
                }
            }
        }
    }
}

/* clear every bus's memory: after a fault (see rv_engine.c), so a blown-up state cannot persist */
static void buses_reset(void)
{
    __builtin_memset(fb_buf, 0, sizeof(fb_buf));
    __builtin_memset(&fb_f, 0, sizeof(fb_f));
    __builtin_memset(fb_comb, 0, sizeof(fb_comb));
    __builtin_memset(fb_ap, 0, sizeof(fb_ap));
    __builtin_memset(fb_hi, 0, sizeof(fb_hi));
    __builtin_memset(fb_hq, 0, sizeof(fb_hq));
    fb_env = fb_dcx = fb_dcy = fb_comblp = fb_shph = fb_crush = 0.0f;
    __builtin_memset(fb_bp, 0, sizeof(fb_bp));
    __builtin_memset(dly_buf, 0, sizeof(dly_buf));
    dly_lp = dly_fbv = dly_half = dly_age_lp = 0.0f;
    __builtin_memset(space_mem, 0, sizeof(space_mem));
    __builtin_memset(grains, 0, sizeof(grains));
    __builtin_memset(hr_w, 0, sizeof hr_w);
    __builtin_memset(hr_lp, 0, sizeof hr_lp);
    __builtin_memset(hr_out, 0, sizeof hr_out);
    hr_in = hr_hp = 0.0f;
    gr_lpl = gr_lpr = 0.0f;
    __builtin_memset(cut_buf, 0, sizeof(cut_buf));
    cut_reps = 0;
    __builtin_memset(bed_y1, 0, sizeof(bed_y1));
    __builtin_memset(bed_y2, 0, sizeof(bed_y2));
    bed_key[0] = -1;
}

static void buses_init(void)
{
    int i;
    for (i = 0; i < (int)RV_DELAY_SAMPLES; i++)  /* (the FM-1's cache RAM is not cleared at start) */
        dly_buf[i] = 0;
    fb_dt = 0.0f;
    fb_edge = 1.0f;
    dly_t = 0.0f;
}
