/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the strike voice. Percussion synthesis as exciter into body: click, noise, metallic
 * squares, an FM burst, dust and rain (seeded, with Order from scattered to clockwork), bow,
 * breath, or another track's audio, into a ping filter, a modal bank, a struck disc or two
 * coupled strings; a transient layer on top; ring modulation after the body.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

static const float bar_ratio[8] = {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.81f, 31.87f};
static const float disc_ratio[8] = {1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f};
static const float metal_hz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

static void strike_seed(strike_t *v, const float *eff)
{
    v->dust_rng = 0x9E3779B9u ^ ((uint32_t)(eff[TP_S15] + 0.5f) * 2654435761u);
    if (!v->dust_rng)
        v->dust_rng = 1;
    v->dust_acc = 0.0f;
    v->dust_next = 0.0f;
}

static void strike_note_on(strike_t *v, int note, float vel, const float *eff)
{
    int i;
    float per;
    v->note = (int8_t)note;
    v->vel = vel;
    v->t = 0.0f;
    v->exc_env = 1.0f;
    v->trans_env = 1.0f;
    v->pitch_env = 1.0f;
    v->on = 1;
    strike_seed(v, eff);
    for (i = 0; i < 6; i++)
        v->mph[i] = (float)i * 0.13f;
    v->fmph[0] = v->fmph[1] = 0.0f;
    v->bow_v = 0.0f;
    v->lim = 0.0f;
    if (clampi((int)(eff[TP_S1] + 0.5f), 0, BODY_N - 1) == BODY_STRINGS) {
        /* strings: a burst into both lines */
        per = 44100.0f / note_hz((float)note + eff[TP_PITCH]);
        for (i = 0; i < RV_KS_LEN; i++) {
            v->s1[i] = 0;
            v->s2[i] = 0;
        }
        v->w1 = v->w2 = 0;
        (void)per;
    }
}

/* the excitation for one sample */
static inline float strike_exciter(strike_t *v, int exc, const float *eff, float f, float ext)
{
    float x = 0.0f, tone = eff[TP_S3] * (1.0f / 127.0f), rate = eff[TP_S9] * (1.0f / 127.0f);
    float order = eff[TP_S8] * (1.0f / 127.0f);
    int k;
    if (v->spark)                                    /* SPARK: one bare tap, so only the body speaks */
        return (v->t < RV_DT * 1.5f ? 1.0f : 0.0f) * v->vel + ext;
    switch (exc) {
    case EXC_CLICK:
        x = v->t < RV_DT * 1.5f ? 1.0f : xs_bi(&v->rng) * v->exc_env * 0.3f;
        v->exc_env *= 0.9f - tone * 0.2f;
        break;
    case EXC_NOISE:
        x = xs_bi(&v->rng) * v->exc_env;
        v->exc_env *= 0.9990f - (1.0f - tone) * 0.004f;
        break;
    case EXC_METAL: {
        float s = 0.0f, sc = f / 400.0f;
        for (k = 0; k < 6; k++) {
            v->mph[k] += metal_hz[k] * sc * RV_DT * 2.0f;
            v->mph[k] -= fm_floorf(v->mph[k]);
            s += v->mph[k] < 0.5f ? 1.0f : -1.0f;
        }
        x = s * 0.17f * v->exc_env;
        v->exc_env *= 0.9992f - (1.0f - tone) * 0.002f;
        break;
    }
    case EXC_FM: {
        float idx = v->exc_env * (1.0f + tone * 6.0f);
        v->fmph[1] += f * 1.41f * RV_DT;
        v->fmph[1] -= fm_floorf(v->fmph[1]);
        v->fmph[0] += f * RV_DT;
        v->fmph[0] -= fm_floorf(v->fmph[0]);
        x = sin_lut(v->fmph[0] + idx * sin_lut(v->fmph[1])) * v->exc_env;
        v->exc_env *= 0.9985f;
        break;
    }
    case EXC_DUST:
    case EXC_RAIN: {
        /* impulses at RATE; ORDER walks from scattered (seeded, so it repeats) to clockwork */
        float hz = 2.0f + rate * rate * 400.0f;
        v->dust_acc += hz * RV_DT;
        if (v->dust_acc >= v->dust_next + 1.0f || v->t == 0.0f) {
            float jit = (1.0f - order) * (xs_uni(&v->dust_rng) * 1.8f - 0.9f);
            v->dust_acc -= v->dust_next + 1.0f;
            v->dust_next = jit;
            x = (exc == EXC_RAIN ? 0.3f + 0.7f * xs_uni(&v->dust_rng) : 1.0f);
            v->exc_env = 1.0f;
        }
        if (exc == EXC_RAIN) {                        /* a droplet: a tiny noise tail */
            x += xs_bi(&v->rng) * v->exc_env * 0.2f;
            v->exc_env *= 0.993f;
        }
        if (!v->gate && v->t > 0.05f)
            x *= 0.0f;
        break;
    }
    case EXC_BOW:
        if (v->gate)
            v->bow_v += (0.4f - v->bow_v) * 0.001f;
        else
            v->bow_v *= 0.999f;
        x = xs_bi(&v->rng) * v->bow_v * (0.1f + tone * 0.2f);
        break;
    case EXC_BLOW:
        if (v->gate)
            v->bow_v += (0.5f - v->bow_v) * 0.0005f;
        else
            v->bow_v *= 0.9995f;
        x = xs_bi(&v->rng) * v->bow_v * 0.25f;
        break;
    default:
        break;
    }
    return x * v->vel + ext;
}

static float strike_render(strike_t *v, track_t *tr, float *out, int n)
{
    const float *eff = tr->eff;
    int exc = clampi((int)(eff[TP_S0] + 0.5f), 0, EXC_N - 1), body = clampi((int)(eff[TP_S1] + 0.5f), 0, BODY_N - 1);
    float decay = eff[TP_S2] * (1.0f / 127.0f), tone = eff[TP_S3] * (1.0f / 127.0f), damp = eff[TP_S4] * (1.0f / 127.0f);
    float inh = eff[TP_S5] * (1.0f / 127.0f), geom = eff[TP_S6] * (1.0f / 127.0f);
    int nm = clampi((int)(eff[TP_S7] + 0.5f), 2, 8), i, k;
    float trans = eff[TP_S10] * (1.0f / 127.0f), tdec = eff[TP_S11] * (1.0f / 127.0f);
    float ringf = 0.25f * fm_exp2f(eff[TP_S12] * (5.0f / 127.0f)), ring = eff[TP_S13] * (1.0f / 127.0f);
    float pdrop = eff[TP_S14] * (1.0f / 127.0f);
    float f0 = note_hz((float)v->note + eff[TP_PITCH] + eff[TP_FINE] * 0.01f + v->hs.semi);
    float f = f0 * (1.0f + pdrop * 1.5f * v->pitch_env * v->pitch_env);
    float t60 = 0.02f + decay * decay * 6.0f, lvl = 0.0f;
    float tcoef = 1.0f - 1.0f / ((0.0005f + tdec * tdec * 0.08f) * 44100.0f);
    const float *ratios = body == BODY_DISC ? disc_ratio : bar_ratio;
    v->pitch_env *= fm_expf(-(float)n * RV_DT / 0.03f);
    if (f > 16000.0f)
        f = 16000.0f;
    /* body coefficients for this control step */
    if (body == BODY_MODAL || body == BODY_DISC) {
        for (k = 0; k < nm; k++) {
            float rr = body == BODY_MODAL ? lerpf((float)(k + 1), ratios[k], 0.3f + inh * 0.7f) : ratios[k] * (1.0f + geom * 0.3f * k * 0.1f);
            float fr = f * rr, td, r, pos;
            if (body == BODY_DISC && (k & 1))
                fr *= 1.0f + inh * 0.03f;               /* beating mode pairs */
            if (fr > 19000.0f) {
                v->md_g[k] = 0.0f;
                continue;
            }
            td = t60 / (1.0f + k * (0.3f + damp * 3.0f));
            r = fm_exp2f(-1.0f / (td * 44100.0f) * 9.97f);
            /* a decaying rotation (coupled form): unlike a two-pole recursion its energy cannot grow
             * when the frequency moves under it (pitch drop, modulation) */
            v->md_b1[k] = r * fm_cosf(6.2831853f * fr * RV_DT);
            v->md_b2[k] = r * fm_sinf(6.2831853f * fr * RV_DT);
            pos = body == BODY_MODAL ? fm_fabsf(sin_lut(0.5f * (k + 1) * (0.1f + geom * 0.4f))) : 1.0f;
            v->md_g[k] = pos * fm_powf(0.5f + tone * 0.5f, (float)k) * 0.12f;
        }
    } else if (body == BODY_PING) {
        svf_set(&v->ping, f, 2.0f + decay * decay * 300.0f);
    }
    for (i = 0; i < n; i++) {
        float x = strike_exciter(v, exc, eff, f, tr->exc[i] * 0.6f), y = 0.0f, a;
        float tr_layer = 0.0f;
        switch (body) {
        case BODY_PING: {
            float bp;
            svf_tick(&v->ping, x, &bp, 0);
            /* k: unity gain at the centre (the raw band peaks at its Q, up to 300; it used to be
             * boosted by Q again on top). A narrow band passes less of a broad strike: make up
             * in proportion to Q, and let the voice limiter below catch the peaks */
            y = bp * v->ping.k * (3.0f + (1.0f / v->ping.k) * 0.9f);
            break;
        }
        case BODY_STRINGS: {
            /* two Karplus-Strong lines leaking into each other */
            float per1 = 44100.0f / f, per2 = per1 / (1.0f + 0.003f + inh * 0.02f), g = fm_exp2f(-per1 / (t60 * 44100.0f) * 9.97f);
            float lpc = 0.2f + tone * 0.75f - damp * 0.15f;
            int d1 = clampi((int)per1, 2, RV_KS_LEN - 2), d2 = clampi((int)per2, 2, RV_KS_LEN - 2);
            int r1 = v->w1 - d1, r2 = v->w2 - d2;
            float y1, y2, c = geom * 0.3f;
            if (r1 < 0)
                r1 += RV_KS_LEN;
            if (r2 < 0)
                r2 += RV_KS_LEN;
            y1 = v->s1[r1] * (1.0f / 16000.0f);
            y2 = v->s2[r2] * (1.0f / 16000.0f);
            v->lp1 += (y1 - v->lp1) * lpc;
            v->lp2 += (y2 - v->lp2) * lpc;
            y1 = v->lp1 * g * (1.0f - c) + v->lp2 * g * c + x;
            y2 = v->lp2 * g * (1.0f - c) + v->lp1 * g * c + x * 0.7f;
            v->s1[v->w1] = (int16_t)clampf(y1 * 16000.0f, -32760.0f, 32760.0f);
            v->s2[v->w2] = (int16_t)clampf(y2 * 16000.0f, -32760.0f, 32760.0f);
            v->w1 = (v->w1 + 1) % RV_KS_LEN;
            v->w2 = (v->w2 + 1) % RV_KS_LEN;
            y = (y1 + y2) * 0.6f;
            break;
        }
        default:
            for (k = 0; k < nm; k++) {
                float re = v->md_b1[k] * v->md_y1[k] - v->md_b2[k] * v->md_y2[k] + v->md_g[k] * x;
                float im = v->md_b2[k] * v->md_y1[k] + v->md_b1[k] * v->md_y2[k];
                v->md_y1[k] = re;
                v->md_y2[k] = im;
                y += im;
            }
            y *= 0.6f;
            break;
        }
        /* a body driven steadily at its resonance (rain, bow, blow, metal, a long decay) can ring far
         * past full scale: a level limiter (instant attack, 80 ms release) holds the voice under
         * -3 dB without distorting it, and the soft curve only catches what slips through */
        {
            float ay = fm_fabsf(y), gl;
            v->lim = ay > v->lim ? ay : v->lim * 0.99972f;
            gl = v->lim > 0.7f ? 0.7f / v->lim : 1.0f;
            y = 2.0f * sat(y * gl * 0.5f);
        }
        if (ring > 0.001f) {
            v->ring_ph += f * ringf * RV_DT;
            v->ring_ph -= fm_floorf(v->ring_ph);
            y = lerpf(y, y * sin_lut(v->ring_ph) * 1.6f, ring);
        }
        if (trans > 0.001f && v->trans_env > 0.0005f && !v->spark) {   /* the attack layer: click and noise */
            tr_layer = (v->t < RV_DT * 1.5f ? 0.8f : 0.0f) + xs_bi(&v->rng) * v->trans_env * 0.5f;
            v->trans_env *= tcoef;
            y += tr_layer * trans * v->vel;
        }
        a = artic_tick(&v->a, &v->cfg);
        y *= a;
        out[i] += y * 2.0f;                           /* (level-matched with the synth engines) */
        v->last = y;
        v->t += RV_DT;
        lvl = a;
    }
    /* done once the articulation ended and the body has rung down */
    if (v->a.stage == AS_OFF || (v->t > t60 * 1.5f + 0.1f && !v->gate && (exc < EXC_DUST || exc > EXC_BLOW)))
        v->on = 0;
    if (v->a.stage == AS_OFF)
        v->on = 0;
    return lvl;
}
