/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: WEFT, a voice patched from blocks. Each voice runs the track's eight blocks once a sample,
 * in order: a block reads the outputs of those before it from this sample and of itself and those
 * after it from the last one, so any cord can go anywhere and feedback is a sample late (gen~'s
 * history, everywhere). Signals are about -1..1; SLOPE and SLEW give 0..1 (envelopes). The blocks'
 * settings are the track's S0-S15 after locks and modulation (eff). Four voices for every WEFT
 * track together; a track takes one or two (VOICES).
 * Part of the engine unit (ravel_app.c), before rv_engine.c (which calls it). */
#include "rv_int.h"

#define WV_N 4                                  /* voices, all WEFT tracks together */
#define WD_LEN 512                              /* a DELAY's memory, samples (11.6 ms: a note's period down to F2, 87 Hz; lower notes an octave up) */
#define WD_N 2                                  /* DELAY blocks a patch can use */

typedef struct {
    float ph, y, s1, s2, prev, prevb, hold, z[8];
    uint8_t st;
} wb_t;                                         /* a block's state in one voice */

typedef struct {
    uint8_t on, track, note, gate, trig;
    uint32_t age, rng;
    float vel, quiet, rel;                      /* how long it has been quiet, and since its release (s) */
    float sv[WS_N];                             /* what an input can read: 0 nothing, 1-8 the blocks' outputs, then the built-ins */
    wb_t b[WF_BLOCKS];
    uint16_t dw[WD_N];
} wv_t;

static wv_t wv[WV_N] RV_RAMBSS;            /* (in plain RAM: the pool is full) */
static int16_t wdly[WV_N][WD_N][WD_LEN] RV_BIG;
static uint32_t wv_age;
static struct { uint8_t on, idx, dec, wait; float last; int8_t buf[32]; } wsc[WF_BLOCKS];   /* the scope's capture (the newest voice of the track on screen) */

/* ------------------------------------------------------------- patches --- */
/* a block's defaults (its settings to the type's) */
static void weft_block_defaults(int t, int i)
{
    const wf_type_t *ty = &wf_types[rv.weft[t & 7].type[i & 7] % WO_N];
    rv.tp[t & 7][TP_S0 + 2 * (i & 7)] = ty->d1;
    rv.tp[t & 7][TP_S0 + 2 * (i & 7) + 1] = ty->d2;
}

/* a factory patch onto track t */
static void weft_preset_apply(int t, int k)
{
    const wf_preset_t *p = &wf_presets[(k < 0 ? 0 : k) % WF_NPRESETS];
    rv_weft *w = &rv.weft[t & 7];
    int i;
    __builtin_memset(w, 0, sizeof *w);
    for (i = 0; i < WF_BLOCKS; i++) {
        w->type[i] = p->blk[i].t;
        w->a[i] = p->blk[i].a;
        w->amta[i] = p->blk[i].aa;
        w->b[i] = p->blk[i].b;
        w->amtb[i] = p->blk[i].ab;
        rv.tp[t & 7][TP_S0 + 2 * i] = p->blk[i].p1;
        rv.tp[t & 7][TP_S0 + 2 * i + 1] = p->blk[i].p2;
    }
    w->out = p->out;
    for (i = 0; i < 4; i++)
        w->map[i] = p->map[i];
    rv.tp[t & 7][TP_POLY] = (int16_t)(p->poly > 1);
}
void rv_weft_preset(int t, int k) { weft_preset_apply(t & 7, k); }
void rv_weft_block_defaults(int t, int i) { weft_block_defaults(t & 7, i & 7); }
int rv_weft_presets(void) { return WF_NPRESETS; }
const char *rv_weft_preset_name(int k) { return wf_presets[(k < 0 ? 0 : k) % WF_NPRESETS].name; }

/* ------------------------------------------------------------- voices --- */
static void weft_note_on(int t, int note, int vel)
{
    int i, pick = -1, n = 0, poly = rv.tp[t][TP_POLY] > 0 ? 2 : 1;
    uint32_t old = 0xFFFFFFFFu;
    wv_t *v;
    for (i = 0; i < WV_N; i++)                  /* its own: at most VOICES of them */
        if (wv[i].on && wv[i].track == t) {
            n++;
            if (wv[i].age < old) {
                old = wv[i].age;
                pick = i;
            }
        }
    if (n < poly) {
        int lim = rv_live.cap != 255 && !rv_live.guard_off ? 2 : WV_N;   /* (the overload guard holding voices back: two at most) */
        pick = -1;
        for (i = 0; i < lim && pick < 0; i++)
            if (!wv[i].on)
                pick = i;
        if (pick < 0) {                         /* a released one, else the oldest */
            old = 0xFFFFFFFFu;
            for (i = 0; i < WV_N; i++)
                if (!wv[i].gate && wv[i].age < old)
                    old = wv[i].age, pick = i;
            if (pick < 0)
                for (i = 0; i < WV_N; i++)
                    if (wv[i].age < old)
                        old = wv[i].age, pick = i;
        }
    }
    v = &wv[pick < 0 ? 0 : pick];
    if (!(v->on && v->track == t && poly == 1)) {   /* a fresh voice: its blocks start clean (a mono track's
                                                    * carries on, so its oscillators and delays don't click) */
        __builtin_memset(v, 0, sizeof *v);
        __builtin_memset(wdly[v - wv], 0, sizeof wdly[0]);
        v->rng = 0x9E3779B9u ^ (uint32_t)(note * 7919 + t * 104729 + (int)wv_age);
    }
    v->on = 1;
    v->track = (uint8_t)t;
    v->note = (uint8_t)note;
    v->gate = 1;
    v->trig = 1;
    v->vel = (float)vel * (1.0f / 127.0f);
    v->rel = v->quiet = 0.0f;
    v->age = ++wv_age;
}

static void weft_note_off(int t, int note)
{
    int i;
    for (i = 0; i < WV_N; i++)
        if (wv[i].on && wv[i].track == t && wv[i].note == note)
            wv[i].gate = 0;
}

static void weft_all_off(void)
{
    int i;
    for (i = 0; i < WV_N; i++)
        wv[i].gate = 0;
}

/* ------------------------------------------------------------- render --- */
/* a setting, 0..1 */
RV_INL float ws(const float *eff, int i, int s) { return clampf(eff[TP_S0 + 2 * i + s], 0.0f, 127.0f) * (1.0f / 127.0f); }
/* a setting about its middle: 64 is exactly 0, -1 .. +0.98 (offsets, thresholds, bias: 64 adds nothing) */
RV_INL float wc(const float *eff, int i, int s) { return (clampf(eff[TP_S0 + 2 * i + s], 0.0f, 127.0f) - 64.0f) * (1.0f / 64.0f); }
/* a time: 0.5 ms .. about 8 s */
RV_INL float wtime(float x) { return 0.0005f * fm_exp2f(x * 14.0f); }
/* a one-pole step toward a target over time T (s) */
RV_INL float wk(float T) { return 1.0f - fm_expf(-RV_DT / (T > 1e-5f ? T : 1e-5f)); }

/* SIN, TRI, SAW and SQUARE in turn, SHAPE blending each into the next */
RV_INL float wf_shape(int k, float p)
{
    return k == 0 ? fm_sin_turns(p) : k == 1 ? (p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f)
         : k == 2 ? 2.0f * p - 1.0f : (p < 0.5f ? 1.0f : -1.0f);
}
static float wf_wave(float p, float shape)
{
    float s3 = shape * 3.0f, f, a;
    int seg = (int)s3;
    p -= fm_floorf(p);
    if (seg > 2)
        seg = 2;
    f = s3 - (float)seg;
    a = wf_shape(seg, p);
    return f > 0.0f ? a + (wf_shape(seg + 1, p) - a) * f : a;
}

RV_INL int wrise(float x, float *prev, float thr)   /* a rising edge through thr */
{
    int r = x > thr && *prev <= thr;
    *prev = x;
    return r;
}

static void weft_voice(wv_t *v, int t, const float *eff, float hz, float *out, int n, int scope)
{
    const rv_weft *w = &rv.weft[t];
    float lfo1 = lfo[0].value, lfo2 = lfo[1].value, wheel = midi_wheel, pitch = ((float)v->note + eff[TP_PITCH] - 60.0f) * (1.0f / 48.0f);
    float kr[WF_BLOCKS], kf[WF_BLOCKS], c1[WF_BLOCKS], c2[WF_BLOCKS], ka[WF_BLOCKS], kb[WF_BLOCKS], fg[WF_BLOCKS], bg[WF_BLOCKS][4];
    float *sv = v->sv;
    uint8_t sa[WF_BLOCKS], sb[WF_BLOCKS];
    int i, j, outb = w->out ? (w->out - 1) & 7 : -1, dn[WF_BLOCKS], nd = 0;
    float peak = 0.0f;
    /* what each block needs per sample, worked out once per step */
    sv[0] = 0.0f;
    sv[WS_PITCH] = pitch;
    sv[WS_VEL] = v->vel;
    sv[WS_LFO1] = lfo1;
    sv[WS_LFO2] = lfo2;
    sv[WS_WHEEL] = wheel;
    sv[WS_ONE] = 1.0f;
    for (i = 0; i < WF_BLOCKS; i++) {
        float x1 = ws(eff, i, 0), x2 = ws(eff, i, 1);
        dn[i] = -1;
        sa[i] = (uint8_t)(w->a[i] < WS_N ? w->a[i] : 0);
        sb[i] = (uint8_t)(w->b[i] < WS_N ? w->b[i] : 0);
        ka[i] = (float)w->amta[i] * 0.01f;
        kb[i] = (float)w->amtb[i] * 0.01f;
        switch (w->type[i]) {
        case WO_SLOPE: case WO_SLEW: kr[i] = wk(wtime(x1)); kf[i] = wk(wtime(x2)); break;
        case WO_PHASE: c1[i] = hz * wf_ratio_v[clampi((int)(eff[TP_S0 + 2 * i] + 0.5f), 0, 15)] * fm_exp2f(wc(eff, i, 1) * (1.0f / 12.0f)) * RV_DT; break;
        case WO_CYCLE: c1[i] = 0.02f * fm_exp2f(x1 * 17.0f) * RV_DT; c2[i] = clampf(x2, 0.01f, 0.99f); break;
        case WO_LP: case WO_BP: case WO_HP:
            c1[i] = 20.0f * fm_exp2f(x1 * 10.0f);
            c2[i] = 2.0f - 1.96f * x2;
            fg[i] = fm_tanf(3.14159265f * clampf(c1[i], 20.0f, 18000.0f) * RV_DT);
            break;
        case WO_BANDS: {
            int k;
            c1[i] = fm_exp2f(wc(eff, i, 0) * 1.0f);
            c2[i] = 0.5f - 0.48f * x2;
            for (k = 0; k < 4; k++)                     /* (the bands sit still through a step) */
                bg[i][k] = fm_tanf(3.14159265f * clampf(hz * fm_powf((float)(k + 1), c1[i]), 20.0f, 16000.0f) * RV_DT);
            break;
        }
        case WO_DELAY:
            if (nd < WD_N)
                dn[i] = nd++;
            c1[i] = x1 < 0.004f ? (float)RV_FS / (hz > 20.0f ? hz : 20.0f) - 2.0f : 0.0001f * fm_exp2f(x1 * 6.8f) * (float)RV_FS;
            while (c1[i] > (float)(WD_LEN - 2))
                c1[i] *= 0.5f;                          /* (a period too long for the memory: an octave up) */
            c2[i] = wc(eff, i, 1) * 1.0f;
            break;
        default: break;
        }
    }
    for (j = 0; j < n; j++) {
        float noise = xs_uni(&v->rng) * 2.0f - 1.0f, trig = v->trig ? 1.0f : 0.0f, gate = v->gate ? 1.0f : 0.0f;
        v->trig = 0;
        sv[WS_GATE] = gate;
        sv[WS_TRIG] = trig;
        sv[WS_NOISE] = noise;
        for (i = 0; i < WF_BLOCKS; i++) {
            wb_t *s = &v->b[i];
            int ty = w->type[i], ac = sa[i] != 0, bc = sb[i] != 0;
            float a = 0.0f, b = 0.0f, y = 0.0f, x1, x2, gn;
            if (!ty)
                continue;
            a = sv[sa[i]] * ka[i];
            b = sv[sb[i]] * kb[i];
            x1 = ws(eff, i, 0);
            x2 = ws(eff, i, 1);
            gn = clampf(eff[TP_S0 + 2 * i + 1], 0.0f, 127.0f) * (1.0f / 64.0f);   /* (GAIN: 64 is 1) */
            switch (ty) {
            case WO_PHASE:                      /* a ramp at the note x RATIO; A: FM (through zero), B: reset */
                s->ph += c1[i] * (1.0f + 8.0f * a);
                s->ph -= fm_floorf(s->ph);
                if (bc && wrise(b, &s->prevb, 0.0f))
                    s->ph = 0.0f;
                y = s->ph * 2.0f - 1.0f;
                break;
            case WO_WAVE: {                     /* reads a phase (A, else its own at the note); B: phase modulation */
                float p, k = clampf(0.5f + wc(eff, i, 1) * 0.5f, 0.03f, 0.97f);
                if (ac)
                    p = a * 0.5f + 0.5f;
                else {
                    s->ph += hz * RV_DT;
                    s->ph -= fm_floorf(s->ph);
                    p = s->ph;
                }
                p += b * 0.5f;
                p -= fm_floorf(p);
                p = p < k ? p / k * 0.5f : 0.5f + (p - k) / (1.0f - k) * 0.5f;   /* WARP: the CZ's bent phase */
                y = wf_wave(p, x1);
                break;
            }
            case WO_SLOPE: {                    /* rises while A (else the note) is up, holds, falls; B: faster or slower */
                float g = ac ? a : gate, sc = bc ? fm_exp2f(-b * 4.0f) : 1.0f;
                if (wrise(g, &s->prev, 0.5f) || (!ac && trig > 0.0f))
                    s->st = 1;
                if (s->st == 1) {
                    s->y += (1.25f - s->y) * clampf(kr[i] * sc, 0.0f, 1.0f);
                    if (s->y >= 1.0f) {
                        s->y = 1.0f;
                        s->st = 2;
                    }
                } else if (s->st == 2 && g <= 0.5f)
                    s->st = 0;
                if (!s->st)
                    s->y -= s->y * clampf(kf[i] * sc, 0.0f, 1.0f);
                y = s->y;
                break;
            }
            case WO_CYCLE: {                    /* rises and falls on its own (SKEW: which takes longer); A: rate, B: reset */
                float inc = c1[i] * (ac ? fm_exp2f(a * 5.0f) : 1.0f), k = c2[i];
                s->ph += inc;
                s->ph -= fm_floorf(s->ph);
                if (bc && wrise(b, &s->prevb, 0.0f))
                    s->ph = 0.0f;
                y = (s->ph < k ? s->ph / k : (1.0f - s->ph) / (1.0f - k)) * 2.0f - 1.0f;
                break;
            }
            case WO_SLEW: {                     /* follows A, taking RISE to go up and FALL to come down */
                float sc = bc ? fm_exp2f(-b * 4.0f) : 1.0f;
                s->y += (a - s->y) * clampf((a > s->y ? kr[i] : kf[i]) * sc, 0.0f, 1.0f);
                y = s->y;
                break;
            }
            case WO_LP: case WO_BP: case WO_HP: {   /* a state-variable filter; B: the cutoff, five octaves each way */
                float g, R = c2[i] * 0.5f;
                if (bc && !(j & 3))             /* (cutoff moved by B: worked out every fourth sample) */
                    s->hold = fm_tanf(3.14159265f * clampf(c1[i] * fm_exp2f(b * 5.0f), 20.0f, 18000.0f) * RV_DT);
                g = bc ? (s->hold > 0.0f ? s->hold : fg[i]) : fg[i];
                float hp = (a - (2.0f * R + g) * s->s1 - s->s2) / (1.0f + 2.0f * R * g + g * g);
                float bp = g * hp + s->s1, lp;
                s->s1 = g * hp + bp;
                lp = g * bp + s->s2;
                s->s2 = g * bp + lp;
                y = ty == WO_LP ? lp : ty == WO_BP ? bp * (2.0f * R) : hp;
                if (!(y > -8.0f && y < 8.0f))   /* (blown up: start over) */
                    s->s1 = s->s2 = y = 0.0f;
                break;
            }
            case WO_ADD: y = (a + b + wc(eff, i, 0) * 2.0f) * gn; break;
            case WO_MULT: y = a * ((bc ? b : 1.0f) + wc(eff, i, 0) * 2.0f) * gn; break;
            case WO_MIX: y = (a + (b - a) * x1) * gn; break;
            case WO_MIN: y = fm_minf(a, b + wc(eff, i, 0) * 2.0f) * gn; break;
            case WO_MAX: y = fm_maxf(a, b + wc(eff, i, 0) * 2.0f) * gn; break;
            case WO_FOLD:                       /* driven round a sine: folds back as it grows */
                y = fm_sin_turns((a * (1.0f + x1 * 15.0f) * (1.0f + (bc ? b : 0.0f)) + wc(eff, i, 1)) * 0.25f);
                break;
            case WO_COMP: case WO_AND: case WO_OR: case WO_XOR: case WO_FLIP: {
                float thr = wc(eff, i, 1);
                int ta = a > thr, tb = b > thr, r;
                if (ty == WO_COMP)
                    r = a > b + thr;
                else if (ty == WO_AND)
                    r = ta && tb;
                else if (ty == WO_OR)
                    r = ta || tb;
                else if (ty == WO_XOR)
                    r = ta != tb;
                else {                          /* FLIP: over on each rise of A (half its rate), B sets it back */
                    if (wrise(a, &s->prev, thr))
                        s->st ^= 1;
                    if (bc && wrise(b, &s->prevb, thr))
                        s->st = 0;
                    r = s->st;
                }
                y = (r ? 1.0f : -1.0f) * x1;
                break;
            }
            case WO_DELAY: {                    /* up to 11 ms; TIME at 0: the note's period. B: the time, two octaves each way */
                int16_t *m;
                float d, fr, r0, r1, wr;
                int k, i0;
                if (dn[i] < 0) {
                    y = 0.0f;
                    break;
                }
                m = wdly[v - wv][dn[i]];
                d = clampf(c1[i] * (bc ? fm_exp2f(b * 2.0f) : 1.0f), 2.0f, (float)(WD_LEN - 2));
                k = v->dw[dn[i]];
                fr = (float)k - d;
                if (fr < 0.0f)
                    fr += (float)WD_LEN;
                i0 = (int)fr;
                fr -= (float)i0;
                r0 = (float)m[i0];
                r1 = (float)m[(i0 + 1) % WD_LEN];
                y = (r0 + (r1 - r0) * fr) * (1.0f / 16384.0f);
                wr = sat(a + y * c2[i]);
                m[k] = (int16_t)(wr * 16384.0f);
                v->dw[dn[i]] = (uint16_t)((k + 1) % WD_LEN);
                break;
            }
            case WO_BANDS: {                    /* four bands at the note's 1st-4th harmonics (SPREAD bends them); B: scan */
                int k;
                float sum = 0.0f, pos = (b * 0.5f + 0.5f) * 3.0f;
                for (k = 0; k < 4; k++) {
                    float g = bg[i][k], R = c2[i], hp, bp, wgt;
                    hp = (a - (2.0f * R + g) * s->z[2 * k] - s->z[2 * k + 1]) / (1.0f + 2.0f * R * g + g * g);
                    bp = g * hp + s->z[2 * k];
                    s->z[2 * k] = g * hp + bp;
                    s->z[2 * k + 1] = g * bp + (g * bp + s->z[2 * k + 1]);
                    wgt = bc ? fm_maxf(0.0f, 1.0f - fm_fabsf((float)k - pos)) : 0.5f;
                    sum += bp * 2.0f * R * wgt;
                }
                y = sum * 3.0f;                 /* (narrow bands pass little: brought up) */
                if (!(y > -8.0f && y < 8.0f)) {
                    __builtin_memset(s->z, 0, sizeof s->z);
                    y = 0.0f;
                }
                break;
            }
            case WO_HOLD: {                     /* takes A on each rise of B (else each note); STEPS: rounds it; GLIDE */
                if (bc ? wrise(b, &s->prevb, 0.0f) : trig > 0.0f) {
                    float q = a;
                    if (x1 > 0.0f) {
                        float lv = 2.0f + x1 * 30.0f;
                        q = fm_floorf((q * 0.5f + 0.5f) * lv + 0.5f) / lv * 2.0f - 1.0f;
                    }
                    s->hold = q;
                }
                s->y += (s->hold - s->y) * (x2 > 0.0f ? wk(wtime(x2 * 0.8f)) : 1.0f);
                y = s->y;
                break;
            }
            default: break;
            }
            sv[i + 1] = y;
            if (scope && wsc[i].on) {           /* the scope: a moment of each block, started on a rising crossing */
                if (wsc[i].wait) {
                    if ((y >= 0.0f && wsc[i].last < 0.0f) || ++wsc[i].wait > 200) {
                        wsc[i].wait = 0;
                        wsc[i].idx = 0;
                        wsc[i].dec = 99;
                    }
                    wsc[i].last = y;
                }
                if (!wsc[i].wait && ++wsc[i].dec >= (ty == WO_SLOPE || ty == WO_SLEW || ty == WO_HOLD || (ty == WO_CYCLE && c1[i] < 0.0005f) ? 96 : 3)) {
                    wsc[i].dec = 0;
                    wsc[i].buf[wsc[i].idx++] = (int8_t)clampf(y * 100.0f, -127.0f, 127.0f);
                    if (wsc[i].idx >= 32) {
                        __builtin_memcpy((void *)rv_live.wscope[i], wsc[i].buf, 32);
                        wsc[i].wait = 1;
                        wsc[i].last = y;
                    }
                }
            }
        }
        {
            float x = outb >= 0 ? v->sv[outb + 1] : 0.0f;
            x = sat(x * 0.6f) * 0.7f;           /* (a patch can't run away with the level) */
            out[j] += x;
            if (fm_fabsf(x) > peak)
                peak = fm_fabsf(x);
        }
    }
    /* a released voice ends when it has been quiet a moment (or after a long while) */
    if (!v->gate) {
        v->rel += (float)n * RV_DT;
        v->quiet = peak < 0.0005f ? v->quiet + (float)n * RV_DT : 0.0f;
        if (v->quiet > 0.25f || v->rel > 12.0f || outb < 0)
            v->on = 0;
    }
}

/* the WEFT track t's voices this step, into buf */
static void weft_render(int t, float *buf, int n)
{
    const float *eff = trk[t].eff;
    int i, newest = -1, k;
    uint32_t age = 0;
    float hzb;
    if (t == rv_live.ui_track)
        for (i = 0; i < WV_N; i++)
            if (wv[i].on && wv[i].track == t && wv[i].age > age)
                age = wv[i].age, newest = i;
    for (k = 0; k < WF_BLOCKS; k++)
        wsc[k].on = newest >= 0 && rv.weft[t].type[k] != WO_NONE;
    for (i = 0; i < WV_N; i++) {
        if (!wv[i].on || wv[i].track != t)
            continue;
        hzb = note_hz((float)wv[i].note + eff[TP_PITCH] + eff[TP_FINE] * 0.01f);
        if (hzb > 12000.0f)
            hzb = 12000.0f;
        weft_voice(&wv[i], t, eff, hzb, buf, n, i == newest);
    }
}

/* voices of tracks that are no longer WEFT go */
static void weft_tidy(void)
{
    int i, c = 0;
    for (i = 0; i < WV_N; i++) {
        if (wv[i].on && rv.tp[wv[i].track & 7][TP_KIND] != KIND_WEFT)
            wv[i].on = 0;
        c += wv[i].on;
    }
    rv_live.wvoices = (uint8_t)c;
}
