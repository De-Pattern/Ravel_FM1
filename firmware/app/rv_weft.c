/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: WEFT, a voice patched from blocks. Each voice runs the track's twelve blocks once a sample,
 * in order: a block reads the outputs of those before it from this sample and of itself and those
 * after it from the last one, so any cord can go anywhere and feedback is a sample late (gen~'s
 * history, everywhere). Signals are about -1..1; SLOPE and SLEW give 0..1 (envelopes). The blocks'
 * settings are track parameters (wf_sp) after locks and modulation (eff). Four voices for every WEFT
 * track together; a track takes one or two (VOICES). A patch with an output block (CLK OUT, MOD OUT)
 * keeps a voice running between notes, so the clock and the source go on.
 * Part of the engine unit (ravel_app.c), before rv_engine.c (which calls it). */
#include "rv_int.h"

#define WV_N 4                                  /* voices, all WEFT tracks together */
#define WD_LEN 512                              /* a DELAY's memory, samples (11.6 ms: a note's period down to F2, 87 Hz; lower notes an octave up) */
#define WD_N 2                                  /* DELAY and DIFFUSE blocks a patch can use, together */
#define WM_N 3                                  /* six-mode resonators (SKIN ... FIXED) a patch can use */
#define WM_K 6                                  /* modes in one */

typedef struct {
    float ph, y, s1, s2, prev, prevb, hold, z[8];
    uint8_t st;
} wb_t;                                         /* a block's state in one voice */

typedef struct {
    uint8_t on, track, note, gate, trig, keep;
    uint32_t age, rng;
    float vel, quiet, rel;                      /* how long it has been quiet, and since its release (s) */
    float sv[WS_N];                             /* what an input can read: 0 nothing, 1-12 the blocks' outputs, then the built-ins */
    wb_t b[WF_BLOCKS];
    float mz[WM_N][2 * WM_K];                   /* the six-mode resonators' state */
    uint16_t dw[WD_N];
} wv_t;

static wv_t wv[WV_N] RV_RAMBSS;            /* (in plain RAM: the pool is full) */
static int16_t wdly[WV_N][WD_N][WD_LEN] RV_BIG;
static uint32_t wv_age;
static uint8_t wf_has_out[RV_TRACKS], wf_last_note[RV_TRACKS] = {60, 60, 60, 60, 60, 60, 60, 60};
static int8_t wf_clk_owner = -1, wf_mod_owner = -1;
static struct { uint8_t on, idx, dec, wait; float last, pk; int8_t buf[32]; } wsc[WF_BLOCKS];   /* the scope's capture (the newest voice of the track on screen) */

/* what a voice's blocks need, worked out once a step (and again for a block whose B moves a setting) */
static struct {
    float p1[WF_BLOCKS], p2[WF_BLOCKS];         /* the settings, 0..1 */
    float c1[WF_BLOCKS], c2[WF_BLOCKS], kr[WF_BLOCKS], kf[WF_BLOCKS], fg[WF_BLOCKS];
    float bg[WF_BLOCKS][4];                     /* BANDS */
    float md[WM_N][WM_K][3];                    /* the six-mode resonators: per mode a1, a2, gain */
    int8_t dn[WF_BLOCKS], mn[WF_BLOCKS];        /* a block's delay memory, its six-mode slot (-1 none) */
    float hz;
} wcx;

/* the bodies' modes: frequency ratios (FIXED: Hz) */
static const float wf_modes[6][WM_K] = {
    {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f},       /* SKIN: a circular membrane */
    {1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f},     /* BAR: free at both ends */
    {1.0f, 1.84f, 2.71f, 3.32f, 4.19f, 5.13f},            /* PLATE */
    {0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f},           /* BELL: hum, prime, tierce, quint, nominal, ... */
    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},                 /* HARMS */
    {150.0f, 310.0f, 640.0f, 1300.0f, 2700.0f, 5500.0f},  /* FIXED: a filterbank */
};

/* ------------------------------------------------------------- patches --- */
/* a block's defaults (its settings to the type's) */
static void weft_block_defaults(int t, int i)
{
    const wf_type_t *ty = &wf_types[rv.weft[t & 7].type[i % WF_BLOCKS] % WO_N];
    rv.tp[t & 7][wf_sp(2 * (i % WF_BLOCKS))] = ty->d1;
    rv.tp[t & 7][wf_sp(2 * (i % WF_BLOCKS) + 1)] = ty->d2;
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
        rv.tp[t & 7][wf_sp(2 * i)] = p->blk[i].p1;
        rv.tp[t & 7][wf_sp(2 * i + 1)] = p->blk[i].p2;
    }
    w->out = p->out;
    w->bmove = p->bmove;
    for (i = 0; i < 4; i++)
        w->map[i] = p->map[i];
    rv.tp[t & 7][TP_POLY] = (int16_t)(p->poly > 1);
}
void rv_weft_preset(int t, int k) { weft_preset_apply(t & 7, k); }
void rv_weft_block_defaults(int t, int i) { weft_block_defaults(t & 7, i % WF_BLOCKS); }
int rv_weft_presets(void) { return WF_NPRESETS; }
const char *rv_weft_preset_name(int k) { return wf_presets[(k < 0 ? 0 : k) % WF_NPRESETS].name; }

/* format 24's patch (eight blocks, 48 bytes) into today's: blocks 9-12 empty, the built-in sources renumbered */
static int wf_old_src(int s) { return s >= WS_OLD_PITCH ? s + (WS_PITCH - WS_OLD_PITCH) : s; }
void rv_weft_from24(rv_weft *w, const uint8_t *o)
{
    int i;
    __builtin_memset(w, 0, sizeof *w);
    for (i = 0; i < 8; i++) {
        w->type[i] = o[i] < WO_DUST ? o[i] : 0;
        w->a[i] = (uint8_t)wf_old_src(o[8 + i]);
        w->b[i] = (uint8_t)wf_old_src(o[16 + i]);
        w->amta[i] = (int8_t)o[24 + i];
        w->amtb[i] = (int8_t)o[32 + i];
    }
    w->out = o[40] <= 8 ? o[40] : 0;
    for (i = 0; i < 4; i++)
        w->map[i] = o[41 + i] <= 16 ? o[41 + i] : 0;
}

/* ------------------------------------------------------------- voices --- */
/* a patch's blocks in use: more than eight play one voice (the FM-1's time is not measured for more yet) */
static int weft_used(int t)
{
    int i, n = 0;
    for (i = 0; i < WF_BLOCKS; i++)
        n += rv.weft[t & 7].type[i] != WO_NONE;
    return n;
}
int rv_weft_poly(int t) { return rv.tp[t & 7][TP_POLY] > 0 && weft_used(t) <= 8 ? 2 : 1; }

static int weft_voice_start(int t, int note, int vel, int keep)
{
    int i, pick = -1, n = 0, poly = rv_weft_poly(t);
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
            if (keep)
                return -1;                      /* (a voice kept for an output never takes a sounding one) */
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
    v->age = ++wv_age;
    v->rel = v->quiet = 0.0f;
    if (!keep) {
        v->gate = 1;
        v->trig = 1;
        v->vel = (float)vel * (1.0f / 127.0f);
        wf_last_note[t] = (uint8_t)note;
    }
    return (int)(v - wv);
}
static void weft_note_on(int t, int note, int vel) { weft_voice_start(t, note, vel, 0); }

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
/* a setting 0..1 as it was turned; about its middle (64 is exactly 0, -1 .. +0.98: offsets, thresholds,
 * bias); as a GAIN (64 is 1); as a list's entry */
#define WC(x) ((x) * (127.0f / 64.0f) - 1.0f)
#define WG(x) ((x) * (127.0f / 64.0f))
#define WN(x, n) clampi((int)((x) * 127.0f + 0.5f), 0, (n) - 1)
/* a time: 0.5 ms .. about 8 s */
RV_INL float wtime(float x) { return 0.0005f * fm_exp2f(x * 14.0f); }
/* a one-pole step toward a target over time T (s) */
RV_INL float wk(float T) { return 1.0f - fm_expf(-RV_DT / (T > 1e-5f ? T : 1e-5f)); }
/* a ring's decay (T60): 2 ms .. 8 s, as its pole's radius */
RV_INL float wdecay(float x) { return fm_expf(-6.9078f * RV_DT / (0.002f * fm_exp2f(x * 12.0f))); }

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

/* a resonator's pole pair at f Hz with radius r: a1, a2 and the input's gain (sin w keeps a struck ring near 1) */
static void wf_pole(float *c, float f, float r, float g)
{
    float x = f * RV_DT;
    if (x > 0.45f || x <= 0.0f) {               /* (above the band: silent) */
        c[0] = c[1] = c[2] = 0.0f;
        return;
    }
    c[0] = 2.0f * r * fm_sin_turns(x + 0.25f);
    c[1] = -r * r;
    c[2] = fm_sin_turns(x) * g;
}

/* block i's per-step values from its settings x1, x2 (0..1); pm: B's pitch (octaves, for the resonators) */
static void wf_setup(int i, int ty, float x1, float x2, float pm)
{
    float hz = wcx.hz;
    wcx.p1[i] = x1;
    wcx.p2[i] = x2;
    switch (ty) {
    case WO_SLOPE: case WO_SLEW: wcx.kr[i] = wk(wtime(x1)); wcx.kf[i] = wk(wtime(x2)); break;
    case WO_PHASE: wcx.c1[i] = hz * wf_ratio_v[WN(x1, 16)] * fm_exp2f(WC(x2) * (1.0f / 12.0f)) * RV_DT; break;
    case WO_CYCLE: wcx.c1[i] = 0.02f * fm_exp2f(x1 * 17.0f) * RV_DT; wcx.c2[i] = clampf(x2, 0.01f, 0.99f); break;
    case WO_LP: case WO_BP: case WO_HP:
        wcx.c1[i] = 20.0f * fm_exp2f(x1 * 10.0f);
        wcx.c2[i] = 2.0f - 1.96f * x2;
        wcx.fg[i] = fm_tanf(3.14159265f * clampf(wcx.c1[i], 20.0f, 18000.0f) * RV_DT);
        break;
    case WO_BANDS: {
        int k;
        wcx.c1[i] = fm_exp2f(WC(x1));
        wcx.c2[i] = 0.5f - 0.48f * x2;
        for (k = 0; k < 4; k++)                 /* (the bands sit still through a step) */
            wcx.bg[i][k] = fm_tanf(3.14159265f * clampf(hz * fm_powf((float)(k + 1), wcx.c1[i]), 20.0f, 16000.0f) * RV_DT);
        break;
    }
    case WO_DELAY:
        wcx.c1[i] = x1 < 0.004f ? (float)RV_FS / (hz > 20.0f ? hz : 20.0f) - 2.0f : 0.0001f * fm_exp2f(x1 * 6.8f) * (float)RV_FS;
        while (wcx.c1[i] > (float)(WD_LEN - 2))
            wcx.c1[i] *= 0.5f;                  /* (a period too long for the memory: an octave up) */
        wcx.c2[i] = WC(x2);
        break;
    case WO_DUST: wcx.c1[i] = 0.5f * fm_exp2f(x1 * 12.0f); break;   /* particles a second: 0.5 .. 2000 */
    case WO_TICK: wcx.c1[i] = wf_div_v[WN(x1, 12)]; break;
    case WO_RING: {
        float c[3];
        wf_pole(c, hz * wf_ratio_v[WN(x1, 16)] * fm_exp2f(pm), wdecay(x2), 1.0f);
        wcx.kr[i] = c[0], wcx.kf[i] = c[1], wcx.fg[i] = c[2];   /* (a1, a2, the input's gain) */
        break;
    }
    case WO_SKIN: case WO_BAR: case WO_PLATE: case WO_BELL: case WO_HARMS: case WO_FIXED:
        if (wcx.mn[i] >= 0) {
            const float *r = wf_modes[ty - WO_SKIN];
            float base = ty == WO_FIXED ? fm_exp2f(pm) : hz * fm_exp2f(pm), dark = 1.0f - x2;
            int k;
            for (k = 0; k < WM_K; k++) {        /* BRIGHT: dark, the high modes softer and shorter */
                float f = base * r[k], rel = r[k] / r[ty == WO_BELL ? 1 : 0];
                float T = 0.002f * fm_exp2f(x1 * 12.0f) * fm_powf(rel > 1.0f ? rel : 1.0f, -dark * 1.2f);
                wf_pole(wcx.md[wcx.mn[i]][k], f, fm_expf(-6.9078f * RV_DT / T), fm_powf((float)(k + 1), -dark * 1.5f) * 0.5f);
            }
        }
        break;
    case WO_DIFFUSE: wcx.c1[i] = 0.08f + 0.92f * x1; wcx.c2[i] = x2 * 0.75f; break;
    case WO_OMOD: wcx.c1[i] = x1 > 0.0f ? wk(0.0005f + x1 * x1 * 0.5f) : 1.0f; break;
    default: break;
    }
}

static void weft_voice(wv_t *v, int t, const float *eff, float hz, float *out, int n, int scope)
{
    const rv_weft *w = &rv.weft[t];
    float lfo1 = lfo[0].value, lfo2 = lfo[1].value, wheel = midi_wheel, pitch = ((float)v->note + eff[TP_PITCH] - 60.0f) * (1.0f / 48.0f);
    float ka[WF_BLOCKS], kb[WF_BLOCKS];
    float *sv = v->sv;
    uint8_t sa[WF_BLOCKS], sb[WF_BLOCKS];
    int i, j, outb = w->out ? (w->out - 1) % WF_BLOCKS : -1, nd = 0, nm = 0;
    float peak = 0.0f, pos = weft_pos16, pinc = RV_DT / (clk_period > 0.001f ? clk_period : 0.001f);
    unsigned bm = w->bmove;
    /* what each block needs per sample, worked out once per step */
    wcx.hz = hz;
    sv[0] = 0.0f;
    sv[WS_PITCH] = pitch;
    sv[WS_VEL] = v->vel;
    sv[WS_LFO1] = lfo1;
    sv[WS_LFO2] = lfo2;
    sv[WS_WHEEL] = wheel;
    sv[WS_ONE] = 1.0f;
    for (i = 0; i < WF_BLOCKS; i++) {
        int ty = w->type[i];
        wcx.dn[i] = wcx.mn[i] = -1;
        sa[i] = (uint8_t)(w->a[i] < WS_N ? w->a[i] : 0);
        sb[i] = (uint8_t)(w->b[i] < WS_N ? w->b[i] : 0);
        ka[i] = (float)w->amta[i] * 0.01f;
        kb[i] = (float)w->amtb[i] * 0.01f;
        if ((ty == WO_DELAY || ty == WO_DIFFUSE) && nd < WD_N)
            wcx.dn[i] = (int8_t)nd++;
        if (WO_MODES(ty) && nm < WM_N)
            wcx.mn[i] = (int8_t)nm++;
        if (ty)
            wf_setup(i, ty, clampf(eff[wf_sp(2 * i)], 0.0f, 127.0f) * (1.0f / 127.0f), clampf(eff[wf_sp(2 * i + 1)], 0.0f, 127.0f) * (1.0f / 127.0f), 0.0f);
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
            float a, b, y = 0.0f, x1, x2;
            if (!ty)
                continue;
            a = sv[sa[i]] * ka[i];
            b = sv[sb[i]] * kb[i];
            if ((bm >> i) & 1u) {               /* B MOVES: B turns the second setting (worked out every 8th sample) */
                if (bc && !(j & 7))
                    wf_setup(i, ty, wcx.p1[i], clampf(clampf(eff[wf_sp(2 * i + 1)], 0.0f, 127.0f) * (1.0f / 127.0f) + b * 0.5f, 0.0f, 1.0f), 0.0f);
                b = 0.0f;
                bc = 0;
            } else if (bc && !(j & 7) && (ty == WO_RING || WO_MODES(ty)))   /* the resonators' PITCH, two octaves each way */
                wf_setup(i, ty, wcx.p1[i], wcx.p2[i], b * 2.0f);
            x1 = wcx.p1[i];
            x2 = wcx.p2[i];
            switch (ty) {
            case WO_PHASE:                      /* a ramp at the note x RATIO; A: FM (through zero), B: reset */
                s->ph += wcx.c1[i] * (1.0f + 8.0f * a);
                s->ph -= fm_floorf(s->ph);
                if (bc && wrise(b, &s->prevb, 0.0f))
                    s->ph = 0.0f;
                y = s->ph * 2.0f - 1.0f;
                break;
            case WO_WAVE: {                     /* reads a phase (A, else its own at the note); B: phase modulation */
                float p, k = clampf(0.5f + WC(x2) * 0.5f, 0.03f, 0.97f);
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
                    s->y += (1.25f - s->y) * clampf(wcx.kr[i] * sc, 0.0f, 1.0f);
                    if (s->y >= 1.0f) {
                        s->y = 1.0f;
                        s->st = 2;
                    }
                } else if (s->st == 2 && g <= 0.5f)
                    s->st = 0;
                if (!s->st)
                    s->y -= s->y * clampf(wcx.kf[i] * sc, 0.0f, 1.0f);
                y = s->y;
                break;
            }
            case WO_CYCLE: {                    /* rises and falls on its own (SKEW: which takes longer); A: rate, B: reset */
                float inc = wcx.c1[i] * (ac ? fm_exp2f(a * 5.0f) : 1.0f), k = wcx.c2[i];
                s->ph += inc;
                s->ph -= fm_floorf(s->ph);
                if (bc && wrise(b, &s->prevb, 0.0f))
                    s->ph = 0.0f;
                y = (s->ph < k ? s->ph / k : (1.0f - s->ph) / (1.0f - k)) * 2.0f - 1.0f;
                break;
            }
            case WO_SLEW: {                     /* follows A, taking RISE to go up and FALL to come down */
                float sc = bc ? fm_exp2f(-b * 4.0f) : 1.0f;
                s->y += (a - s->y) * clampf((a > s->y ? wcx.kr[i] : wcx.kf[i]) * sc, 0.0f, 1.0f);
                y = s->y;
                break;
            }
            case WO_LP: case WO_BP: case WO_HP: {   /* a state-variable filter; B: the cutoff, five octaves each way */
                float g, R = wcx.c2[i] * 0.5f, hp, bp, lp;
                if (bc && !(j & 3))             /* (cutoff moved by B: worked out every fourth sample) */
                    s->hold = fm_tanf(3.14159265f * clampf(wcx.c1[i] * fm_exp2f(b * 5.0f), 20.0f, 18000.0f) * RV_DT);
                g = bc ? (s->hold > 0.0f ? s->hold : wcx.fg[i]) : wcx.fg[i];
                hp = (a - (2.0f * R + g) * s->s1 - s->s2) / (1.0f + 2.0f * R * g + g * g);
                bp = g * hp + s->s1;
                s->s1 = g * hp + bp;
                lp = g * bp + s->s2;
                s->s2 = g * bp + lp;
                y = ty == WO_LP ? lp : ty == WO_BP ? bp * (2.0f * R) : hp;
                if (!(y > -8.0f && y < 8.0f))   /* (blown up: start over) */
                    s->s1 = s->s2 = y = 0.0f;
                break;
            }
            case WO_ADD: y = (a + b + WC(x1) * 2.0f) * WG(x2); break;
            case WO_MULT: y = a * ((bc ? b : 1.0f) + WC(x1) * 2.0f) * WG(x2); break;
            case WO_MIX: y = (a + (b - a) * x1) * WG(x2); break;
            case WO_MIN: y = fm_minf(a, b + WC(x1) * 2.0f) * WG(x2); break;
            case WO_MAX: y = fm_maxf(a, b + WC(x1) * 2.0f) * WG(x2); break;
            case WO_FOLD:                       /* driven round a sine: folds back as it grows */
                y = fm_sin_turns((a * (1.0f + x1 * 15.0f) * (1.0f + (bc ? b : 0.0f)) + WC(x2)) * 0.25f);
                break;
            case WO_COMP: case WO_AND: case WO_OR: case WO_XOR: case WO_FLIP: {
                float thr = WC(x2);
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
                if (wcx.dn[i] < 0)
                    break;
                m = wdly[v - wv][wcx.dn[i]];
                d = clampf(wcx.c1[i] * (bc ? fm_exp2f(b * 2.0f) : 1.0f), 2.0f, (float)(WD_LEN - 2));
                k = v->dw[wcx.dn[i]];
                fr = (float)k - d;
                if (fr < 0.0f)
                    fr += (float)WD_LEN;
                i0 = (int)fr;
                fr -= (float)i0;
                r0 = (float)m[i0];
                r1 = (float)m[(i0 + 1) % WD_LEN];
                y = (r0 + (r1 - r0) * fr) * (1.0f / 16384.0f);
                wr = sat(a + y * wcx.c2[i]);
                m[k] = (int16_t)(wr * 16384.0f);
                v->dw[wcx.dn[i]] = (uint16_t)((k + 1) % WD_LEN);
                break;
            }
            case WO_BANDS: {                    /* four bands at the note's 1st-4th harmonics (SPREAD bends them); B: scan */
                int k;
                float sum = 0.0f, pos2 = (b * 0.5f + 0.5f) * 3.0f;
                for (k = 0; k < 4; k++) {
                    float g = wcx.bg[i][k], R = wcx.c2[i], hp, bp, wgt;
                    hp = (a - (2.0f * R + g) * s->z[2 * k] - s->z[2 * k + 1]) / (1.0f + 2.0f * R * g + g * g);
                    bp = g * hp + s->z[2 * k];
                    s->z[2 * k] = g * hp + bp;
                    s->z[2 * k + 1] = g * bp + (g * bp + s->z[2 * k + 1]);
                    wgt = bc ? fm_maxf(0.0f, 1.0f - fm_fabsf((float)k - pos2)) : 0.5f;
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
            case WO_DUST: {                     /* random particles; each one makes more likely soon after it (CLUSTER), so
                                                 * they come in clumps that spread out. A: density; B (else each note): a burst */
                float rate = wcx.c1[i] * (ac ? fm_exp2f(a * 6.0f) : 1.0f), tau = 0.04f, br = x2 * 0.95f / (rate * tau + 1e-3f);
                s->s1 *= 1.0f - RV_DT / tau;
                if (bc ? wrise(b, &s->prevb, 0.0f) : trig > 0.0f)
                    s->s1 += 6.0f / (rate * tau + 1e-3f) * (0.2f + x2);
                if (s->s1 > 400.0f)
                    s->s1 = 400.0f;
                if (xs_uni(&v->rng) < rate * (1.0f + s->s1) * RV_DT) {
                    y = 0.25f + 0.75f * xs_uni(&v->rng);
                    s->s1 += br;
                }
                break;
            }
            case WO_TICK: {                     /* a click each DIV of the tempo (on the transport's grid), CHANCE of it
                                                 * sounding; A: ticks only while it is up; B: each one late by up to B x half a DIV */
                float L = wcx.c1[i], at = fm_floorf((pos + pinc * (float)j) / L);
                if (at != s->hold) {
                    s->hold = at;
                    if ((!ac || a > 0.0f) && xs_uni(&v->rng) * 0.999f < x2)
                        s->s1 = bc ? fm_fabsf(b) * xs_uni(&v->rng) * L * 0.5f * clk_period * (float)RV_FS + 1.0f : 1.0f;
                }
                if (s->s1 > 0.0f && (s->s1 -= 1.0f) <= 0.0f)
                    y = 1.0f;
                break;
            }
            case WO_RING: {                     /* one resonator, struck by A (an impulse rings at about A's height) */
                y = wcx.kr[i] * s->s1 + wcx.kf[i] * s->s2 + wcx.fg[i] * a;
                s->s2 = s->s1;
                s->s1 = y;
                if (!(y > -16.0f && y < 16.0f))
                    s->s1 = s->s2 = y = 0.0f;
                break;
            }
            case WO_SKIN: case WO_BAR: case WO_PLATE: case WO_BELL: case WO_HARMS: case WO_FIXED: {
                int k, m = wcx.mn[i];           /* six resonators tuned to a body, struck by A */
                float *z;
                if (m < 0)
                    break;
                z = v->mz[m];
                for (k = 0; k < WM_K; k++) {
                    const float *c = wcx.md[m][k];
                    float yk = c[0] * z[2 * k] + c[1] * z[2 * k + 1] + c[2] * a;
                    z[2 * k + 1] = z[2 * k];
                    z[2 * k] = yk;
                    y += yk;
                }
                if (!(y > -16.0f && y < 16.0f)) {
                    __builtin_memset(z, 0, sizeof v->mz[0]);
                    y = 0.0f;
                }
                break;
            }
            case WO_SAT:                        /* A driven into a soft clip; B: more drive */
                y = sat((a * (1.0f + x1 * 15.0f) * (bc ? fm_exp2f(b * 2.0f) : 1.0f) + WC(x2)) * 0.8f);
                break;
            case WO_ABS: y = fm_fabsf(a + b + WC(x1) * 2.0f) * WG(x2); break;
            case WO_DIFFUSE: {                  /* four allpasses in a row, each in a quarter of the memory; B: their size */
                static const float base[4] = {0.43f, 0.61f, 0.79f, 1.0f};
                int16_t *m;
                int k, wpos;
                float g = wcx.c2[i], sz = wcx.c1[i] * (bc ? fm_exp2f(b) : 1.0f), x = a;
                if (wcx.dn[i] < 0)
                    break;
                m = wdly[v - wv][wcx.dn[i]];
                wpos = v->dw[wcx.dn[i]] & 127;
                for (k = 0; k < 4; k++) {
                    int L = clampi((int)(base[k] * 125.0f * sz), 1, 127);
                    float d = (float)m[k * 128 + ((wpos - L) & 127)] * (1.0f / 8192.0f), o = d - g * x;
                    m[k * 128 + wpos] = (int16_t)(clampf(x + g * o, -3.9f, 3.9f) * 8192.0f);
                    x = o;
                }
                v->dw[wcx.dn[i]] = (uint16_t)((wpos + 1) & 127);
                y = x;
                break;
            }
            case WO_OCLK:                       /* A's rises (through THRESH), every DIV-th: the WEFT clock; B: back to step 1 */
                y = a;
                if (wrise(a, &s->prev, WC(x2)) && ++s->st > WN(x1, 16)) {
                    s->st = 1;
                    if (t == wf_clk_owner)
                        weft_clk_fired = 1;
                }
                if (bc && wrise(b, &s->prevb, 0.0f) && t == wf_clk_owner)
                    weft_clk_reset = 1;
                break;
            case WO_OMOD: {                     /* A (x B), smoothed: the matrix's WEFT source */
                float x = a * (bc ? b : 1.0f);
                s->y += (x - s->y) * wcx.c1[i];
                y = s->y;
                if (t == wf_mod_owner)
                    weft_mod_v = WN(x2, 2) ? clampf(y * 0.5f + 0.5f, 0.0f, 1.0f) : clampf(y, -1.0f, 1.0f);
                break;
            }
            default: break;
            }
            sv[i + 1] = y;
            if (scope && wsc[i].on) {           /* the scope: a moment of each block (each point the biggest of
                                                 * those it stands for), started on a rising crossing */
                if (wsc[i].wait) {
                    if ((y >= 0.0f && wsc[i].last < 0.0f) || ++wsc[i].wait > 200) {
                        wsc[i].wait = 0;
                        wsc[i].idx = 0;
                        wsc[i].dec = 99;
                        wsc[i].pk = 0.0f;
                    }
                    wsc[i].last = y;
                }
                if (!wsc[i].wait) {
                    if (fm_fabsf(y) >= fm_fabsf(wsc[i].pk))
                        wsc[i].pk = y;
                    if (++wsc[i].dec >= (ty == WO_SLOPE || ty == WO_SLEW || ty == WO_HOLD || ty == WO_DUST || ty == WO_TICK || ty == WO_OMOD ||
                                         ty == WO_OCLK || (ty == WO_CYCLE && wcx.c1[i] < 0.0005f) ? 96 : 3)) {
                        wsc[i].dec = 0;
                        wsc[i].buf[wsc[i].idx++] = (int8_t)clampf(wsc[i].pk * 100.0f, -127.0f, 127.0f);
                        wsc[i].pk = 0.0f;
                        if (wsc[i].idx >= 32) {
                            __builtin_memcpy((void *)rv_live.wscope[i], wsc[i].buf, 32);
                            wsc[i].wait = 1;
                            wsc[i].last = y;
                        }
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
    /* a released voice ends when it has been quiet a moment (or after a long while); one a patch's
     * output block needs goes on */
    if (!v->gate) {
        v->rel += (float)n * RV_DT;
        v->quiet = peak < 0.0005f ? v->quiet + (float)n * RV_DT : 0.0f;
        if (!wf_has_out[t] && (v->quiet > 0.25f || v->rel > 12.0f || outb < 0))
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

/* each step, before the voices: voices of tracks that are no longer WEFT go; a patch with an output block
 * keeps a voice; the clock's place on the tempo's grid moves on */
static void weft_tidy(int n)
{
    int i, t, c = 0;
    wf_clk_owner = wf_mod_owner = -1;
    for (t = RV_TRACKS - 1; t >= 0; t--) {
        int oc = 0, om = 0;
        if (rv.tp[t][TP_KIND] == KIND_WEFT)
            for (i = 0; i < WF_BLOCKS; i++) {
                oc |= rv.weft[t].type[i] == WO_OCLK;
                om |= rv.weft[t].type[i] == WO_OMOD;
            }
        wf_has_out[t] = (uint8_t)(oc || om);
        if (oc)
            wf_clk_owner = (int8_t)t;           /* (the lowest track with one) */
        if (om)
            wf_mod_owner = (int8_t)t;
    }
    if (wf_mod_owner < 0)
        weft_mod_v = 0.0f;
    for (i = 0; i < WV_N; i++) {
        if (wv[i].on && rv.tp[wv[i].track & 7][TP_KIND] != KIND_WEFT)
            wv[i].on = 0;
    }
    for (t = 0; t < RV_TRACKS; t++)
        if (wf_has_out[t]) {
            for (i = 0; i < WV_N && !(wv[i].on && wv[i].track == t); i++)
                ;
            if (i == WV_N)
                weft_voice_start(t, wf_last_note[t], 0, 1);
        }
    for (i = 0; i < WV_N; i++)
        c += wv[i].on;
    rv_live.wvoices = (uint8_t)c;
    weft_pos16 += (float)n * RV_DT / (clk_period > 0.001f ? clk_period : 0.001f);
    if (weft_pos16 >= 384.0f)                   /* (a whole number of every division's length) */
        weft_pos16 -= 384.0f;
}
