/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the shared buses. The material vocoder (Silt's favourite idea: one sound's spectrum
 * played on another's material), the feedback bus (a delay loop with a processor inside and an
 * edge guard, after Silt's feedback network), the filter pair (two filters with their own LFOs,
 * in series, parallel or feeding each other), a tape-style delay, a small space, and the
 * microcuts (Silt: slivers of the output looped, reversed, stuttered).
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

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
    float rel = 0.0015f / (0.2f + rv.gp[GP_VOCREL] * (3.0f / 127.0f)), tilt = rv.gp[GP_VOCTILT] * (1.0f / 64.0f);
    float lvl = rv.gp[GP_VOCLEVEL] * (1.0f / 127.0f);
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
    float target = 40.0f + fm_exp2f(rv.gp[GP_FBTIME] * (7.2f / 127.0f)) * 40.0f;   /* samples: ~2 ms .. 0.14 s */
    float regen = rv.gp[GP_FBREGEN] * (1.05f / 127.0f), amt = rv.gp[GP_FBAMT] * (1.0f / 127.0f);
    float tune = rv.gp[GP_FBTUNE] * (1.0f / 127.0f), lvl = rv.gp[GP_FBLEVEL] * (1.0f / 127.0f);
    int edge = rv.gp[GP_FBEDGE];
    if (target > (float)RV_FB_SAMPLES - 4.0f)
        target = (float)RV_FB_SAMPLES - 4.0f;
    svf_set(&fb_f, rv_cut_hz((float)rv.gp[GP_FBCUT]), 0.9f);
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
    float depth = rv.gp[GP_PAIRDEPTH] * (1.0f / 127.0f), res = rv.gp[GP_PAIRRES] * (1.0f / 127.0f);
    float lvl = rv.gp[GP_PAIRLEVEL] * (1.0f / 127.0f) * 1.4f, q = 0.7f + res * res * 14.0f;
    float rate[2], fc[2];
    rate[0] = 0.02f * fm_exp2f(rv.gp[GP_PAIRLFO1] * (9.0f / 127.0f));
    rate[1] = 0.02f * fm_exp2f(rv.gp[GP_PAIRLFO2] * (9.0f / 127.0f));
    for (k = 0; k < 2; k++) {
        pair_ph[k] += rate[k] * RV_DT * (float)n;
        pair_ph[k] -= fm_floorf(pair_ph[k]);
        fc[k] = (float)rv.gp[k ? GP_PAIRF2 : GP_PAIRF1] + depth * 40.0f * sin_lut(pair_ph[k]);
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
static int16_t dly_buf[RV_DELAY_SAMPLES] RV_CACHE_RAM;
static int32_t dly_w;
static float dly_lp, dly_t, dly_fbv;
static uint8_t dly_freeze;

/* GRAINS (after Mutable Instruments' Beads, lofi): the delay's memory becomes a grain buffer,
 * written at half rate and 12 bits (about 0.55 s), and up to eight grains read from it. POSITN is
 * how far back they reach, DENSITY how often they start, SIZE how long each lasts, PITCH their
 * transposition, RANDOM how much each one strays in place and pitch, STEREO how wide they scatter.
 * FEEDBK sends the cloud back into the buffer; FREEZE stops writing, so the cloud plays what it
 * holds. */
#define GR_N 8
typedef struct { float pos, inc, t, dt, pl, pr; uint8_t on; } grain_t;
static grain_t grains[GR_N];
static float gr_acc, gr_half, gr_lpl, gr_lpr, gr_fbv;
static uint8_t gr_ph;
static uint32_t gr_rng = 0x6EA1u;

static inline float gr_read(float p)
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

static void grains_run(const float *in, int n, float *l, float *r)
{
    int i, k;
    float fb = rv.gp[GP_DLYFB] * (0.9f / 127.0f), tone = 0.05f + rv.gp[GP_DLYTONE] * (0.9f / 127.0f);
    float lvl = rv.gp[GP_DLYLEVEL] * (1.0f / 127.0f) * 1.3f, posn = rv.gp[GP_DLYTIME] * (1.0f / 127.0f);
    float stereo = rv.gp[GP_DLYSPREAD] * (1.0f / 127.0f), rnd = rv.gp[GP_DLYWOB] * (1.0f / 127.0f);
    float rate = 0.7f * fm_exp2f(rv.gp[GP_GRDENS] * (6.5f / 127.0f));          /* 0.7 to 64 grains a second */
    float size = 0.02f * fm_exp2f(rv.gp[GP_GRSIZE] * (4.3f / 127.0f));         /* 20 ms to 0.4 s */
    float span = (float)RV_DELAY_SAMPLES - 64.0f;
    int frz = dly_freeze || rv.gp[GP_DLYFREEZE];
    for (i = 0; i < n; i++) {
        float x = (frz ? 0.0f : in[i]) + gr_fbv * fb, ol = 0.0f, or_ = 0.0f;
        /* write at half rate: average two samples, keep 12 bits */
        gr_half += x * 0.5f;
        if (++gr_ph >= 2) {
            gr_ph = 0;
            if (!frz) {
                int v = (int)clampf(sat(gr_half) * 16384.0f, -32000.0f, 32000.0f);
                dly_buf[dly_w] = (int16_t)(v & ~15);
                dly_w = (dly_w + 1) % (int32_t)RV_DELAY_SAMPLES;
            }
            gr_half = 0.0f;
        }
        /* start grains */
        gr_acc += rate * RV_DT;
        if (gr_acc >= 1.0f) {
            gr_acc -= 1.0f;
            for (k = 0; k < GR_N; k++)
                if (!grains[k].on)
                    break;
            if (k < GR_N) {
                grain_t *g = &grains[k];
                float back = 0.02f + posn * 0.95f + rnd * 0.4f * xs_bi(&gr_rng);          /* of the buffer */
                float semi = (float)rv.gp[GP_GRPITCH] + rnd * 7.0f * xs_bi(&gr_rng);
                float len = size * (1.0f + rnd * 0.5f * xs_bi(&gr_rng)), pan = 0.5f + stereo * 0.5f * xs_bi(&gr_rng);
                g->inc = 0.5f * fm_exp2f(semi * (1.0f / 12.0f));                       /* the buffer runs at half rate */
                back = clampf(back, 0.0f, 0.98f) * span;
                if (g->inc > 0.5f && back < g->inc * len * (float)RV_FS)          /* a grain that reads faster than */
                    back = g->inc * len * (float)RV_FS + 8.0f;                      /* the buffer fills starts further back */
                g->pos = (float)dly_w - clampf(back, 8.0f, span);
                g->t = 0.0f;
                g->dt = RV_DT / clampf(len, 0.01f, 0.6f);
                g->pl = fm_sqrtf(1.0f - pan);
                g->pr = fm_sqrtf(pan);
                g->on = 1;
            }
        }
        for (k = 0; k < GR_N; k++) {
            grain_t *g = &grains[k];
            float w, y;
            if (!g->on)
                continue;
            w = 4.0f * g->t * (1.0f - g->t);                       /* a smooth window */
            y = gr_read(g->pos) * w * w;
            ol += y * g->pl;
            or_ += y * g->pr;
            g->pos += g->inc;
            if (g->pos >= (float)RV_DELAY_SAMPLES)
                g->pos -= (float)RV_DELAY_SAMPLES;
            g->t += g->dt;
            if (g->t >= 1.0f)
                g->on = 0;
        }
        gr_lpl += (ol - gr_lpl) * tone;
        gr_lpr += (or_ - gr_lpr) * tone;
        gr_fbv = (gr_lpl + gr_lpr) * 0.5f;
        l[i] += gr_lpl * lvl;
        r[i] += gr_lpr * lvl;
    }
}

static void delay_run(const float *in, int n, float *l, float *r)
{
    static int8_t last_mode = -1;
    if (rv.gp[GP_DLYMODE] != last_mode) {              /* a new mode starts from a clean buffer */
        last_mode = (int8_t)rv.gp[GP_DLYMODE];
        __builtin_memset(dly_buf, 0, sizeof(dly_buf));
        __builtin_memset(grains, 0, sizeof(grains));
        dly_lp = dly_fbv = gr_lpl = gr_lpr = gr_fbv = 0.0f;
    }
    if (rv.gp[GP_DLYMODE] == 1) {
        grains_run(in, n, l, r);
        return;
    }
    {
    int i;
    float spb = 60.0f / (float)rv.gp[GP_TEMPO];
    /* TIME: free below the middle, then synced divisions above */
    int tv = rv.gp[GP_DLYTIME];
    float target;
    float fb = rv.gp[GP_DLYFB] * (0.95f / 127.0f), tone = 0.05f + rv.gp[GP_DLYTONE] * (0.9f / 127.0f);
    float lvl = rv.gp[GP_DLYLEVEL] * (1.0f / 127.0f);
    if (tv < 64)
        target = 0.01f + (float)tv * (0.25f / 64.0f);
    else {
        static const float div[8] = {0.125f, 0.1667f, 0.25f, 0.333f, 0.375f, 0.5f, 0.6667f, 0.75f};
        target = spb * div[clampi((tv - 64) / 8, 0, 7)];
    }
    target *= (float)RV_FS;
    if (target > (float)RV_DELAY_SAMPLES / 1.5f - 4.0f)
        target = (float)RV_DELAY_SAMPLES / 1.5f - 4.0f;
    if (dly_t <= 0.0f)
        dly_t = target;
    int frz = dly_freeze || rv.gp[GP_DLYFREEZE];
    float spread = 1.0f + rv.gp[GP_DLYSPREAD] * (0.5f / 127.0f), wob = rv.gp[GP_DLYWOB] * (1.0f / 127.0f);
    static float dly_wph;
    if (frz)
        fb = 1.0f;
    if (target * spread * 1.015f > (float)RV_DELAY_SAMPLES - 8.0f)
        target = ((float)RV_DELAY_SAMPLES - 8.0f) / (spread * 1.015f);
    for (i = 0; i < n; i++) {
        float rp, a, b, fr, tt;
        int32_t i0;
        dly_t += (target - dly_t) * 0.0005f;
        dly_wph += 0.55f * RV_DT;                      /* WOBBLE: a slow tape-like drift of the time */
        dly_wph -= fm_floorf(dly_wph);
        tt = dly_t * (1.0f + wob * 0.012f * sin_lut(dly_wph));
        rp = (float)dly_w - tt;
        if (rp < 0.0f)
            rp += (float)RV_DELAY_SAMPLES;
        i0 = (int32_t)rp;
        fr = rp - (float)i0;
        a = (dly_buf[i0] + (dly_buf[(i0 + 1) % RV_DELAY_SAMPLES] - dly_buf[i0]) * fr) * (1.0f / 16384.0f);
        rp = (float)dly_w - tt * spread;               /* SPREAD: the right tap, from same time to 1.5x */
        if (rp < 0.0f)
            rp += (float)RV_DELAY_SAMPLES;
        i0 = (int32_t)rp;
        b = dly_buf[i0] * (1.0f / 16384.0f);
        dly_lp += (b - dly_lp) * tone;
        dly_fbv = dly_lp;
        dly_buf[dly_w] = (int16_t)clampf(sat((frz ? 0.0f : in[i]) + dly_fbv * fb) * 16384.0f, -32000.0f, 32000.0f);
        dly_w = (dly_w + 1) % (int32_t)RV_DELAY_SAMPLES;
        l[i] += a * lvl;                              /* ping on the left, pong on the right */
        r[i] += dly_lp * lvl;
    }
}
}

/* -------------------------------------------------------------- space --- */
#define REV_N 4
/* one memory for both modes: ROOM uses the first 4096 samples, CLOUDS all of it */
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
    float dec = 0.55f + rv.gp[GP_REVSIZE] * (0.44f / 127.0f), damp = 0.1f + (127 - rv.gp[GP_REVDAMP]) * (0.85f / 127.0f);
    float dif = 0.45f + rv.gp[GP_REVDIFF] * (0.35f / 127.0f), lvl = rv.gp[GP_REVLEVEL] * (1.0f / 127.0f);
    float modd = 1.0f + rv.gp[GP_REVMOD] * (14.0f / 127.0f);
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

static void space_run(const float *in, int n, float *l, float *r)
{
    static int8_t last_mode = -1;
    if (rv.gp[GP_REVMODE] != last_mode) {             /* the modes share memory: start clean */
        last_mode = (int8_t)rv.gp[GP_REVMODE];
        __builtin_memset(space_mem, 0, sizeof(space_mem));
        cl_lp[0] = cl_lp[1] = cl_tail[0] = cl_tail[1] = cl_bw = cl_hp = 0.0f;
    }
    if (rv.gp[GP_REVMODE] == 1) {
        clouds_run(in, n, l, r);
        return;
    }
    {
    int i, k;
    float size = 0.45f + rv.gp[GP_REVSIZE] * (0.53f / 127.0f), damp = 0.15f + (127 - rv.gp[GP_REVDAMP]) * (0.8f / 127.0f);
    float g = 0.55f + size * 0.42f, lvl = rv.gp[GP_REVLEVEL] * (1.0f / 127.0f);
    float dif = 0.15f + rv.gp[GP_REVDIFF] * (0.6f / 127.0f), modd = rv.gp[GP_REVMOD] * (10.0f / 127.0f);
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
    float lvl = rv.gp[GP_BANDLEVEL] * (1.0f / 127.0f) * 1.6f, depth = rv.gp[GP_BANDDEPTH] * (1.0f / 127.0f);
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

/* ---------------------------------------------------------- microcuts --- */
static int16_t cut_buf[RV_CUT_SAMPLES] RV_BIG;
static int32_t cut_w, cut_start, cut_len, cut_pos, cut_left, cut_fade;
static uint8_t cut_rev;
static uint32_t cut_rng = 0xA5A5A5u;

static void cut_now(int action)
{
    float sz = 0.008f + (float)rv.gp[GP_CUTSIZE] * (0.08f / 127.0f);
    int32_t back;
    if (action <= 0)
        return;
    cut_len = clampi((int)(sz * (float)RV_FS), 64, (int)RV_CUT_SAMPLES / 2);
    back = action == 3 ? cut_len * (1 + (int32_t)(xs32(&cut_rng) % 3u)) : cut_len;
    cut_start = cut_w - back;
    while (cut_start < 0)
        cut_start += (int32_t)RV_CUT_SAMPLES;
    cut_rev = action == 2;
    cut_pos = 0;
    cut_left = cut_len * clampi(rv.gp[GP_CUTREP], 1, 16);
    cut_fade = clampi(cut_len / 8, 16, 400);
}

/* in place on the master: writes the ring, plays the cut over it */
static void cuts_run(float *l, float *r, int n)
{
    int i;
    float mix = rv.gp[GP_CUTMIX] * (1.0f / 127.0f);
    for (i = 0; i < n; i++) {
        cut_buf[cut_w] = (int16_t)clampf((l[i] + r[i]) * 8192.0f, -32000.0f, 32000.0f);
        cut_w = (cut_w + 1) % (int32_t)RV_CUT_SAMPLES;
        if (cut_left > 0) {
            int32_t p = cut_rev ? cut_len - 1 - cut_pos : cut_pos, idx = (cut_start + p) % (int32_t)RV_CUT_SAMPLES;
            float e = fm_minf(1.0f, fm_minf((float)(cut_pos + 1) / (float)cut_fade, (float)(cut_len - cut_pos) / (float)cut_fade));
            float cl = cut_buf[idx] * (1.0f / 16384.0f) * e, cr = cl;
            l[i] = lerpf(l[i], cl, mix * (e > 0.0f ? 1.0f : 0.0f));
            r[i] = lerpf(r[i], cr, mix * (e > 0.0f ? 1.0f : 0.0f));
            if (++cut_pos >= cut_len)
                cut_pos = 0;
            cut_left--;
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
    dly_lp = dly_fbv = 0.0f;
    __builtin_memset(space_mem, 0, sizeof(space_mem));
    __builtin_memset(grains, 0, sizeof(grains));
    cl_lp[0] = cl_lp[1] = cl_tail[0] = cl_tail[1] = cl_bw = cl_hp = 0.0f;
    gr_lpl = gr_lpr = gr_fbv = 0.0f;
    __builtin_memset(rev_lp, 0, sizeof(rev_lp));
    __builtin_memset(rev_ap, 0, sizeof(rev_ap));
    __builtin_memset(cut_buf, 0, sizeof(cut_buf));
    cut_left = 0;
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
