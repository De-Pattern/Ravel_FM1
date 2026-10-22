/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the shared buses. The material vocoder (Silt's favourite idea: one sound's spectrum
 * played on another's material), the feedback bus (a delay loop with a processor inside and an
 * edge guard, after Silt's feedback network), the filter pair (two filters with their own LFOs,
 * in series, parallel or feeding each other), a tape-style delay, a small space, and the
 * microcuts (Silt: slivers of the output looped, reversed, stuttered).
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

/* a bus setting as the matrix and the macros move it (GLOBAL destinations) */
static inline float gv(int g) { return geff[g]; }

/* ---------------------------------------------------------- vocoder --- */
static const float voc_hz[8] = {140.0f, 220.0f, 340.0f, 520.0f, 800.0f, 1250.0f, 2600.0f, 6000.0f};
static svf_t voc_an[8], voc_sy[8];
static float voc_env[8], voc_cenv[8];
static svf_t drop_f[2];
static float drop_env, drop_ph, drop_hz;
static uint32_t voc_rng = 0x51ED27u, dust_rng2 = 0xC0FFEEu;
static float voc_dust_acc;

static int16_t voc_set_shift = -999, voc_set_q = -999;
static void vocoder_setup(void)
{
    int b;
    float sh = fm_exp2f(rv.gp[GP_VOCSHIFT] * (1.0f / 12.0f)), qm = fm_exp2f((rv.gp[GP_VOCQ] - 64) * (1.0f / 40.0f));
    voc_set_shift = rv.gp[GP_VOCSHIFT];
    voc_set_q = rv.gp[GP_VOCQ];
    for (b = 0; b < 8; b++) {
        svf_set(&voc_an[b], voc_hz[b], (b < 5 ? 6.0f : 3.5f) * qm);
        svf_set(&voc_sy[b], voc_hz[b] * sh, (b < 5 ? 9.0f : 4.0f) * qm);   /* SHIFT moves the voice's bands */
    }
    svf_set(&drop_f[0], 2400.0f, 3.0f);
    svf_set(&drop_f[1], 5200.0f, 2.0f);
}

/* in: the analysed sound (track sends), body: the carrier material; adds to out (mono) */
static void vocoder_run(const float *in, const float *body_track, int n, float *out)
{
    int i, b, which = clampi(rv.gp[GP_VOCBODY], 0, 11);
    float rel = 0.0015f / (0.2f + rv.gp[GP_VOCREL] * (3.0f / 127.0f)), tilt = gv(GP_VOCTILT) * (1.0f / 64.0f);
    float lvl = gv(GP_VOCLEVEL) * (1.0f / 127.0f);
    float att = 0.2f / (1.0f + rv.gp[GP_VOCATK] * 0.5f);
    if (voc_set_shift != rv.gp[GP_VOCSHIFT] || voc_set_q != rv.gp[GP_VOCQ])
        vocoder_setup();
    for (i = 0; i < n; i++) {
        float body = 0.0f, y = 0.0f, x = in[i];
        switch (which) {
        case 0:                                       /* dust: sparse clicks through two bands */
            voc_dust_acc += 900.0f * RV_DT;
            if (xs_uni(&dust_rng2) < 900.0f * RV_DT) {
                body = xs_bi(&dust_rng2);
            }
            body = svf_tick(&drop_f[1], body, 0, 0) * 3.0f + body * 0.5f;
            break;
        case 1:                                       /* droplets: little falling chirps */
            if (xs_uni(&dust_rng2) < 60.0f * RV_DT) {
                drop_env = 1.0f;
                drop_hz = 900.0f + xs_uni(&dust_rng2) * 3000.0f;
            }
            drop_ph += drop_hz * RV_DT;
            drop_ph -= fm_floorf(drop_ph);
            drop_hz *= 1.00008f;
            body = sin_lut(drop_ph) * drop_env + xs_bi(&voc_rng) * 0.05f;
            drop_env *= 0.9985f;
            break;
        case 2: body = xs_bi(&voc_rng); break;
        default: body = body_track ? body_track[i] : 0.0f; break;
        }
        for (b = 0; b < 8; b++) {
            float bp, sb, a, w = 1.0f + tilt * ((float)b - 3.5f) * 0.25f;
            float c, g;
            svf_tick(&voc_an[b], x, &bp, 0);
            a = fm_fabsf(bp * voc_an[b].k);             /* k: unity gain at the band's centre */
            voc_env[b] += (a - voc_env[b]) * (a > voc_env[b] ? att : rel);
            svf_tick(&voc_sy[b], body, &sb, 0);
            sb *= voc_sy[b].k;
            /* the body is levelled band by band, so the output follows the analysed sound's level
             * (not input x body), whatever the body is: a quiet track, noise or sparse dust */
            c = fm_fabsf(sb);
            voc_cenv[b] += (c - voc_cenv[b]) * (c > voc_cenv[b] ? 0.004f : 0.0004f);
            g = 1.0f / (voc_cenv[b] + 0.004f);
            if (g > 24.0f)
                g = 24.0f;
            y += sb * g * voc_env[b] * (w > 0.0f ? w : 0.0f);
        }
        y *= lvl * 3.0f;
        out[i] += y / (1.0f + fm_fabsf(y) * 0.5f);        /* soft ceiling: never a hard edge */
    }
    for (b = 0; b < 8; b++)
        rv_live.voc_band[b] = voc_env[b];
}

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

static inline float fb_read(float d)
{
    float rp = (float)fb_w - d, fr;
    int32_t i0, i1;
    while (rp < 0.0f)
        rp += (float)RV_FB_SAMPLES;
    i0 = (int32_t)rp;
    fr = rp - (float)i0;
    i1 = (i0 + 1) % (int32_t)RV_FB_SAMPLES;
    return (fb_buf[i0] + (fb_buf[i1] - fb_buf[i0]) * fr) * (1.0f / 16384.0f);
}

static inline float fb_insert(float x, int proc, float amt, float tune)
{
    int k;
    switch (proc) {
    case 1: {                                         /* comb, 40 Hz .. 5 kHz */
        float fr = 40.0f * fm_exp2f(tune * 7.0f), d = clampf(44100.0f / fr, 2.0f, 1020.0f), z, y;
        int32_t i0 = fb_cw - (int32_t)d;
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
        static const int lens[4] = {61, 113, 173, 241};
        float g = amt * 0.75f, y = x;
        for (k = 0; k < 4; k++) {
            int L = clampi((int)((float)lens[k] * (0.3f + tune * 0.7f)), 2, 255);
            int ri = (fb_api[k] - L + 256) & 255;
            float z = fb_ap[k][ri] * (1.0f / 16384.0f), v = y + z * g;
            fb_ap[k][fb_api[k]] = (int16_t)clampf(v * 16384.0f, -32000.0f, 32000.0f);
            y = z - v * g;
            fb_api[k] = (fb_api[k] + 1) & 255;
        }
        return y;
    }
    case 3: {                                         /* frequency shift, -200 .. +200 Hz */
        float c = (tune - 0.5f) * 2.0f, ac = fm_fabsf(c), hz = 0.0f, ai = x, aq = x, s;
        if (ac > 0.03f) {
            hz = 0.5f * fm_powf(400.0f, (ac - 0.03f) / 0.97f);
            if (c < 0.0f)
                hz = -hz;
        }
        for (k = 0; k < 4; k++) {
            ai = ap2(fb_hi[k], hil_ci[k], ai);
            aq = ap2(fb_hq[k], hil_cq[k], aq);
        }
        s = fb_hz1;
        fb_hz1 = ai;
        fb_shph += hz * RV_DT;
        fb_shph -= fm_floorf(fb_shph);
        s = s * sin_lut(fb_shph + 0.25f) + aq * sin_lut(fb_shph);
        return lerpf(x, s, amt);
    }
    case 4:
        return lerpf(x, fm_sinf(x * (1.0f + amt * 8.0f * (0.5f + tune))), amt);
    case 5: {
        int hold = 1 + (int)(tune * tune * 40.0f);
        float q = fm_exp2f(15.0f - amt * 13.0f);
        if (++fb_crc >= hold) {
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
static void feedback_run(const float *in, int n, float *out)
{
    int i, proc = clampi(rv.gp[GP_FBPROC], 0, 5);
    float target = 40.0f + fm_exp2f(gv(GP_FBTIME) * (7.2f / 127.0f)) * 40.0f;   /* samples: ~2 ms .. 0.14 s */
    float regen = gv(GP_FBREGEN) * (1.05f / 127.0f), amt = gv(GP_FBAMT) * (1.0f / 127.0f);
    float tune = gv(GP_FBTUNE) * (1.0f / 127.0f), lvl = gv(GP_FBLEVEL) * (1.0f / 127.0f);
    int edge = rv.gp[GP_FBEDGE];
    if (target > (float)RV_FB_SAMPLES - 4.0f)
        target = (float)RV_FB_SAMPLES - 4.0f;
    svf_set(&fb_f, rv_cut_hz((float)gv(GP_FBCUT)), 0.9f);
    if (fb_dt <= 0.0f)
        fb_dt = target;
    for (i = 0; i < n; i++) {
        float dly, ff, y, d, a, r = regen;
        fb_dt += (target - fb_dt) * 0.0008f;
        dly = fb_read(fb_dt);
        ff = proc == 0 ? svf_tick(&fb_f, dly, 0, 0) : fb_insert(svf_tick(&fb_f, dly, 0, 0), proc, amt, tune);
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
        fb_w = (fb_w + 1) % (int32_t)RV_FB_SAMPLES;
        out[i] = d * lvl;
    }
    rv_live.fb_env = fb_env;
}

/* -------------------------------------------------------- filter pair --- */
static svf_t pair_f[2];
static float pair_ph[2], pair_prev;

/* one of the pair: low, band or high pass (TYPE 1, TYPE 2) */
static float pair_tick(int k, float x)
{
    float bp, hp, lp = svf_tick(&pair_f[k], x, &bp, &hp);
    int ty = rv.gp[k ? GP_PAIRT2 : GP_PAIRT1];
    return ty == 0 ? lp : ty == 1 ? bp : hp;
}

/* in: mono; adds stereo to l, r */
static void pair_run(const float *in, int n, float *l, float *r)
{
    int i, k, mode = clampi(rv.gp[GP_PAIRMODE], 0, 2);
    float depth = rv.gp[GP_PAIRDEPTH] * (1.0f / 127.0f), res = gv(GP_PAIRRES) * (1.0f / 127.0f);
    float lvl = gv(GP_PAIRLEVEL) * (1.0f / 127.0f) * 1.4f, q = 0.7f + res * res * 14.0f;
    float rate[2], fc[2];
    rate[0] = 0.02f * fm_exp2f(rv.gp[GP_PAIRLFO1] * (9.0f / 127.0f));
    rate[1] = 0.02f * fm_exp2f(rv.gp[GP_PAIRLFO2] * (9.0f / 127.0f));
    for (k = 0; k < 2; k++) {
        pair_ph[k] += rate[k] * RV_DT * (float)n;
        pair_ph[k] -= fm_floorf(pair_ph[k]);
        fc[k] = gv(k ? GP_PAIRF2 : GP_PAIRF1) + depth * 40.0f * sin_lut(pair_ph[k]);
        svf_set(&pair_f[k], rv_cut_hz(clampf(fc[k], 0.0f, 127.0f)), q);
    }
    for (i = 0; i < n; i++) {
        float b1, b2, x = in[i];
        if (mode == 0) {                               /* series: filter 1 into filter 2 */
            b2 = pair_tick(1, pair_tick(0, x));
            l[i] += b2 * lvl;
            r[i] += b2 * lvl;
        } else if (mode == 1) {                        /* parallel: one each side */
            b1 = pair_tick(0, x);
            b2 = pair_tick(1, x);
            l[i] += b1 * lvl;
            r[i] += b2 * lvl;
        } else {                                       /* feedback: each feeds the other */
            b1 = pair_tick(0, x + pair_prev * res * 0.9f);
            b2 = pair_tick(1, b1);
            pair_prev = sat(b2);
            l[i] += b1 * lvl;
            r[i] += b2 * lvl;
        }
    }
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
static inline float dly_read(float p)
{
    int32_t i0;
    float fr;
    while (p < 0.0f)
        p += (float)RV_DELAY_SAMPLES;
    i0 = (int32_t)p;
    fr = p - (float)i0;
    i0 %= (int32_t)RV_DELAY_SAMPLES;
    return (dly_buf[i0] + (dly_buf[(i0 + 1) % (int32_t)RV_DELAY_SAMPLES] - dly_buf[i0]) * fr) * (1.0f / 16384.0f);
}

/* the tape's saturation: unity for quiet signals, rounding the loud ones off, a little lopsided
 * (even harmonics), so it can never make the loop gain more than FEEDBK */
static inline float dly_sat(float x, float drv)
{
    return drv > 1.01f ? sat(x * drv + 0.08f * (drv - 1.0f) * x * x) / drv : x;
}

static void delay_run(const float *in, int n, float *l, float *r)
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
            dly_w = (dly_w + 1) % (int32_t)RV_DELAY_SAMPLES;
            dly_half = 0.0f;
        }
        l[i] += (a * (1.0f - gmix * 0.7f) + gr_lpl * gmix * 1.3f) * lvl + d1 * dbl * 0.8f;   /* ping left, pong right */
        r[i] += (y * (1.0f - gmix * 0.7f) + gr_lpr * gmix * 1.3f) * lvl + d2 * dbl * 0.8f;
    }
}

/* -------------------------------------------------------------- space --- */
#define REV_N 4
/* one memory for all the modes: ROOM uses the first 4096 samples, CLOUDS all of it, BLACKHOLE nearly
 * all of it at half rate, 12-BIT half of it */
#define SPACE_MEM 10496
static int16_t space_mem[SPACE_MEM] RV_BIG;
#define rev_buf ((int16_t(*)[1024])space_mem)
static const int rev_len[REV_N] = {701, 811, 907, 1009};
static int32_t rev_w[REV_N];
static float rev_lp[REV_N];
static float rev_ap[2][512];
static int32_t rev_api[2];
static float rev_mod;

/* CLOUDS: a plate tank in the manner of the reverb in Mutable Instruments' Clouds (the figure-eight
 * tank Jon Dattorro described in 1997): four input diffusers, then two halves, each a modulated
 * allpass, a delay, damping, a second allpass and a delay, feeding each other. Long, smooth and a
 * little grainy at 16 bits. DECAY (SIZE) is how long it rings, DAMP how dark, DIFFUSE how quickly
 * echoes smear, MOTION how much the tank's lines wander, LOW CUT keeps the bass out. */
enum { CL_A1, CL_A2, CL_A3, CL_A4, CL_LAP, CL_LD1, CL_LAP2, CL_LD2, CL_RAP, CL_RD1, CL_RAP2, CL_RD2, CL_N };
static const int16_t cl_len[CL_N] = {66, 50, 175, 128, 327, 2062, 833, 1722, 436, 1952, 1230, 1464};
static int16_t cl_base[CL_N], cl_w[CL_N];
static float cl_lp[2], cl_tail[2], cl_bw, cl_ph, cl_hp;

static inline float cl_rd(int k, float d)               /* d samples ago (fractional) */
{
    float p = (float)cl_w[k] - d;
    int32_t i0;
    float fr;
    while (p < 0.0f)
        p += (float)cl_len[k];
    i0 = (int32_t)p;
    fr = p - (float)i0;
    {
        int32_t a = i0 % cl_len[k], b = (i0 + 1) % cl_len[k];
        return (space_mem[cl_base[k] + a] + (space_mem[cl_base[k] + b] - space_mem[cl_base[k] + a]) * fr) * (1.0f / 8192.0f);
    }
}
static inline float cl_tap(int k, int d) { int32_t p = cl_w[k] - d; if (p < 0) p += cl_len[k]; return space_mem[cl_base[k] + p] * (1.0f / 8192.0f); }
static inline void cl_wr(int k, float x)
{
    space_mem[cl_base[k] + cl_w[k]] = (int16_t)clampf(x * 8192.0f, -32000.0f, 32000.0f);
    if (++cl_w[k] >= cl_len[k])
        cl_w[k] = 0;
}
static inline float cl_ap(int k, float x, float g, float d)   /* an allpass on line k, d samples long */
{
    float z = cl_rd(k, d), v = x + z * g;
    cl_wr(k, v);
    return z - v * g;
}

static void clouds_run(const float *in, int n, float *l, float *r)
{
    int i, k;
    float dec = 0.55f + gv(GP_REVSIZE) * (0.44f / 127.0f), damp = 0.1f + (127 - gv(GP_REVDAMP)) * (0.85f / 127.0f);
    float dif = 0.45f + rv.gp[GP_REVDIFF] * (0.35f / 127.0f), lvl = gv(GP_REVLEVEL) * (1.0f / 127.0f);
    float modd = 1.0f + gv(GP_REVMOD) * (14.0f / 127.0f);
    float lowc = rv.gp[GP_REVLOW] > 0 ? clampf(rv_cut_hz(rv.gp[GP_REVLOW] * 0.55f) * 6.2832f * RV_DT, 0.0f, 0.9f) : 0.0f;
    if (!cl_base[CL_N - 1]) {
        int b = 0;
        for (k = 0; k < CL_N; k++) {
            cl_base[k] = (int16_t)b;
            b += cl_len[k];
        }
    }
    for (i = 0; i < n; i++) {
        float x = in[i] * 0.6f, a, b, ml, mr, ol, or_;
        if (lowc > 0.0f) {
            cl_hp += (x - cl_hp) * lowc;
            x -= cl_hp;
        }
        cl_bw += (x - cl_bw) * 0.7f;                  /* the input's bandwidth */
        x = cl_ap(CL_A1, cl_bw, dif, (float)cl_len[CL_A1] - 1.0f);
        x = cl_ap(CL_A2, x, dif, (float)cl_len[CL_A2] - 1.0f);
        x = cl_ap(CL_A3, x, dif * 0.83f, (float)cl_len[CL_A3] - 1.0f);
        x = cl_ap(CL_A4, x, dif * 0.83f, (float)cl_len[CL_A4] - 1.0f);
        cl_ph += 0.7f * RV_DT;
        cl_ph -= fm_floorf(cl_ph);
        ml = modd * sin_lut(cl_ph);
        mr = modd * sin_lut(cl_ph + 0.25f);
        /* left half, fed by the right's tail */
        a = cl_ap(CL_LAP, x + cl_tail[1] * dec, -0.7f, (float)cl_len[CL_LAP] - 17.0f + ml);
        cl_wr(CL_LD1, a);
        a = cl_tap(CL_LD1, cl_len[CL_LD1] - 1);
        cl_lp[0] += (a - cl_lp[0]) * damp;
        a = cl_ap(CL_LAP2, cl_lp[0] * dec, 0.5f, (float)cl_len[CL_LAP2] - 1.0f);
        cl_wr(CL_LD2, a);
        /* right half, fed by the left's tail */
        b = cl_ap(CL_RAP, x + cl_tail[0] * dec, -0.7f, (float)cl_len[CL_RAP] - 17.0f + mr);
        cl_wr(CL_RD1, b);
        b = cl_tap(CL_RD1, cl_len[CL_RD1] - 1);
        cl_lp[1] += (b - cl_lp[1]) * damp;
        b = cl_ap(CL_RAP2, cl_lp[1] * dec, 0.5f, (float)cl_len[CL_RAP2] - 1.0f);
        cl_wr(CL_RD2, b);
        cl_tail[0] = cl_tap(CL_LD2, cl_len[CL_LD2] - 1);
        cl_tail[1] = cl_tap(CL_RD2, cl_len[CL_RD2] - 1);
        /* the outputs: taps across both halves */
        ol = cl_tap(CL_RD1, 123) + cl_tap(CL_RD1, 1377) - cl_tap(CL_RAP2, 886) + cl_tap(CL_RD2, 924)
           - cl_tap(CL_LD1, 921) - cl_tap(CL_LAP2, 87) - cl_tap(CL_LD2, 494);
        or_ = cl_tap(CL_LD1, 163) + cl_tap(CL_LD1, 1679) - cl_tap(CL_LAP2, 569) + cl_tap(CL_LD2, 1238)
            - cl_tap(CL_RD1, 977) - cl_tap(CL_RAP2, 155) - cl_tap(CL_RD2, 56);
        l[i] += ol * 0.5f * lvl;
        r[i] += or_ * 0.5f * lvl;
    }
}

/* BLACKHOLE and 12-BIT run at half the sample rate (a pair of samples in, one tank step, a pair out),
 * so the same memory holds twice the time. */
static int16_t hr_w[12];                                  /* write positions, a line each */
static int16_t hr_base[12];
static float hr_in, hr_out[2][2], hr_lp[8], hr_ph[4], hr_hp, hr_ext;   /* hr_ext: BLACKHOLE's slow swell */
static uint8_t hr_odd;
static void hr_layout(const int16_t *len, int n)
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
    int32_t a, b;
    float fr;
    while (p < 0.0f)
        p += (float)len[k];
    a = (int32_t)p;
    fr = p - (float)a;
    a %= len[k];
    b = a + 1 >= len[k] ? 0 : a + 1;
    return (space_mem[hr_base[k] + a] + (space_mem[hr_base[k] + b] - space_mem[hr_base[k] + a]) * fr) * (1.0f / 8192.0f);
}
static inline void hr_wr(const int16_t *len, int k, float x, int32_t q)   /* q: the step a sample is kept in (1: 16 bits) */
{
    int32_t v = (int32_t)clampf(x * 8192.0f, -32000.0f, 32000.0f);
    if (q > 1)
        v = v >= 0 ? v / q * q : -((-v) / q * q);         /* (truncated, as cheap converters did) */
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

/* BLACKHOLE (after Eventide's Blackhole): a vast, dark, unnatural space. Four long lines (100 to 125 ms
 * each at half rate), mixed every pass and fed back through a soft saturator, so near the top of GRAVITY
 * the tail stops dying and blooms instead, compressed and endless; MOTION swirls the lines, DIFFUSE
 * smears what goes in, DAMP darkens it, LOW CUT keeps the bass out. GRAVITY past the middle also lets
 * the reverb swell in after a note rather than leap out with it. */
static const int16_t bh_len[8] = {113, 89, 241, 197, 2129, 2339, 2549, 2729};
static void blackhole_tank(float x, float *ol, float *or_)
{
    float grav = gv(GP_REVSIZE) * (1.0f / 127.0f), g = grav < 0.85f ? 0.80f + grav * 0.22f : 0.987f + (grav - 0.85f) * 0.22f;
    float damp = 0.08f + (127 - gv(GP_REVDAMP)) * (0.62f / 127.0f), dif = 0.45f + rv.gp[GP_REVDIFF] * (0.3f / 127.0f);
    float modd = 2.0f + gv(GP_REVMOD) * (40.0f / 127.0f), o[4], sum;
    int k;
    for (k = 0; k < 4; k++)                               /* the input smeared */
        x = hr_ap(bh_len, k, x, dif, 1);
    for (k = 0; k < 4; k++) {
        hr_ph[k] += (0.07f + 0.05f * (float)k) * gv(GP_REVMOD) * (1.0f / 127.0f) * (2.0f * RV_DT) + 0.00002f;
        hr_ph[k] -= fm_floorf(hr_ph[k]);
        o[k] = hr_rd(bh_len, 4 + k, (float)bh_len[4 + k] - 2.0f - modd - modd * sin_lut(hr_ph[k]));
        hr_lp[k] += (o[k] - hr_lp[k]) * damp;
    }
    sum = (hr_lp[0] + hr_lp[1] + hr_lp[2] + hr_lp[3]) * 0.5f;   /* a Householder mix */
    for (k = 0; k < 4; k++) {
        float y = (hr_lp[k] - sum) * g + x * (k & 1 ? -0.6f : 0.6f);
        hr_wr(bh_len, 4 + k, 1.2f * sat(y * (1.0f / 1.2f)), 1);   /* the saturator that lets it bloom, never blow up */
    }
    *ol = o[0] - o[2] + 0.4f * o[1];
    *or_ = o[1] - o[3] + 0.4f * o[2];
}

/* 12-BIT (after the Alesis Quadraverb's era): a bright, metallic plate of four combs and two allpasses a
 * side, kept in 12 bits (CRUNCH takes it down to 8) at half rate with its steps held, so it lisps and
 * aliases the way early digital reverbs did. SIZE is how long it rings, DAMP how dark, MOTION a little
 * chorus in the combs. */
static const int16_t q12_len[12] = {558, 594, 639, 678, 581, 617, 662, 701, 113, 171, 124, 182};
static void q12_tank(float x, float *ol, float *or_)
{
    float fb = 0.80f + gv(GP_REVSIZE) * (0.185f / 127.0f), damp = 0.15f + (127 - gv(GP_REVDAMP)) * (0.7f / 127.0f);
    float modd = gv(GP_REVMOD) * (6.0f / 127.0f), a = 0.0f, b = 0.0f;
    int32_t q = 1 << (2 + (rv.gp[GP_REVDIFF] * 4) / 127);   /* 12 bits of full scale, down to 8 */
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

static void halfrate_run(int mode, const float *in, int n, float *l, float *r)
{
    int i;
    float lvl = gv(GP_REVLEVEL) * (1.0f / 127.0f), lowc = rv.gp[GP_REVLOW] > 0 ? clampf(rv_cut_hz(rv.gp[GP_REVLOW] * 0.55f) * 6.2832f * RV_DT, 0.0f, 0.9f) : 0.0f;
    float swell = mode == 2 && gv(GP_REVSIZE) > 64 ? (gv(GP_REVSIZE) - 64) * (1.0f / 63.0f) : 0.0f;
    if (!hr_base[1])
        hr_layout(mode == 2 ? bh_len : q12_len, mode == 2 ? 8 : 12);
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
            if (swell > 0.0f) {                           /* BLACKHOLE: it swells in after the sound, slowly */
                hr_ext += (fm_fabsf(xin) - hr_ext) * 0.0004f;
                xin *= 1.0f - swell + swell * clampf(hr_ext * 12.0f, 0.0f, 1.0f);
            }
            if (mode == 2)
                blackhole_tank(xin * 0.7f, &ol, &or_);
            else
                q12_tank(xin, &ol, &or_);
            hr_out[0][0] = hr_out[0][1];
            hr_out[1][0] = hr_out[1][1];
            hr_out[0][1] = ol;
            hr_out[1][1] = or_;
        }
        if (mode == 2) {                                  /* out smoothly (interpolated) ... */
            float u = hr_odd ? 1.0f : 0.5f;
            l[i] += (hr_out[0][0] + (hr_out[0][1] - hr_out[0][0]) * u) * lvl * 1.6f;
            r[i] += (hr_out[1][0] + (hr_out[1][1] - hr_out[1][0]) * u) * lvl * 1.6f;
        } else {                                          /* ... or held, step by step, as the old units did */
            l[i] += hr_out[0][1] * lvl * 1.2f;
            r[i] += hr_out[1][1] * lvl * 1.2f;
        }
        hr_odd ^= 1u;
    }
}

static void space_run(const float *in, int n, float *l, float *r)
{
    static int8_t last_mode = -1;
    if (rv.gp[GP_REVMODE] != last_mode) {             /* the modes share memory: start clean */
        last_mode = (int8_t)rv.gp[GP_REVMODE];
        __builtin_memset(space_mem, 0, sizeof(space_mem));
        cl_lp[0] = cl_lp[1] = cl_tail[0] = cl_tail[1] = cl_bw = cl_hp = 0.0f;
        __builtin_memset(hr_w, 0, sizeof hr_w);
        __builtin_memset(hr_base, 0, sizeof hr_base);
        __builtin_memset(hr_lp, 0, sizeof hr_lp);
        __builtin_memset(hr_out, 0, sizeof hr_out);
        hr_in = hr_hp = hr_ext = 0.0f;
    }
    if (rv.gp[GP_REVMODE] >= 2) {
        halfrate_run(rv.gp[GP_REVMODE] >= 3 ? 3 : 2, in, n, l, r);
        return;
    }
    if (rv.gp[GP_REVMODE] == 1) {
        clouds_run(in, n, l, r);
        return;
    }
    {
    int i, k;
    float size = 0.45f + gv(GP_REVSIZE) * (0.53f / 127.0f), damp = 0.15f + (127 - gv(GP_REVDAMP)) * (0.8f / 127.0f);
    float g = 0.55f + size * 0.42f, lvl = gv(GP_REVLEVEL) * (1.0f / 127.0f);
    float dif = 0.15f + rv.gp[GP_REVDIFF] * (0.6f / 127.0f), modd = gv(GP_REVMOD) * (10.0f / 127.0f);
    float lowc = rv.gp[GP_REVLOW] > 0 ? rv_cut_hz(rv.gp[GP_REVLOW] * 0.55f) * 6.2832f * RV_DT : 0.0f;
    static float rev_hp;
    rev_mod += 0.4f * RV_DT * (float)n;               /* MOTION: the line lengths drift slowly */
    rev_mod -= fm_floorf(rev_mod);
    for (i = 0; i < n; i++) {
        float x = in[i], o[REV_N], s, y;
        if (lowc > 0.0f) {                             /* LOW CUT on what goes in */
            rev_hp += (x - rev_hp) * clampf(lowc, 0.0f, 0.9f);
            x -= rev_hp;
        }
        /* input diffusion: two allpasses (DIFFUSE) */
        for (k = 0; k < 2; k++) {
            int L = k ? 337 : 211, ri = (rev_api[k] - L + 512) & 511;
            float z = rev_ap[k][ri], v = x + z * dif;
            rev_ap[k][rev_api[k]] = v;
            x = z - v * dif;
            rev_api[k] = (rev_api[k] + 1) & 511;
        }
        for (k = 0; k < REV_N; k++) {
            int L = (int)((float)rev_len[k] * size + modd * sin_lut(rev_mod + (float)k * 0.25f)), ri = rev_w[k] - L;
            if (ri < 0)
                ri += 1024;
            o[k] = rev_buf[k][ri] * (1.0f / 16384.0f);
            rev_lp[k] += (o[k] - rev_lp[k]) * damp;
        }
        /* a Householder mix of four lines */
        s = (rev_lp[0] + rev_lp[1] + rev_lp[2] + rev_lp[3]) * 0.5f;
        for (k = 0; k < REV_N; k++) {
            y = (rev_lp[k] - s) * g + x * 0.35f;
            rev_buf[k][rev_w[k]] = (int16_t)clampf(y * 16384.0f, -32000.0f, 32000.0f);
            rev_w[k] = (rev_w[k] + 1) & 1023;
        }
        l[i] += (o[0] + o[2]) * lvl * 0.7f;
        r[i] += (o[1] + o[3]) * lvl * 0.7f;
    }
}
}

/* -------------------------------------------------------------- bands --- */
/* BANDS: eight resonant bandpass filters at fixed frequencies, after the Moog MuRF. Each band
 * has its own level (sculpt the spectrum), all share RESO, and SHIFT moves the whole set.
 * The sweep is a spotlight that travels across the bands: SWEEP picks its path (up, down,
 * ping-pong, random, or stepping band to band like the MuRF's patterns), RATE how fast, WIDTH
 * how many bands it lights at once, DEPTH how dark the unlit bands go (0: no sweep, just the
 * sculpted bands). Odd bands lean left and even ones right, so the sweep crosses the stereo. */
static const float band_hz[8] = {150.0f, 240.0f, 380.0f, 600.0f, 950.0f, 1500.0f, 2400.0f, 3800.0f};
static svf_t band_f[8];
static float band_gs[8], band_ph, band_pos, band_k = 1.0f;
static uint32_t band_rng = 0x3A7Fu;
static int16_t band_set_q = -999, band_set_shift = -999;

static void bands_run(const float *in, int n, float *l, float *r)
{
    int i, k, shape = clampi(rv.gp[GP_BANDSHAPE], 0, 5), wrapped;
    float lvl = gv(GP_BANDLEVEL) * (1.0f / 127.0f) * 1.6f, depth = rv.gp[GP_BANDDEPTH] * (1.0f / 127.0f);
    float width = 0.6f + rv.gp[GP_BANDWIDTH] * (3.4f / 127.0f), rate = 0.03f * fm_exp2f(rv.gp[GP_BANDRATE] * (9.5f / 127.0f));
    float target[8], sm = 1.0f - fm_expf(-(float)n * RV_DT / 0.004f), gl[8];
    if (rv.gp[GP_BANDQ] != band_set_q || rv.gp[GP_BANDSHIFT] != band_set_shift) {
        float q = 2.0f + rv.gp[GP_BANDQ] * rv.gp[GP_BANDQ] * (40.0f / 16129.0f), sh = fm_exp2f(rv.gp[GP_BANDSHIFT] * (1.0f / 12.0f));
        band_set_q = rv.gp[GP_BANDQ];
        band_set_shift = rv.gp[GP_BANDSHIFT];
        for (k = 0; k < 8; k++)
            svf_set(&band_f[k], band_hz[k] * sh, q);
        band_k = band_f[0].k;                         /* k x band = unity at the peak */
    }
    /* the spotlight */
    band_ph += rate * (float)n * RV_DT;
    wrapped = band_ph >= 1.0f;
    band_ph -= fm_floorf(band_ph);
    switch (shape) {
    case 0: band_pos = band_ph * 8.0f; break;                                     /* UP */
    case 1: band_pos = 8.0f - band_ph * 8.0f; break;                              /* DOWN */
    case 2: band_pos = (band_ph < 0.5f ? band_ph * 2.0f : 2.0f - band_ph * 2.0f) * 7.0f; break;   /* PINGPNG */
    case 3: if (wrapped) band_pos = (float)(xs32(&band_rng) % 8u); break;          /* RANDOM: a new band each cycle */
    case 4: band_pos = fm_floorf(band_ph * 8.0f); break;                          /* STEP UP: eight steps a cycle */
    default:                                                                      /* STEP RND */
        if ((int)(band_ph * 8.0f) != (int)((band_ph - rate * (float)n * RV_DT) * 8.0f) || wrapped)
            band_pos = (float)(xs32(&band_rng) % 8u);
        break;
    }
    for (k = 0; k < 8; k++) {
        float d = fm_fabsf((float)k - band_pos), spot;
        if (shape == 0 || shape == 1 || shape == 4)   /* the paths that wrap: 8 sits next to 1 */
            d = fm_minf(d, 8.0f - d);
        spot = clampf(1.0f - d / width, 0.0f, 1.0f);
        spot = spot * spot * (3.0f - 2.0f * spot);
        target[k] = rv.gp[GP_BAND1 + k] * (1.0f / 127.0f) * (1.0f - depth + depth * spot);
        band_gs[k] += (target[k] - band_gs[k]) * sm;
        gl[k] = band_gs[k] * band_k;
        rv_live.band_g[k] = band_gs[k];
    }
    for (i = 0; i < n; i++) {
        float x = in[i], sl = 0.0f, sr = 0.0f, bp;
        for (k = 0; k < 8; k++) {
            svf_tick(&band_f[k], x, &bp, 0);
            if (k & 1)
                sr += bp * gl[k], sl += bp * gl[k] * 0.4f;
            else
                sl += bp * gl[k], sr += bp * gl[k] * 0.4f;
        }
        l[i] += sat(sl * lvl * 0.6f) * 1.4f;
        r[i] += sat(sr * lvl * 0.6f) * 1.4f;
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

/* l, r: the tracks so far (it listens to them to duck); adds itself to them and to the space's input */
static void bed_run(float *rev_in, float *l, float *r, int n)
{
    float lvl = gv(GP_BEDLEVEL) * (1.0f / 127.0f), dens = gv(GP_BEDDENS) * (1.0f / 127.0f);
    float duck = gv(GP_BEDDUCK) * (1.0f / 127.0f), send = gv(GP_BEDSPACE) * (1.0f / 127.0f), act = 0.0f, want;
    float hz = 0.4f + dens * dens * 40.0f, breath = 0.004f + dens * 0.012f;
    int i, k;
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
        bed_setup();
    }
    for (i = 0; i < n; i++)
        act += fm_fabsf(l[i]) + fm_fabsf(r[i]);
    act /= (float)n;
    bed_follow += (act - bed_follow) * (act > bed_follow ? 0.25f : 0.012f);   /* quick to duck, slow to come back */
    want = 1.0f - duck * 0.92f * clampf(bed_follow * 5.0f, 0.0f, 1.0f);   /* (at most down about 22 dB) */
    bed_ph += (float)n * RV_DT * 0.11f;
    bed_ph -= fm_floorf(bed_ph);
    for (i = 0; i < n; i++) {
        float x = 0.0f, yl = 0.0f, yr = 0.0f, trem = 0.75f + 0.25f * sin_lut(bed_ph);
        bed_wait -= RV_DT;
        if (bed_wait <= 0.0f) {                       /* a grain of dust */
            float u = xs_uni(&bed_rng);
            x = 0.25f + 0.75f * xs_uni(&bed_rng);
            bed_wait = -0.6931472f * fm_log2f(u > 0.001f ? u : 0.001f) / hz;
            rv_live.bed_hit++;
        }
        bed_nlp += (xs_bi(&bed_rng) - bed_nlp) * 0.05f;   /* and a breath of soft noise */
        x += bed_nlp * breath * 8.0f;
        bed_lvl += (lvl - bed_lvl) * 0.002f;
        bed_gain += (want - bed_gain) * 0.0008f;
        for (k = 0; k < BED_M; k++) {
            float re = bed_b1[k] * bed_y1[k] - bed_b2[k] * bed_y2[k] + bed_g[k] * x;
            float im = bed_b2[k] * bed_y1[k] + bed_b1[k] * bed_y2[k], o = im * (k >= 6 ? trem : 1.0f);
            bed_y1[k] = re;
            bed_y2[k] = im;
            yl += o * bed_pan[k];
            yr += o * (1.0f - bed_pan[k]);
        }
        yl = sat(yl * 2.0f) * bed_lvl * bed_lvl * bed_gain;
        yr = sat(yr * 2.0f) * bed_lvl * bed_lvl * bed_gain;
        l[i] += yl;
        r[i] += yr;
        rev_in[i] += (yl + yr) * send;
        rv_live.bed_env = fm_maxf(fm_fabsf(yl + yr) * 0.5f, rv_live.bed_env * 0.9995f);
    }
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
    __builtin_memset(voc_an, 0, sizeof(voc_an));
    __builtin_memset(voc_sy, 0, sizeof(voc_sy));
    __builtin_memset(voc_env, 0, sizeof(voc_env));
    __builtin_memset(voc_cenv, 0, sizeof(voc_cenv));
    __builtin_memset(drop_f, 0, sizeof(drop_f));
    drop_env = drop_ph = 0.0f;
    voc_dust_acc = 0.0f;
    __builtin_memset(fb_buf, 0, sizeof(fb_buf));
    __builtin_memset(&fb_f, 0, sizeof(fb_f));
    __builtin_memset(fb_comb, 0, sizeof(fb_comb));
    __builtin_memset(fb_ap, 0, sizeof(fb_ap));
    __builtin_memset(fb_hi, 0, sizeof(fb_hi));
    __builtin_memset(fb_hq, 0, sizeof(fb_hq));
    fb_env = fb_dcx = fb_dcy = fb_comblp = fb_shph = fb_crush = 0.0f;
    __builtin_memset(pair_f, 0, sizeof(pair_f));
    __builtin_memset(band_f, 0, sizeof(band_f));
    __builtin_memset(band_gs, 0, sizeof(band_gs));
    band_set_q = band_set_shift = -999;
    pair_ph[0] = pair_ph[1] = pair_prev = 0.0f;
    __builtin_memset(dly_buf, 0, sizeof(dly_buf));
    dly_lp = dly_fbv = dly_half = dly_age_lp = 0.0f;
    __builtin_memset(space_mem, 0, sizeof(space_mem));
    __builtin_memset(grains, 0, sizeof(grains));
    cl_lp[0] = cl_lp[1] = cl_tail[0] = cl_tail[1] = cl_bw = cl_hp = 0.0f;
    gr_lpl = gr_lpr = 0.0f;
    __builtin_memset(rev_lp, 0, sizeof(rev_lp));
    __builtin_memset(rev_ap, 0, sizeof(rev_ap));
    __builtin_memset(cut_buf, 0, sizeof(cut_buf));
    cut_reps = 0;
    __builtin_memset(bed_y1, 0, sizeof(bed_y1));
    __builtin_memset(bed_y2, 0, sizeof(bed_y2));
    bed_key[0] = -1;
    vocoder_setup();
}

static void buses_init(void)
{
    int i;
    for (i = 0; i < (int)RV_DELAY_SAMPLES; i++)  /* (the FM-1's cache RAM is not cleared at start) */
        dly_buf[i] = 0;
    vocoder_setup();
    fb_dt = 0.0f;
    fb_edge = 1.0f;
    dly_t = 0.0f;
}
