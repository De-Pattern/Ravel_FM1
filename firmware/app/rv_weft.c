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
#define WM_N 3                                  /* filter-bank blocks (SKIN ... FIXED, CHEBY, FORMANT, VOCODER) a patch can use */
#define WM_K 6                                  /* modes in one */

typedef struct {
    float ph, y, s1, s2, prev, prevb, hold, z[8];
    uint32_t rng;                               /* its own dice (DUST, TICK, the switches, CHAOS, NOISE, RUNGLER):
                                                 * a block's draws don't depend on the order the blocks run in */
    uint8_t st;
} wb_t;                                         /* a block's state in one voice */

typedef struct {
    uint8_t on, track, note, gate, trig, keep;
    uint32_t age, rng;
    float vel, quiet, rel;                      /* how long it has been quiet, and since its release (s) */
    float sv[WS_N];                             /* what an input can read: 0 nothing, 1-12 the blocks' outputs, then the built-ins */
    wb_t b[WF_BLOCKS];
    float mz[WM_N][32];                         /* the filter-bank blocks' state */
    uint16_t dw[WD_N];
    struct wcx_s *W;                            /* the core's working values it plays with this step */
} wv_t;

static wv_t wv[WV_N] RV_RAMBSS;            /* (in plain RAM: the pool is full) */
static int16_t wdly[WV_N][WD_N][WD_LEN] RV_RAMBSS;   /* (plain RAM too: the pool holds the voice loop) */
static uint32_t wv_age;
static uint8_t wf_has_out[RV_TRACKS], wf_last_note[RV_TRACKS] = {60, 60, 60, 60, 60, 60, 60, 60};
static int8_t wf_clk_owner = -1, wf_mod_owner = -1, wf_lead[RV_TRACKS];
static struct { uint8_t on, idx, dec, wait; float last, pk; int8_t buf[32]; } wsc[WF_BLOCKS];   /* the scope's capture (the newest voice of the track on screen) */

/* what a voice's blocks need, worked out once a step (and again for a block whose B moves a setting); one for
 * each core, as the second may play a WEFT voice while this one plays another */
typedef struct wcx_s {
    float p1[WF_BLOCKS], p2[WF_BLOCKS];         /* the settings, 0..1 */
    float c1[WF_BLOCKS], c2[WF_BLOCKS], kr[WF_BLOCKS], kf[WF_BLOCKS], fg[WF_BLOCKS], fd[WF_BLOCKS];   /* (fd: a filter's 1 / its denominator) */
    float bg[WF_BLOCKS][4], bd[WF_BLOCKS][4];   /* BANDS: each band's g, 1 / its denominator */
    float md[WM_N][WM_K][5];                    /* the filter-bank blocks' coefficients (the six-mode resonators: per mode a1, a2, gain) */
    int8_t dn[WF_BLOCKS], mn[WF_BLOCKS];        /* a block's delay memory, its six-mode slot (-1 none) */
    float hz;
} wcx_t;
static wcx_t wcxs[2];

/* (the bigger blocks' work, RV_HOT2: run from RAM on the FM-1, in the pool's code; the rest of WEFT runs from the
 * flash) */
/* SHIFT's carrier: a cosine and sine turned by inc each sample (a rotation, its length kept at 1), not two sines
 * worked out each sample */
RV_INL void wf_rot(wb_t *s, float inc)
{
    float th = inc * 6.2831853f, t2 = th * th, cd = 1.0f - t2 * 0.5f, sd = th - th * t2 * (1.0f / 6.0f);
    float c = s->s1 * cd - s->s2 * sd, sn = s->s1 * sd + s->s2 * cd, g = 1.5f - 0.5f * (c * c + sn * sn);
    if (!(g > 0.5f && g < 1.5f))
        c = 1.0f, sn = 0.0f, g = 1.0f;
    s->s1 = c * g;
    s->s2 = sn * g;
}
/* SHIFT (wk_shift): A split into two copies 90 degrees apart (two chains of four allpasses, within 1.5 degrees from 40 Hz to
 * 15 kHz), turned by a sine and cosine at the shift: every partial moves by the same Hz. SIDE: 0 down, 127 up, 64
 * both (a ring modulator) */
static const float wf_hil[8] = {-0.51977f, 0.562206f, -0.915577f, -0.988316f, -0.78934f, -0.997096f, -0.96769f, -0.06218f};

/* the bodies' modes: frequency ratios (FIXED: Hz) */
static const float wf_modes[6][WM_K] = {
    {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f},       /* SKIN: a circular membrane */
    {1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f},     /* BAR: free at both ends */
    {1.0f, 1.84f, 2.71f, 3.32f, 4.19f, 5.13f},            /* PLATE */
    {0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f},           /* BELL: hum, prime, tierce, quint, nominal, ... */
    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},                 /* HARMS */
    {150.0f, 310.0f, 640.0f, 1300.0f, 2700.0f, 5500.0f},  /* FIXED: a filterbank */
};

/* a number scrambled (a block's dice seeded from the voice's) */
static uint32_t in_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* the math the blocks call, out of line: each inlined at every use would not fit the FM-1's flash */
static __attribute__((noinline)) float wx_exp2(float x) { return fm_exp2f(x); }
static __attribute__((noinline)) float wx_exp(float x) { return fm_expf(x); }
static __attribute__((noinline)) float wx_tan(float x) { return fm_tanf(x); }
static __attribute__((noinline)) float wx_pow(float x, float y) { return fm_powf(x, y); }
static __attribute__((noinline)) float wx_log2(float x) { return fm_log2f(x); }

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
        int lim = rv_live.cap != 255 && !rv_live.guard_off ? (rv_live.relief >= 2 ? 1 : 2) : WV_N;   /* (the overload guard holding voices back: two at most, one when it eases the rest too) */
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
RV_INL float wtime(float x) { return 0.0005f * RV_FAR(wx_exp2)(x * 14.0f); }
/* a one-pole step toward a target over time T (s) */
RV_INL float wk(float T) { return 1.0f - RV_FAR(wx_exp)(-RV_DT / (T > 1e-5f ? T : 1e-5f)); }
/* a ring's decay (T60): 2 ms .. 8 s, as its pole's radius */
RV_INL float wdecay(float x) { return RV_FAR(wx_exp)(-6.9078f * RV_DT / (0.002f * RV_FAR(wx_exp2)(x * 12.0f))); }

/* SIN, TRI, SAW and SQUARE in turn, SHAPE blending each into the next */
RV_INL float wf_shape(int k, float p)
{
    return k == 0 ? fm_sin_turns(p) : k == 1 ? (p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f)
         : k == 2 ? 2.0f * p - 1.0f : (p < 0.5f ? 1.0f : -1.0f);
}
RV_INL float wf_wave(float p, float shape)
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

/* a phase back into 0..1 (as p - floor(p), without the floor most of the time) */
RV_INL float wwrap(float p)
{
    if (p >= 1.0f) {
        p -= 1.0f;
        if (p >= 1.0f)
            p -= fm_floorf(p);
    } else if (p < 0.0f) {
        p += 1.0f;
        if (p < 0.0f)
            p -= fm_floorf(p);
    }
    return p;
}
/* a state-variable filter's 1 / (1 + 2Rg + g^2), worked out when g or R changes, not each sample */
RV_INL float wsvd(float g, float R) { return 1.0f / (1.0f + 2.0f * R * g + g * g); }

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
static __attribute__((noinline)) void wf_setup(wcx_t *W, int i, int ty, float x1, float x2, float pm, int full)
{
    float hz = W->hz;
    W->p1[i] = x1;
    W->p2[i] = x2;
    switch (ty) {
    case WO_SLOPE: case WO_SLEW: W->kr[i] = wk(wtime(x1)); W->kf[i] = wk(wtime(x2)); break;
    case WO_PHASE: W->c1[i] = hz * wf_ratio_v[WN(x1, 16)] * wx_exp2(WC(x2) * (1.0f / 12.0f)) * RV_DT; break;
    case WO_CYCLE: W->c1[i] = 0.02f * wx_exp2(x1 * 17.0f) * RV_DT; W->c2[i] = clampf(x2, 0.01f, 0.99f); break;
    case WO_LP: case WO_BP: case WO_HP:
        W->c1[i] = 20.0f * wx_exp2(x1 * 10.0f);
        W->c2[i] = 2.0f - 1.96f * x2;
        W->fg[i] = wx_tan(3.14159265f * clampf(W->c1[i], 20.0f, 18000.0f) * RV_DT);
        W->fd[i] = wsvd(W->fg[i], W->c2[i] * 0.5f);
        break;
    case WO_BANDS: {
        int k;
        W->c1[i] = wx_exp2(WC(x1));
        W->c2[i] = 0.5f - 0.48f * x2;
        for (k = 0; k < 4; k++) {               /* (the bands sit still through a step) */
            W->bg[i][k] = wx_tan(3.14159265f * clampf(hz * wx_pow((float)(k + 1), W->c1[i]), 20.0f, 16000.0f) * RV_DT);
            W->bd[i][k] = wsvd(W->bg[i][k], W->c2[i]);
        }
        break;
    }
    case WO_DELAY:
        W->c1[i] = x1 < 0.004f ? (float)RV_FS / (hz > 20.0f ? hz : 20.0f) - 2.0f : 0.0001f * wx_exp2(x1 * 6.8f) * (float)RV_FS;
        while (W->c1[i] > (float)(WD_LEN - 2))
            W->c1[i] *= 0.5f;                  /* (a period too long for the memory: an octave up) */
        W->c2[i] = WC(x2);
        break;
    case WO_DUST: W->c1[i] = 0.5f * wx_exp2(x1 * 12.0f); break;   /* particles a second: 0.5 .. 2000 */
    case WO_TICK: W->c1[i] = wf_div_v[WN(x1, 12)]; break;
    case WO_RING: {
        float c[3];
        if (full)
            W->c2[i] = wdecay(x2);              /* (its radius: the pitch alone leaves it) */
        wf_pole(c, hz * wf_ratio_v[WN(x1, 16)] * wx_exp2(pm), W->c2[i], 1.0f);
        W->kr[i] = c[0], W->kf[i] = c[1], W->fg[i] = c[2];   /* (a1, a2, the input's gain) */
        break;
    }
    case WO_SKIN: case WO_BAR: case WO_PLATE: case WO_BELL: case WO_HARMS: case WO_FIXED:
        if (W->mn[i] >= 0) {
            const float *r = wf_modes[ty - WO_SKIN];
            float base = ty == WO_FIXED ? wx_exp2(pm) : hz * wx_exp2(pm), dark = 1.0f - x2;
            int k;
            for (k = 0; k < WM_K; k++) {        /* BRIGHT: dark, the high modes softer and shorter */
                float *c = W->md[W->mn[i]][k], f = base * r[k];
                if (full) {                     /* (each mode's radius and level: the pitch alone leaves them) */
                    float rel = r[k] / r[ty == WO_BELL ? 1 : 0];
                    float T = 0.002f * wx_exp2(x1 * 12.0f) * wx_pow(rel > 1.0f ? rel : 1.0f, -dark * 1.2f);
                    c[3] = wx_exp(-6.9078f * RV_DT / T);
                    c[4] = wx_pow((float)(k + 1), -dark * 1.5f) * 0.5f;
                }
                wf_pole(c, f, c[3], c[4]);
            }
        }
        break;
    case WO_DIFFUSE: W->c1[i] = 0.08f + 0.92f * x1; W->c2[i] = x2 * 0.75f; break;
    case WO_SW14: case WO_SW41: W->c2[i] = x2 > 0.0f ? wk(0.0005f + x2 * 0.02f) : 1.0f; break;   /* SMOOTH: 0 .. 20 ms */
    case WO_STRING: {                           /* the loop's length (less what the damping adds), its gain a pass, the damping */
        float k = 1.0f - 0.9f * x1, P = (float)RV_FS / clampf(hz * wx_exp2(pm), 20.0f, 12000.0f) - (1.0f - k) / k - 1.0f;
        while (P > (float)(WD_LEN - 2))
            P *= 0.5f;                          /* (a period too long for the memory: an octave up) */
        W->c1[i] = P < 2.0f ? 2.0f : P;
        W->c2[i] = wx_exp(-6.9078f * W->c1[i] / (0.05f * wx_exp2(x2 * 7.5f) * (float)RV_FS));   /* DECAY: 50 ms .. 9 s */
        W->kr[i] = k;
        break;
    }
    case WO_CHAOS: W->c1[i] = 0.1f * wx_exp2(x2 * 14.6f) * RV_DT; W->c2[i] = 3.4f + 0.6f * x1; break;   /* steps a sample; r */
    case WO_CRUSH: W->c1[i] = 0.5f * wx_exp2(16.0f - x1 * 15.0f); W->c2[i] = wx_exp2(x2 * 6.0f); break;   /* half the levels; samples held */
    case WO_FOLLOW: W->kr[i] = wk(wtime(x1)); W->kf[i] = wk(wtime(x2)); break;
    case WO_SHAPE: W->c1[i] = 1.0f + x2 * 15.0f; break;
    case WO_RMOD: W->c1[i] = hz * wx_exp2(x1 * 7.0f - 3.0f) * RV_DT; break;   /* the inner carrier: the note x 1/8 .. 16 */
    case WO_SHIFT: {                            /* Hz a sample, either way of 64: 0 .. 2 kHz, finest near the middle */
        float d = (x1 - 0.5f) * 2.0f, m = 0.1f * (wx_exp2((d < 0.0f ? -d : d) * 14.3f) - 1.0f);
        W->c1[i] = (d < 0.0f ? -m : m) * RV_DT;
        break;
    }
    case WO_DIODE: W->c1[i] = 1.0f + x1 * x1 * 30.0f; break;
    case WO_NOISE: W->c2[i] = wx_exp2((1.0f - x2) * 10.0f); break;           /* samples each is held: 1 .. 1024 */
    case WO_LISSA: W->c1[i] = 0.02f * wx_exp2(x1 * 17.0f) * RV_DT; break;
    case WO_CHEBY:                              /* four poles, Chebyshev type I: CUTOFF, RIPPLE 0.1 .. 6 dB; two biquads */
        if (W->mn[i] >= 0) {
            float *c = &W->md[W->mn[i]][0][0], fc = clampf(20.0f * wx_exp2(x1 * 10.0f + pm * 2.5f), 20.0f, 18000.0f), sh, ch, Wc;
            if (full) {                         /* (the ripple's shape: the cutoff alone leaves it) */
                float eps = fm_sqrtf(wx_exp2((0.1f + x2 * 5.9f) * 0.33219f) - 1.0f), iv = 1.0f / eps;
                float v = wx_log2(iv + fm_sqrtf(iv * iv + 1.0f)) * (0.69315f / 4.0f), ev = wx_exp(v);
                c[7] = 0.5f * (ev - 1.0f / ev);
                c[8] = 0.5f * (ev + 1.0f / ev);
                c[6] = 1.0f / fm_sqrtf(1.0f + eps * eps);
            }
            sh = c[7];
            ch = c[8];
            Wc = wx_tan(3.14159265f * fc * RV_DT);
            int k;
            for (k = 0; k < 2; k++) {
                float th = (float)(2 * k + 1) * (1.0f / 16.0f), sg = sh * fm_sin_turns(th), om = ch * fm_sin_turns(th + 0.25f);
                float w0 = fm_sqrtf(sg * sg + om * om), K = w0 * Wc, Q = w0 / (2.0f * sg), nm = 1.0f / (1.0f + K / Q + K * K);
                c[3 * k] = K * K * nm;
                c[3 * k + 1] = 2.0f * (K * K - 1.0f) * nm;
                c[3 * k + 2] = (1.0f - K / Q + K * K) * nm;
            }
        }
        break;
    case WO_FORMANT:                            /* three band passes at a vowel's formants: A E I O U, morphed; SHIFT +-1 octave */
        if (W->mn[i] >= 0) {
            static const float fv[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
            static const float bw[3] = {80.0f, 90.0f, 120.0f}, am[3] = {1.0f, 0.5f, 0.25f};
            float vw = clampf(x1 + pm * 0.25f, 0.0f, 1.0f) * 3.999f, fr = vw - fm_floorf(vw), sft = wx_exp2(WC(x2));
            int v0 = (int)vw, k;
            for (k = 0; k < 3; k++) {
                float f = clampf((fv[v0][k] + (fv[v0 + 1][k] - fv[v0][k]) * fr) * sft, 50.0f, 16000.0f);
                W->md[W->mn[i]][k][0] = wx_tan(3.14159265f * f * RV_DT);
                W->md[W->mn[i]][k][1] = bw[k] * sft / (2.0f * f);
                W->md[W->mn[i]][k][2] = am[k];
                W->md[W->mn[i]][k][3] = wsvd(W->md[W->mn[i]][k][0], W->md[W->mn[i]][k][1]);
            }
        }
        break;
    case WO_VOCODER:                            /* six bands, 250 Hz .. 4 kHz: SHARP their width, SHIFT the carrier's +-1 octave */
        if (W->mn[i] >= 0) {
            float R = 0.3f - 0.27f * x1, sft = wx_exp2(WC(x2));
            int k;
            for (k = 0; k < WM_K; k++) {
                float f = 250.0f * wx_exp2((float)k * 0.8f);
                W->md[W->mn[i]][k][0] = wx_tan(3.14159265f * f * RV_DT);
                W->md[W->mn[i]][k][1] = wx_tan(3.14159265f * clampf(f * sft, 50.0f, 16000.0f) * RV_DT);
                W->md[W->mn[i]][k][2] = R;
                W->md[W->mn[i]][k][3] = wsvd(W->md[W->mn[i]][k][0], R);
                W->md[W->mn[i]][k][4] = wsvd(W->md[W->mn[i]][k][1], R);
            }
            W->c1[i] = wk(0.002f);
            W->c2[i] = wk(0.03f);
        }
        break;
    case WO_OMOD: W->c1[i] = x1 > 0.0f ? wk(0.0005f + x1 * x1 * 0.5f) : 1.0f; break;
    default: break;
    }
}

/* the block heard: OUT's (1-12), or with OUT on AUTO the last that isn't an output block (none if there are AUDIO
 * blocks: then they are the sound); -1 none */
static int wf_outb(const rv_weft *w)
{
    int i, aud = 0;
    if (w->out >= 1 && w->out <= WF_BLOCKS)
        return w->out - 1;
    if (w->out != WF_OUT_AUTO)
        return -1;
    for (i = 0; i < WF_BLOCKS; i++)
        aud |= w->type[i] == WO_AOUT;
    for (i = WF_BLOCKS - 1; i >= 0 && !aud; i--)
        if (w->type[i] && !WO_ISOUT(w->type[i]))
            return i;
    return -1;
}
int rv_weft_outb(int t) { return wf_outb(&rv.weft[t & 7]); }

/* the bigger blocks, each in a function of its own (the sample loop stays small enough for the FM-1's branches) */
/* a sequential switch: B's rises (else each note) step it in ORDER, or in SELECT B's level picks; SMOOTH fades
 * from one to the next */
RV_INL float wf_switch(wv_t *v, wb_t *s, int i, int ty, float a, float b, int bc, float trig, float x1, const float *in4, float kai)
{
    wcx_t *W = v->W;
    float y = 0.0f;
    int pos = s->st & 3, ord = WN(x1, 5), k;
    if (ord == WSW_SELECT) {
        if (bc)
            pos = clampi((int)((b * 0.5f + 0.5f) * 4.0f), 0, 3);
    } else if (bc ? wrise(b, &s->prevb, 0.0f) : trig > 0.0f) {
        if (ord == WSW_UP)
            pos = (pos + 1) & 3;
        else if (ord == WSW_DOWN)
            pos = (pos + 3) & 3;
        else if (ord == WSW_PING) {
            if (s->ph == 0.0f)
                s->ph = 1.0f;
            pos += s->ph > 0.0f ? 1 : -1;
            if (pos > 3)
                pos = 2, s->ph = -1.0f;
            else if (pos < 0)
                pos = 1, s->ph = 1.0f;
        } else
            pos = (int)(xs_uni(&s->rng) * 4.0f) & 3;
    }
    s->st = (uint8_t)pos;
    for (k = 0; k < 4; k++)
        s->z[k] += ((k == pos ? 1.0f : 0.0f) - s->z[k]) * W->c2[i];
    if (ty == WO_SW14)              /* one in, four outs (SW 1-4: the caller's); its own output, where it is (-1 .. +1) */
        y = (float)pos * (2.0f / 3.0f) - 1.0f;
    else if (in4)                   /* four in (A's source and the three after it, as the caller read them), one out */
        for (k = 0; k < 4; k++)
            y += in4[k] * kai * s->z[k];
    return y;
}
/* Karplus-Strong: A excites a loop the note's period long, damped as it goes round */
RV_INL float wf_string(wv_t *v, wb_t *s, int i, float a)
{
    wcx_t *W = v->W;
    int16_t *m;
    float y, fr, r0, r1;
    int k, i0;
    if (W->dn[i] < 0)
        return 0.0f;
    m = wdly[v - wv][W->dn[i]];
    k = v->dw[W->dn[i]];
    fr = (float)k - W->c1[i];
    if (fr < 0.0f)
        fr += (float)WD_LEN;
    i0 = (int)fr;
    fr -= (float)i0;
    r0 = (float)m[i0];
    r1 = (float)m[(i0 + 1) % WD_LEN];
    s->y += ((r0 + (r1 - r0) * fr) * (1.0f / 16384.0f) - s->y) * W->kr[i];
    y = s->y;
    m[k] = (int16_t)(sat(a + y * W->c2[i]) * 16384.0f);
    v->dw[W->dn[i]] = (uint16_t)((k + 1) % WD_LEN);
    return y;
}
RV_INL float wf_modes_run(wv_t *v, int i, float a)
{
    wcx_t *W = v->W;
    int k, m = W->mn[i];           /* six resonators tuned to a body, struck by A */
    float *z, y = 0.0f;
    if (m < 0)
        return 0.0f;
    z = v->mz[m];
    for (k = 0; k < WM_K; k++) {
        const float *c = W->md[m][k];
        float yk = c[0] * z[2 * k] + c[1] * z[2 * k + 1] + c[2] * a;
        z[2 * k + 1] = z[2 * k];
        z[2 * k] = yk;
        y += yk;
    }
    if (!(y > -16.0f && y < 16.0f)) {
        __builtin_memset(z, 0, sizeof v->mz[0]);
        y = 0.0f;
    }
    return y;
}

/* the transfer curves of SHAPE */
RV_INL float wf_shape_run(int c, float x)
{
    float q;
    switch (c) {
    case 0: return sat(x);                                          /* TANH */
    case 1: return clampf(x, -1.0f, 1.0f);                          /* CLIP */
    case 2: return fm_sin_turns(x * 0.25f);                         /* SINE: folds as it grows */
    case 3: q = clampf(x, -1.0f, 1.0f); return 1.5f * q - 0.5f * q * q * q;   /* CUBE */
    case 4: q = clampf(x, -1.0f, 1.0f); return 4.0f * q * q * q - 3.0f * q;   /* CHEB3: a sine in, its third harmonic out */
    case 5: return fm_floorf(clampf(x, -1.0f, 0.999f) * 2.0f + 0.5f) * 0.5f;   /* STEPS */
    default: return sat(x + 0.4f) - 0.38f;                          /* ASYM: even harmonics */
    }
}
/* CHEBY: two biquads (transposed direct form II) */
RV_INL float wf_cheby(wv_t *v, int i, float a)
{
    wcx_t *W = v->W;
    const float *c;
    float *z, y = a;
    int k;
    if (W->mn[i] < 0)
        return 0.0f;
    c = &W->md[W->mn[i]][0][0];
    z = v->mz[W->mn[i]];
    for (k = 0; k < 2; k++) {
        float b0 = c[3 * k], x = y;
        y = b0 * x + z[2 * k];
        z[2 * k] = 2.0f * b0 * x - c[3 * k + 1] * y + z[2 * k + 1];
        z[2 * k + 1] = b0 * x - c[3 * k + 2] * y;
    }
    y *= c[6];
    if (!(y > -16.0f && y < 16.0f)) {
        __builtin_memset(z, 0, 4 * sizeof(float));
        y = 0.0f;
    }
    return y;
}
/* a state-variable band pass, one sample: g the frequency's tan, R the damping; z its two states */
RV_INL float wf_bp(float x, float g, float R, float d, float *z)
{
    float hp = (x - (2.0f * R + g) * z[0] - z[1]) * d, bp = g * hp + z[0];
    z[0] = g * hp + bp;
    z[1] = g * bp + (g * bp + z[1]);
    return bp * 2.0f * R;
}
/* FORMANT: three band passes, summed by their levels */
RV_INL float wf_formant(wv_t *v, int i, float a)
{
    wcx_t *W = v->W;
    float *z, y = 0.0f;
    int k;
    if (W->mn[i] < 0)
        return 0.0f;
    z = v->mz[W->mn[i]];
    for (k = 0; k < 3; k++)
        y += wf_bp(a, W->md[W->mn[i]][k][0], W->md[W->mn[i]][k][1], W->md[W->mn[i]][k][3], z + 2 * k) * W->md[W->mn[i]][k][2];
    y *= 3.0f;
    if (!(y > -16.0f && y < 16.0f)) {
        __builtin_memset(z, 0, 6 * sizeof(float));
        y = 0.0f;
    }
    return y;
}
/* VOCODER: A (the modulator) through six bands, each band's level riding the same band of B (the carrier; with
 * nothing on B, a saw at the note) */
RV_INL float wf_vocoder(wv_t *v, wb_t *s, int i, float a, float b, int bc, float hz)
{
    wcx_t *W = v->W;
    float *z, y = 0.0f, car;
    int k;
    if (W->mn[i] < 0)
        return 0.0f;
    if (bc)
        car = b;
    else {
        s->ph += hz * RV_DT;
        s->ph = wwrap(s->ph);
        car = s->ph * 2.0f - 1.0f;
    }
    z = v->mz[W->mn[i]];
    for (k = 0; k < WM_K; k++) {
        const float *c = W->md[W->mn[i]][k];
        float m = fm_fabsf(wf_bp(a, c[0], c[2], c[3], z + 4 * k)), *e = &z[24 + k];
        *e += (m - *e) * (m > *e ? W->c1[i] : W->c2[i]);
        y += wf_bp(car, c[1], c[2], c[4], z + 4 * k + 2) * *e;
    }
    y *= 12.0f;
    if (!(y > -16.0f && y < 16.0f)) {
        __builtin_memset(z, 0, sizeof v->mz[0]);
        y = 0.0f;
    }
    return y;
}

/* ---- the blocks' work, a run of samples at a time ----
 * Each block type is a function of its own that plays block i from sample j1 to j2 - 1 of the run (its state
 * kept in registers through it); weft_voice says which, and when */
#define WR 16                                   /* samples a run */
typedef struct {
    wv_t *v;
    wcx_t *W;
    float (*vb)[WR + 1];                        /* each source over the run: [0] the sample before it, [1 + j] sample j */
    const int8_t *prod;                         /* the block that makes each source (-1: a value that holds) */
    const float *trg;                           /* TRIG over the run */
    float hz, gatev, pos, pinc;
    int t, j0, lis, sw14, lead;
    const float *PA[WF_BLOCKS], *PB[WF_BLOCKS]; /* where each block reads A and B (a sample late where so) */
    float ka[WF_BLOCKS], kb[WF_BLOCKS];
    uint8_t fl[WF_BLOCKS], ty[WF_BLOCKS], sa[WF_BLOCKS];
} wk_t;
typedef void (*wk_fn)(wk_t *K, int i, int j1, int j2);
/* (the blocks that do little a sample, and are rarely many: the clocks, the logic, the sequencers' outputs, S&H, CHAOS,
 * CRUSH, EDGE, RUNGLER, TICK; and the two seldom used, BANDS and VOCODER: from the flash, to leave the pool's room to
 * the ones most patches use) */
#define WK_FLASH __attribute__((noinline))
#define WK_HEAD \
    wv_t *v = K->v; \
    wcx_t *W = K->W; \
    wb_t *s = &v->b[i]; \
    const float *pa = K->PA[i], *pb = K->PB[i], *trg = K->trg; \
    float ka_ = K->ka[i], kb_ = K->kb[i], x1 = W->p1[i], x2 = W->p2[i], a = 0.0f, b = 0.0f, y = 0.0f; \
    float *yo = &K->vb[i + 1][1]; \
    int f = K->fl[i], ac = f & 1, bc = (f >> 1) & 1, ty = K->ty[i], j; \
    (void)v; (void)W; (void)s; (void)pa; (void)pb; (void)trg; (void)ka_; (void)kb_; (void)x1; (void)x2; \
    (void)a; (void)b; (void)y; (void)yo; (void)ac; (void)bc; (void)ty;
#define WA (pa[j] * ka_)
#define WB (pb[j] * kb_)

/* a slow LFO (CYCLE, LISSA): worked out every 8th sample, a straight line between */
RV_HOT2 static void wk_ctl(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    for (j = j1; j < j2; j++) {
        if (!((K->j0 + j) & 7)) {
            float y0, y1 = 0.0f;
            if (ty == WO_CYCLE) {
                float k = W->c2[i];
                s->ph = wwrap(s->ph + W->c1[i] * 8.0f);
                y0 = (s->ph < k ? s->ph / k : (1.0f - s->ph) / (1.0f - k)) * 2.0f - 1.0f;
            } else {
                const uint8_t *r = wf_lratio_v[WN(W->p2[i], 9)];
                s->ph = wwrap(s->ph + W->c1[i] * 8.0f);
                y0 = fm_sin_turns(s->ph * (float)r[0]);
                y1 = fm_sin_turns(s->ph * (float)r[1]);
            }
            s->z[4] = (y0 - s->z[6]) * 0.125f;   /* (z: the steps along the lines, and where they are) */
            s->z[5] = (y1 - s->z[7]) * 0.125f;
        }
        s->z[6] += s->z[4];
        s->z[7] += s->z[5];
        yo[j] = s->z[6];
        if (i == K->lis)
            K->vb[WS_LISY][1 + j] = s->z[7];
    }
}

RV_HOT2 static void wk_phase(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* a ramp at the note x RATIO; A: FM (through zero), B: reset */
        float ph = s->ph, c1 = W->c1[i];
        for (j = j1; j < j2; j++) {
            a = WA;
            ph += c1 * (1.0f + 8.0f * a);
            ph = wwrap(ph);
            if (bc && wrise(WB, &s->prevb, 0.0f))
                ph = 0.0f;
            yo[j] = ph * 2.0f - 1.0f;
        }
        s->ph = ph;
        return;
    }
}

RV_HOT2 static void wk_wave(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* reads a phase (A, else its own at the note); B: phase modulation */
        float p, k = clampf(0.5f + WC(x2) * 0.5f, 0.03f, 0.97f), ph = s->ph, inc = K->hz * RV_DT;
        for (j = j1; j < j2; j++) {
            if (ac)
                p = WA * 0.5f + 0.5f;
            else {
                ph += inc;
                ph = wwrap(ph);
                p = ph;
            }
            p += WB * 0.5f;
            p = wwrap(p);
            p = p < k ? p / k * 0.5f : 0.5f + (p - k) / (1.0f - k) * 0.5f;   /* WARP */
            yo[j] = wf_wave(p, x1);
        }
        s->ph = ph;
        return;
    }
}

RV_HOT2 static void wk_slope(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* rises while A (else the note) is up, holds, falls; B: faster or slower */
        float yv = s->y, kr = W->kr[i], kf = W->kf[i];
        for (j = j1; j < j2; j++) {
            float gg = ac ? WA : K->gatev, sc = bc ? fm_exp2f(-WB * 4.0f) : 1.0f;
            if (wrise(gg, &s->prev, 0.5f) || (!ac && trg[j] > 0.0f))
                s->st = 1;
            if (s->st == 1) {
                yv += (1.25f - yv) * clampf(kr * sc, 0.0f, 1.0f);
                if (yv >= 1.0f) {
                    yv = 1.0f;
                    s->st = 2;
                }
            } else if (s->st == 2 && gg <= 0.5f)
                s->st = 0;
            if (!s->st)
                yv -= yv * clampf(kf * sc, 0.0f, 1.0f);
            yo[j] = yv;
        }
        s->y = yv;
        return;
    }
}

RV_HOT2 static void wk_cycle(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* rises and falls on its own (SKEW); A: rate, B: reset */
        float ph = s->ph, c1 = W->c1[i], k = W->c2[i];
        for (j = j1; j < j2; j++) {
            float inc = c1 * (ac ? fm_exp2f(WA * 5.0f) : 1.0f);
            ph += inc;
            ph = wwrap(ph);
            if (bc && wrise(WB, &s->prevb, 0.0f))
                ph = 0.0f;
            yo[j] = (ph < k ? ph / k : (1.0f - ph) / (1.0f - k)) * 2.0f - 1.0f;
        }
        s->ph = ph;
        return;
    }
}

RV_HOT2 static void wk_slew(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {   /* SLEW follows A; FOLLOW follows A's size: RISE (ATTACK) up, FALL (RELEASE) down */
        float yv = s->y, kr = W->kr[i], kf = W->kf[i];
        for (j = j1; j < j2; j++) {
            float r = ty == WO_FOLLOW ? fm_fabsf(WA) : WA, sc = bc ? fm_exp2f(-WB * 4.0f) : 1.0f;
            yv += (r - yv) * clampf((r > yv ? kr : kf) * sc, 0.0f, 1.0f);
            yo[j] = yv;
        }
        s->y = yv;
        return;
    }
}

RV_HOT2 static void wk_lp(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {   /* a state-variable filter; B: the cutoff, five octaves each way */
        float R = W->c2[i] * 0.5f, s1 = s->s1, s2 = s->s2, fg = W->fg[i], fd = W->fd[i], c1 = W->c1[i];
        for (j = j1; j < j2; j++) {
            float g = fg, d = fd, hp, bp, lp;
            a = WA;
            if (bc) {
                b = WB;
                if (!((K->j0 + j) & 3)) {   /* (cutoff moved by B: worked out every fourth sample) */
                    s->hold = RV_FAR(wx_tan)(3.14159265f * clampf(c1 * fm_exp2f(b * 5.0f), 20.0f, 18000.0f) * RV_DT);
                    s->z[0] = wsvd(s->hold, R);
                }
                if (s->hold > 0.0f) {
                    g = s->hold;
                    d = s->z[0];
                }
            }
            hp = (a - (2.0f * R + g) * s1 - s2) * d;
            bp = g * hp + s1;
            s1 = g * hp + bp;
            lp = g * bp + s2;
            s2 = g * bp + lp;
            y = ty == WO_LP ? lp : ty == WO_BP ? bp * (2.0f * R) : hp;
            if (!(y > -8.0f && y < 8.0f))   /* (blown up: start over) */
                s1 = s2 = y = 0.0f;
            yo[j] = y;
        }
        s->s1 = s1;
        s->s2 = s2;
        return;
    }
}

RV_HOT2 static void wk_add(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float o = WC(x1) * 2.0f, gn = WG(x2);
        for (j = j1; j < j2; j++)
            yo[j] = (WA + WB + o) * gn;
        return;
    }
}

RV_HOT2 static void wk_mult(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float o = WC(x1) * 2.0f, gn = WG(x2);
        for (j = j1; j < j2; j++)
            yo[j] = WA * ((bc ? WB : 1.0f) + o) * gn;
        return;
    }
}

RV_HOT2 static void wk_mix(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float gn = WG(x2);
        for (j = j1; j < j2; j++) {
            a = WA;
            yo[j] = (a + (WB - a) * x1) * gn;
        }
        return;
    }
}

RV_HOT2 static void wk_min(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float o = WC(x1) * 2.0f, gn = WG(x2);
        for (j = j1; j < j2; j++)
            yo[j] = (ty == WO_MIN ? fm_minf(WA, WB + o) : fm_maxf(WA, WB + o)) * gn;
        return;
    }
}

RV_HOT2 static void wk_fold(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* driven round a sine: folds back as it grows */
        float dr = 1.0f + x1 * 15.0f, o = WC(x2);
        for (j = j1; j < j2; j++)
            yo[j] = fm_sin_turns((WA * dr * (1.0f + (bc ? WB : 0.0f)) + o) * 0.25f);
        return;
    }
}

WK_FLASH static void wk_comp(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float thr = WC(x2);
        for (j = j1; j < j2; j++) {
            int ta, tb, r;
            a = WA;
            b = WB;
            ta = a > thr;
            tb = b > thr;
            if (ty == WO_COMP)
                r = a > b + thr;
            else if (ty == WO_AND)
                r = ta && tb;
            else if (ty == WO_OR)
                r = ta || tb;
            else if (ty == WO_XOR)
                r = ta != tb;
            else {              /* FLIP: over on each rise of A, B sets it back */
                if (wrise(a, &s->prev, thr))
                    s->st ^= 1;
                if (bc && wrise(b, &s->prevb, thr))
                    s->st = 0;
                r = s->st;
            }
            yo[j] = (r ? 1.0f : -1.0f) * x1;
        }
        return;
    }
}

RV_HOT2 static void wk_delay(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* up to 11 ms; TIME at 0: the note's period. B: the time, two octaves each way */
        int16_t *mm;
        float c1 = W->c1[i], c2 = W->c2[i];
        int k;
        if (W->dn[i] < 0) {
            for (j = j1; j < j2; j++)
                yo[j] = 0.0f;
            return;
        }
        mm = wdly[v - wv][W->dn[i]];
        k = v->dw[W->dn[i]];
        for (j = j1; j < j2; j++) {
            float d = clampf(c1 * (bc ? fm_exp2f(WB * 2.0f) : 1.0f), 2.0f, (float)(WD_LEN - 2)), fr, r0, r1, wr;
            int i0;
            a = WA;
            fr = (float)k - d;
            if (fr < 0.0f)
                fr += (float)WD_LEN;
            i0 = (int)fr;
            fr -= (float)i0;
            r0 = (float)mm[i0];
            r1 = (float)mm[(i0 + 1) & (WD_LEN - 1)];
            y = (r0 + (r1 - r0) * fr) * (1.0f / 16384.0f);
            wr = sat(a + y * c2);
            mm[k] = (int16_t)(wr * 16384.0f);
            k = (k + 1) & (WD_LEN - 1);
            yo[j] = y;
        }
        v->dw[W->dn[i]] = (uint16_t)k;
        return;
    }
}

WK_FLASH static void wk_bands(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* four bands at the note's 1st-4th harmonics (SPREAD bends them); B: scan */
        float R = W->c2[i];
        for (j = j1; j < j2; j++) {
            int k;
            float sum = 0.0f, pos2;
            a = WA;
            b = WB;
            pos2 = (b * 0.5f + 0.5f) * 3.0f;
            for (k = 0; k < 4; k++) {
                float gk = W->bg[i][k], hp, bp, wgt;
                hp = (a - (2.0f * R + gk) * s->z[2 * k] - s->z[2 * k + 1]) * W->bd[i][k];
                bp = gk * hp + s->z[2 * k];
                s->z[2 * k] = gk * hp + bp;
                s->z[2 * k + 1] = gk * bp + (gk * bp + s->z[2 * k + 1]);
                wgt = bc ? fm_maxf(0.0f, 1.0f - fm_fabsf((float)k - pos2)) : 0.5f;
                sum += bp * 2.0f * R * wgt;
            }
            y = sum * 3.0f;
            if (!(y > -8.0f && y < 8.0f)) {
                __builtin_memset(s->z, 0, sizeof s->z);
                y = 0.0f;
            }
            yo[j] = y;
        }
        return;
    }
}

WK_FLASH static void wk_hold(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* takes A on each rise of B (else each note); STEPS: rounds it; GLIDE */
        float kh = x2 > 0.0f ? wk(wtime(x2 * 0.8f)) : 1.0f, lv = 2.0f + x1 * 30.0f;
        for (j = j1; j < j2; j++) {
            if (bc ? wrise(WB, &s->prevb, 0.0f) : trg[j] > 0.0f) {
                float qv = WA;
                if (x1 > 0.0f)
                    qv = fm_floorf((qv * 0.5f + 0.5f) * lv + 0.5f) / lv * 2.0f - 1.0f;
                s->hold = qv;
            }
            s->y += (s->hold - s->y) * kh;
            yo[j] = s->y;
        }
        return;
    }
}

RV_HOT2 static void wk_dust(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* random particles in clumps; A: density; B (else each note): a burst */
        float c1 = W->c1[i], tau = 0.04f, dec = 1.0f - RV_DT / tau;
        for (j = j1; j < j2; j++) {
            float rate = c1 * (ac ? fm_exp2f(WA * 6.0f) : 1.0f), br = x2 * 0.95f / (rate * tau + 1e-3f);
            y = 0.0f;
            s->s1 *= dec;
            if (bc ? wrise(WB, &s->prevb, 0.0f) : trg[j] > 0.0f)
                s->s1 += 6.0f / (rate * tau + 1e-3f) * (0.2f + x2);
            if (s->s1 > 400.0f)
                s->s1 = 400.0f;
            if (xs_uni(&s->rng) < rate * (1.0f + s->s1) * RV_DT) {
                y = 0.25f + 0.75f * xs_uni(&s->rng);
                s->s1 += br;
            }
            yo[j] = y;
        }
        return;
    }
}

WK_FLASH static void wk_tick(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* a click each DIV of the tempo, CHANCE of it sounding; A: gate; B: late */
        float L = W->c1[i];
        for (j = j1; j < j2; j++) {
            float at = fm_floorf((K->pos + K->pinc * (float)(K->j0 + j)) / L);
            y = 0.0f;
            a = WA;
            if (at != s->hold) {
                s->hold = at;
                if ((!ac || a > 0.0f) && xs_uni(&s->rng) * 0.999f < x2)
                    s->s1 = bc ? fm_fabsf(WB) * xs_uni(&s->rng) * L * 0.5f * clk_period * (float)RV_FS + 1.0f : 1.0f;
            }
            if (s->s1 > 0.0f && (s->s1 -= 1.0f) <= 0.0f)
                y = 1.0f;
            yo[j] = y;
        }
        return;
    }
}

RV_HOT2 static void wk_ring(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* one resonator, struck by A */
        float a1 = W->kr[i], a2 = W->kf[i], gn = W->fg[i], s1 = s->s1, s2 = s->s2;
        for (j = j1; j < j2; j++) {
            y = a1 * s1 + a2 * s2 + gn * WA;
            s2 = s1;
            s1 = y;
            if (!(y > -16.0f && y < 16.0f))
                s1 = s2 = y = 0.0f;
            yo[j] = y;
        }
        s->s1 = s1;
        s->s2 = s2;
        return;
    }
}

RV_HOT2 static void wk_skin(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    int mi = W->mn[i];
    if (mi >= 0 && j2 - j1 > 1) {       /* a run: each mode over all of it in turn (its state held throughout), the
                                         * modes summed in the same order as wf_modes_run; if it blows up anywhere,
                                         * the run again a sample at a time, as wf_modes_run does */
        float *z = v->mz[mi], z0[2 * WM_K], acc[WR], ain[WR];
        int k;
        for (k = 0; k < 2 * WM_K; k++)
            z0[k] = z[k];
        for (j = j1; j < j2; j++) {
            acc[j] = 0.0f;
            ain[j] = WA;
        }
        for (k = 0; k < WM_K; k++) {
            const float *c = W->md[mi][k];
            float c0 = c[0], c1 = c[1], c2 = c[2], y0 = z[2 * k], y1 = z[2 * k + 1];
            for (j = j1; j < j2; j++) {
                float yk = c0 * y0 + c1 * y1 + c2 * ain[j];
                y1 = y0;
                y0 = yk;
                acc[j] += yk;
            }
            z[2 * k] = y0;
            z[2 * k + 1] = y1;
        }
        for (j = j1; j < j2; j++)
            if (!(acc[j] > -16.0f && acc[j] < 16.0f))
                break;
        if (j == j2) {
            for (j = j1; j < j2; j++)
                yo[j] = acc[j];
            return;
        }
        for (k = 0; k < 2 * WM_K; k++)
            z[k] = z0[k];
    }
    for (j = j1; j < j2; j++)
        yo[j] = wf_modes_run(v, i, WA);
}

RV_HOT2 static void wk_sat(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {              /* A driven into a soft clip; B: more drive */
        float dr = 1.0f + x1 * 15.0f, o = WC(x2);
        for (j = j1; j < j2; j++)
            yo[j] = sat((WA * dr * (bc ? fm_exp2f(WB * 2.0f) : 1.0f) + o) * 0.8f);
        return;
    }
}

RV_HOT2 static void wk_abs(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float o = WC(x1) * 2.0f, gn = WG(x2);
        for (j = j1; j < j2; j++)
            yo[j] = fm_fabsf(WA + WB + o) * gn;
        return;
    }
}

RV_HOT2 static void wk_diffuse(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {   /* four allpasses in a row, each in a quarter of the memory; B: their size (as wf_diffuse, the sizes worked
         * out once while B doesn't move them) */
        static const float base[4] = {0.43f, 0.61f, 0.79f, 1.0f};
        int16_t *m;
        int k, wpos, L[4];
        float g = W->c2[i], c1 = W->c1[i];
        if (W->dn[i] < 0) {
            for (j = j1; j < j2; j++)
                yo[j] = 0.0f;
            return;
        }
        m = wdly[v - wv][W->dn[i]];
        wpos = v->dw[W->dn[i]] & 127;
        if (!bc)
            for (k = 0; k < 4; k++)
                L[k] = clampi((int)(base[k] * 125.0f * (c1 * 1.0f)), 1, 127);
        for (j = j1; j < j2; j++) {
            float x = WA;
            if (bc) {
                float sz = c1 * fm_exp2f(WB);
                for (k = 0; k < 4; k++)
                    L[k] = clampi((int)(base[k] * 125.0f * sz), 1, 127);
            }
            for (k = 0; k < 4; k++) {
                float d = (float)m[k * 128 + ((wpos - L[k]) & 127)] * (1.0f / 8192.0f), o = d - g * x;
                m[k * 128 + wpos] = (int16_t)(clampf(x + g * o, -3.9f, 3.9f) * 8192.0f);
                x = o;
            }
            wpos = (wpos + 1) & 127;
            yo[j] = x;
        }
        v->dw[W->dn[i]] = (uint16_t)wpos;
    }
}

WK_FLASH static void wk_oclk(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* A's rises, every DIV-th: the WEFT clock; B: back to step 1 */
        float thr = WC(x2);
        int dv = WN(x1, 16);
        for (j = j1; j < j2; j++) {
            y = a = WA;
            if (wrise(a, &s->prev, thr) && ++s->st > dv) {
                s->st = 1;
                if (K->t == wf_clk_owner)
                    weft_clk_fired = 1;
            }
            if (bc && wrise(WB, &s->prevb, 0.0f) && K->t == wf_clk_owner)
                weft_clk_reset = 1;
            yo[j] = y;
        }
        return;
    }
}

WK_FLASH static void wk_omod(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* A (x B), smoothed: the matrix's WEFT source */
        float c1 = W->c1[i];
        int rg = WN(x2, 2);
        for (j = j1; j < j2; j++) {
            float x = WA * (bc ? WB : 1.0f);
            s->y += (x - s->y) * c1;
            y = s->y;
            if (K->t == wf_mod_owner)
                weft_mod_v = rg ? clampf(y * 0.5f + 0.5f, 0.0f, 1.0f) : clampf(y, -1.0f, 1.0f);
            yo[j] = y;
        }
        return;
    }
}

RV_HOT2 static void wk_sw14(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        const float *p4[4];
        float in4[4];
        int k, sai = K->sa[i];
        for (k = 0; k < 4; k++) {
            int src = sai + k;
            p4[k] = src < WS_N ? &K->vb[src][1 - (K->prod[src] >= 0 && K->prod[src] >= i)] : &K->vb[0][1];
        }
        for (j = j1; j < j2; j++) {
            a = WA;
            for (k = 0; k < 4; k++)
                in4[k] = p4[k][j];
            y = wf_switch(v, s, i, ty, a, WB, bc, trg[j], x1, ty == WO_SW41 && sai ? in4 : 0, ka_);
            if (ty == WO_SW14 && i == K->sw14)
                for (k = 0; k < 4; k++)
                    K->vb[WS_SW1 + k][1 + j] = a * s->z[k];
            yo[j] = y;
        }
        return;
    }
}

RV_HOT2 static void wk_string(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
        for (j = j1; j < j2; j++)
            yo[j] = wf_string(v, s, i, WA);
        return;
}

WK_FLASH static void wk_chaos(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* the logistic map, stepped by A's rises (else at RATE); B: more or less */
        float c1 = W->c1[i], c2 = W->c2[i];
        for (j = j1; j < j2; j++) {
            int stp;
            a = WA;
            if (ac)
                stp = wrise(a, &s->prev, 0.0f);
            else {
                s->ph += c1;
                stp = s->ph >= 1.0f;
                if (stp)
                    s->ph = wwrap(s->ph);
            }
            if (!(s->y > 0.0001f && s->y < 0.9999f))
                s->y = 0.1f + 0.8f * xs_uni(&s->rng);
            if (stp) {
                float r = clampf(c2 + (bc ? WB * 0.3f : 0.0f), 2.8f, 4.0f);
                s->y = r * s->y * (1.0f - s->y);
            }
            yo[j] = s->y * 2.0f - 1.0f;
        }
        return;
    }
}

WK_FLASH static void wk_crush(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* A held for RATE samples at a time, in BITS' levels; B: the rate */
        float c1 = W->c1[i], c2 = W->c2[i];
        for (j = j1; j < j2; j++) {
            float hl = c2 * (bc ? fm_exp2f(WB * 3.0f) : 1.0f);
            if ((s->ph += 1.0f) >= hl) {
                s->ph = 0.0f;
                s->hold = fm_floorf(WA * c1 + 0.5f) / c1;
            }
            yo[j] = s->hold;
        }
        return;
    }
}

WK_FLASH static void wk_edge(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* a click at each rise of A */
        float o = WC(x1);
        for (j = j1; j < j2; j++)
            yo[j] = wrise(WA, &s->prev, o + (bc ? WB : 0.0f)) ? x2 : 0.0f;
        return;
    }
}

RV_HOT2 static void wk_shape(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        float c1 = W->c1[i];
        int cv = WN(x1, 7);
        for (j = j1; j < j2; j++)
            yo[j] = wf_shape_run(cv, WA * c1 * (bc ? fm_exp2f(WB * 2.0f) : 1.0f));
        return;
    }
}

RV_HOT2 static void wk_cheby(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    int mi = W->mn[i];
    if (mi >= 0 && j2 - j1 > 1) {       /* a run: the first biquad over all of it, then the second (as wf_cheby, a sample
                                         * at a time again if it blows up anywhere) */
        const float *c = &W->md[mi][0][0];
        float *z = v->mz[mi], z0[4], buf[WR];
        int k;
        for (k = 0; k < 4; k++)
            z0[k] = z[k];
        for (j = j1; j < j2; j++)
            buf[j] = WA;
        for (k = 0; k < 2; k++) {
            float b0 = c[3 * k], a1 = c[3 * k + 1], a2 = c[3 * k + 2], s0 = z[2 * k], s1 = z[2 * k + 1];
            for (j = j1; j < j2; j++) {
                float x = buf[j], yv = b0 * x + s0;
                s0 = 2.0f * b0 * x - a1 * yv + s1;
                s1 = b0 * x - a2 * yv;
                buf[j] = yv;
            }
            z[2 * k] = s0;
            z[2 * k + 1] = s1;
        }
        for (j = j1; j < j2; j++) {
            buf[j] *= c[6];
            if (!(buf[j] > -16.0f && buf[j] < 16.0f))
                break;
        }
        if (j == j2) {
            for (j = j1; j < j2; j++)
                yo[j] = buf[j];
            return;
        }
        for (k = 0; k < 4; k++)
            z[k] = z0[k];
    }
    for (j = j1; j < j2; j++)
        yo[j] = wf_cheby(v, i, WA);
}

RV_HOT2 static void wk_formant(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    int mi = W->mn[i];
    if (mi >= 0 && j2 - j1 > 1) {       /* a run: each band pass over all of it, summed in wf_formant's order (a sample at a
                                         * time again if it blows up anywhere) */
        float *z = v->mz[mi], z0[6], acc[WR], ain[WR];
        int k;
        for (k = 0; k < 6; k++)
            z0[k] = z[k];
        for (j = j1; j < j2; j++) {
            acc[j] = 0.0f;
            ain[j] = WA;
        }
        for (k = 0; k < 3; k++) {
            const float *c = W->md[mi][k];
            float g = c[0], R = c[1], am = c[2], d = c[3];
            for (j = j1; j < j2; j++)
                acc[j] += wf_bp(ain[j], g, R, d, z + 2 * k) * am;
        }
        for (j = j1; j < j2; j++) {
            acc[j] *= 3.0f;
            if (!(acc[j] > -16.0f && acc[j] < 16.0f))
                break;
        }
        if (j == j2) {
            for (j = j1; j < j2; j++)
                yo[j] = acc[j];
            return;
        }
        for (k = 0; k < 6; k++)
            z[k] = z0[k];
    }
    for (j = j1; j < j2; j++)
        yo[j] = wf_formant(v, i, WA);
}

WK_FLASH static void wk_vocoder(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
        for (j = j1; j < j2; j++)
            yo[j] = wf_vocoder(v, s, i, WA, WB, bc, K->hz);
        return;
}

RV_HOT2 static void wk_rmod(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* A x B (with nothing on B, a sine at the note x FREQ); MIX: A to the product */
        float c1 = W->c1[i];
        for (j = j1; j < j2; j++) {
            float car;
            a = WA;
            if (bc)
                car = WB;
            else {
                s->ph += c1;
                s->ph = wwrap(s->ph);
                car = fm_sin_turns(s->ph);
            }
            yo[j] = a + (a * car - a) * x2;
        }
        return;
    }
}

RV_HOT2 static void wk_shift(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {
        /* (as wf_shift was: the carrier's turn worked out once while B doesn't move it) */
        float c1 = W->c1[i], th = (c1 + 0.0f) * 6.2831853f, t2 = th * th, cd = 1.0f - t2 * 0.5f, sd = th - th * t2 * (1.0f / 6.0f);
        float s1 = s->s1, s2 = s->s2, *z = s->z;
        int k;
        for (j = j1; j < j2; j++) {
            float p, q, c, sn, g;
            p = q = WA;
            if (bc) {
                float inc = c1 + WB * 1000.0f * RV_DT;
                th = inc * 6.2831853f;
                t2 = th * th;
                cd = 1.0f - t2 * 0.5f;
                sd = th - th * t2 * (1.0f / 6.0f);
            }
            for (k = 0; k < 4; k++) {           /* (transposed: y = c x + z, z = x - c y) */
                float y1 = wf_hil[k] * p + z[k], y2 = wf_hil[4 + k] * q + z[4 + k];
                z[k] = p - wf_hil[k] * y1;
                z[4 + k] = q - wf_hil[4 + k] * y2;
                p = y1;
                q = y2;
            }
            if (!(p > -64.0f && p < 64.0f && q > -64.0f && q < 64.0f)) {
                for (k = 0; k < 8; k++)
                    z[k] = 0.0f;
                yo[j] = 0.0f;
                continue;
            }
            c = s1 * cd - s2 * sd;              /* (the carrier turned: wf_rot) */
            sn = s1 * sd + s2 * cd;
            g = 1.5f - 0.5f * (c * c + sn * sn);
            if (!(g > 0.5f && g < 1.5f))
                c = 1.0f, sn = 0.0f, g = 1.0f;
            s1 = c * g;
            s2 = sn * g;
            yo[j] = x2 * (p * s1 - q * s2) + (1.0f - x2) * (p * s1 + q * s2);
        }
        s->s1 = s1;
        s->s2 = s2;
        return;
    }
}

RV_HOT2 static void wk_diode(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* a diode (PAIR 0: half-wave), or a pair (127: even); B: drive */
        float c1 = W->c1[i];
        for (j = j1; j < j2; j++) {
            float u = WA * c1 * (bc ? fm_exp2f(WB * 2.0f) : 1.0f), hp, r = fm_sqrtf(u * u + 0.04f);   /* (once: the same both times) */
            y = 0.5f * (u + r) - x2 * 0.5f * (r - u) - 0.1f * (1.0f - x2);
            y = sat(y * 0.7f);
            hp = y - s->s1;
            s->s1 += hp * 0.0015f;
            yo[j] = hp * 1.3f;
        }
        return;
    }
}

RV_HOT2 static void wk_noise(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* white noise held RATE samples at a time, coloured; A: the rate, B: the colour */
        float c2 = W->c2[i];
        for (j = j1; j < j2; j++) {
            float col = clampf(x1 + (bc ? WB * 0.5f : 0.0f), 0.0f, 1.0f);
            if ((s->ph += 1.0f) >= c2 * (ac ? fm_exp2f(-WA * 4.0f) : 1.0f)) {
                s->ph = 0.0f;
                s->hold = xs_uni(&s->rng) * 2.0f - 1.0f;
            }
            if (col < 0.5f) {
                float k = col * 2.0f;
                s->y += (s->hold - s->y) * fm_maxf(k * k, 0.002f);
                y = s->y * (1.0f + (1.0f - k) * 4.0f);
            } else {
                float t2 = (col - 0.5f) * 2.0f;
                s->y += (s->hold - s->y) * 0.15f;
                y = s->hold + ((s->hold - s->y) * 1.5f - s->hold) * t2;
            }
            yo[j] = y;
        }
        return;
    }
}

WK_FLASH static void wk_rungler(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {          /* a shift register stepped by A's rises (else each note) */
        int L = 3 + WN(x2, 6);
        for (j = j1; j < j2; j++) {
            if (ac ? wrise(WA, &s->prev, 0.0f) : trg[j] > 0.0f) {
                int last = (s->st >> (L - 1)) & 1, nb = bc ? WB > 0.0f : xs_uni(&s->rng) < 0.5f;
                s->st = (uint8_t)(((s->st << 1) | (xs_uni(&s->rng) * 0.999f < x1 ? last : nb)) & ((1 << L) - 1));
            }
            yo[j] = (float)((s->st >> (L - 3)) & 7) * (2.0f / 7.0f) - 1.0f;
        }
        return;
    }
}

RV_HOT2 static void wk_aout(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {             /* A into the track's sound, at LEVEL (x B), through a tilt */
        float tn = WC(x2);
        for (j = j1; j < j2; j++) {
            float qv = WA * x1 * 1.27f * (bc ? WB : 1.0f);
            if (tn < -0.02f) {
                s->y += (qv - s->y) * fm_maxf((1.0f + tn) * (1.0f + tn), 0.002f);
                qv = s->y;
            } else if (tn > 0.02f) {
                s->y += (qv - s->y) * (1.0f - tn) * 0.5f;
                qv = qv + (qv - s->y - qv) * tn;
            }
            yo[j] = qv;
        }
        return;
    }
}

WK_FLASH static void wk_seq(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {              /* A's rises do MODE to TRACK's sequencer; B: where (JUMP), which (PATTERN) */
        int tr = WN(x1, 8), md = WN(x2, 5);
        for (j = j1; j < j2; j++) {
            y = a = WA;
            yo[j] = y;
            if (!K->lead)
                continue;       /* (one voice of the track conducts) */
            b = WB;
            if (md == WSQ_GATE) {
                if (a > 0.0f)
                    weft_gate_open |= (uint8_t)(1u << tr);
            } else if (wrise(a, &s->prev, 0.0f)) {
                if (md == WSQ_STEP)
                    wsq_steps[tr]++;
                else if (md == WSQ_JUMP)
                    wsq_jump[tr] = (uint8_t)(1 + (int)(clampf(b, 0.0f, 1.0f) * 254.0f));
                else if (md == WSQ_RESTART)
                    wsq_restart |= (uint8_t)(1u << tr);
                else
                    wsq_pat[tr] = (int8_t)(bc ? b > 0.0f : 2);
            }
        }
        return;
    }
}

RV_HOT2 static void wk_lissa(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
    {            /* X and Y sines at RATIO of one RATE: X out, Y the source LIS Y */
        const uint8_t *r = wf_lratio_v[WN(x2, 9)];
        float c1 = W->c1[i];
        for (j = j1; j < j2; j++) {
            s->ph += c1 * (ac ? fm_exp2f(WA * 5.0f) : 1.0f);
            s->ph = wwrap(s->ph);
            yo[j] = fm_sin_turns(s->ph * (float)r[0]);
            if (i == K->lis)
                K->vb[WS_LISY][1 + j] = fm_sin_turns(s->ph * (float)r[1] + (bc ? WB * 0.5f : 0.0f));
        }
        return;
    }
}

RV_HOT2 static void wk_none(wk_t *K, int i, int j1, int j2)
{
    WK_HEAD
        for (j = j1; j < j2; j++)
            yo[j] = 0.0f;
        return;
}

static const wk_fn wk_tab[WO_N] = {
    [WO_PHASE] = wk_phase,
    [WO_WAVE] = wk_wave,
    [WO_SLOPE] = wk_slope,
    [WO_CYCLE] = wk_cycle,
    [WO_SLEW] = wk_slew,
    [WO_FOLLOW] = wk_slew,
    [WO_LP] = wk_lp,
    [WO_BP] = wk_lp,
    [WO_HP] = wk_lp,
    [WO_ADD] = wk_add,
    [WO_MULT] = wk_mult,
    [WO_MIX] = wk_mix,
    [WO_MIN] = wk_min,
    [WO_MAX] = wk_min,
    [WO_FOLD] = wk_fold,
    [WO_COMP] = wk_comp,
    [WO_AND] = wk_comp,
    [WO_OR] = wk_comp,
    [WO_XOR] = wk_comp,
    [WO_FLIP] = wk_comp,
    [WO_DELAY] = wk_delay,
    [WO_BANDS] = wk_bands,
    [WO_HOLD] = wk_hold,
    [WO_DUST] = wk_dust,
    [WO_TICK] = wk_tick,
    [WO_RING] = wk_ring,
    [WO_SKIN] = wk_skin,
    [WO_BAR] = wk_skin,
    [WO_PLATE] = wk_skin,
    [WO_BELL] = wk_skin,
    [WO_HARMS] = wk_skin,
    [WO_FIXED] = wk_skin,
    [WO_SAT] = wk_sat,
    [WO_ABS] = wk_abs,
    [WO_DIFFUSE] = wk_diffuse,
    [WO_OCLK] = wk_oclk,
    [WO_OMOD] = wk_omod,
    [WO_SW14] = wk_sw14,
    [WO_SW41] = wk_sw14,
    [WO_STRING] = wk_string,
    [WO_CHAOS] = wk_chaos,
    [WO_CRUSH] = wk_crush,
    [WO_EDGE] = wk_edge,
    [WO_SHAPE] = wk_shape,
    [WO_CHEBY] = wk_cheby,
    [WO_FORMANT] = wk_formant,
    [WO_VOCODER] = wk_vocoder,
    [WO_RMOD] = wk_rmod,
    [WO_SHIFT] = wk_shift,
    [WO_DIODE] = wk_diode,
    [WO_NOISE] = wk_noise,
    [WO_RUNGLER] = wk_rungler,
    [WO_AOUT] = wk_aout,
    [WO_SEQ] = wk_seq,
    [WO_LISSA] = wk_lissa,
};
#undef WA
#undef WB

/* a WEFT voice's step, worked out before its samples: each block's settings, the blocks it needs, what makes each
 * source, the order the blocks run in and which run sample by sample (in the flash: once a step) */
typedef struct {
    int outb, naud, ng, na, nmade, noise_on;
    unsigned ctl;
    uint16_t comp;
    int8_t prod[WS_N];
    uint8_t mv[WF_BLOCKS], fl[WF_BLOCKS], ol[WF_BLOCKS], gst[WF_BLOCKS + 1], al[WF_BLOCKS], made[WS_N];
    const float *PBo[WF_BLOCKS];
    float kbo[WF_BLOCKS];
    wk_fn fn[WF_BLOCKS];
} wplan_t;

static __attribute__((noinline)) void weft_plan(wv_t *v, int t, const float *eff, float hz, int scope, wplan_t *P, wk_t *K, float (*vb)[WR + 1])
{
    wcx_t *W = v->W;
    const rv_weft *w = &rv.weft[t];
    float lfo1 = lfo[0].value, lfo2 = lfo[1].value, wheel = midi_wheel, pitch = ((float)v->note + eff[TP_PITCH] - 60.0f) * (1.0f / 48.0f);
    float ka[WF_BLOCKS], kb[WF_BLOCKS];
    float *sv = v->sv;
    uint8_t sa[WF_BLOCKS], sb[WF_BLOCKS];
    int i, outb = wf_outb(w), nd = 0, nm = 0, sw14 = -1, lis = -1, noise_on = 0, naud = 0, lead = (int)(v - wv) == wf_lead[t];
    unsigned live = 0, ctl = 0;
    float pos = weft_pos16, pinc = RV_DT / (clk_period > 0.001f ? clk_period : 0.001f);
    unsigned bm = w->bmove;
    /* what each block needs per sample, worked out once per step */
    W->hz = hz;
    sv[0] = 0.0f;
    sv[WS_PITCH] = pitch;
    sv[WS_VEL] = v->vel;
    sv[WS_LFO1] = lfo1;
    sv[WS_LFO2] = lfo2;
    sv[WS_WHEEL] = wheel;
    sv[WS_ONE] = 1.0f;
    sv[WS_TAP] = rv_live.vtap;                  /* (VIOLENCE) */
    sv[WS_HELD] = rv_live.vheld;
    sv[WS_TURN] = rv_live.vturn;
    for (i = 0; i < WF_BLOCKS; i++) {
        int ty = w->type[i];
        if (!v->b[i].rng)                       /* (a block's dice: seeded from the voice's, once) */
            v->b[i].rng = in_hash32(v->rng + (uint32_t)i * 0x9E3779B9u) | 1u;
        W->dn[i] = W->mn[i] = -1;
        naud += ty == WO_AOUT;
        sa[i] = (uint8_t)(w->a[i] < WS_N ? w->a[i] : 0);
        sb[i] = (uint8_t)(w->b[i] < WS_N ? w->b[i] : 0);
        ka[i] = (float)w->amta[i] * 0.01f;
        kb[i] = (float)w->amtb[i] * 0.01f;
        if (ty == WO_SW14 && sw14 < 0)
            sw14 = i;                           /* (the first one feeds SW 1-4) */
        if ((ty == WO_DELAY || ty == WO_DIFFUSE || ty == WO_STRING) && nd < WD_N)
            W->dn[i] = (int8_t)nd++;
        if (WO_SLOT(ty) && nm < WM_N)
            W->mn[i] = (int8_t)nm++;
        if (ty == WO_LISSA && lis < 0)
            lis = i;                            /* (the first one feeds LIS Y) */
        if (ty)
            RV_FAR(wf_setup)(W, i, ty, clampf(eff[wf_sp(2 * i)], 0.0f, 127.0f) * (1.0f / 127.0f), clampf(eff[wf_sp(2 * i + 1)], 0.0f, 127.0f) * (1.0f / 127.0f), 0.0f, 1);
    }
    if (sw14 < 0)
        sv[WS_SW1] = sv[WS_SW2] = sv[WS_SW3] = sv[WS_SW4] = 0.0f;
    if (lis < 0)
        sv[WS_LISY] = 0.0f;
    /* the blocks the voice needs: the output and the output blocks, and whatever feeds them (back to front, a few
     * passes); the rest rest. While the scopes show, all of them play, so a block being patched can be seen */
    if (scope)
        live = (1u << WF_BLOCKS) - 1u;
    else {
        int pass, k;
        if (outb >= 0)
            live |= 1u << outb;
        for (i = 0; i < WF_BLOCKS; i++)
            if (WO_ISOUT(w->type[i]))
                live |= 1u << i;
        for (pass = 0; pass < 4; pass++)
            for (i = WF_BLOCKS - 1; i >= 0; i--) {
                int src[5], ns = 0;
                if (!((live >> i) & 1u) || !w->type[i])
                    continue;
                src[ns++] = sa[i];
                src[ns++] = sb[i];
                if (w->type[i] == WO_SW41)       /* (it reads A's block and the three after it) */
                    for (k = 1; k < 4; k++)
                        src[ns++] = sa[i] ? sa[i] + k : 0;
                for (k = 0; k < ns; k++) {
                    int q = src[k];
                    if (q >= 1 && q <= WF_BLOCKS)
                        live |= 1u << (q - 1);
                    else if (q >= WS_SW1 && q <= WS_SW4 && sw14 >= 0)
                        live |= 1u << sw14;
                    else if (q == WS_LISY && lis >= 0)
                        live |= 1u << lis;
                }
            }
    }
    for (i = 0; i < WF_BLOCKS; i++) {          /* does anything heard read the noise; which slow LFOs can run at an eighth of the rate */
        int ty = w->type[i];
        if (!((live >> i) & 1u))
            continue;
        noise_on |= sa[i] == WS_NOISE || sb[i] == WS_NOISE;
        if (!sa[i] && !sb[i] && !((bm >> i) & 1u) &&
            ((ty == WO_CYCLE && W->c1[i] < 50.0f * RV_DT) || (ty == WO_LISSA && W->c1[i] < 30.0f * RV_DT)))
            ctl |= 1u << i;
    }
    /* ---- the blocks a run of WR samples at a time ----
     * A block reads a source as it was this sample if what makes it comes before the block, as it was a sample
     * ago if it comes after it (or is the block itself), as ever. Each source's run is kept in vb: [0] the sample
     * before the run, [1 + j] sample j. A block whose inputs are all ready (made earlier this run, or a sample
     * late and made in a group before it) runs its whole run in one go; blocks in a loop of each other through
     * the later-block (a sample late) cords run sample by sample, in their order, as before. So the sound is the
     * same, sample for sample; the blocks just don't change places each sample */
    {
        int8_t *prod = P->prod;
        uint16_t pred[WF_BLOCKS], scc[WF_BLOCKS], reach[WF_BLOCKS], comp = 0;
        uint8_t *mv = P->mv, *fl = P->fl, *ol = P->ol, *gst = P->gst, *al = P->al, *made = P->made;
        uint32_t need = 0, back = 0;
        const float **PBo = P->PBo;
        float *kbo = P->kbo;
        wk_fn *fn = P->fn;
        int ng = 0, na = 0, nmade = 0, si, q;
        float gatev = v->gate ? 1.0f : 0.0f;
        /* who makes what */
        for (si = 0; si < WS_N; si++)
            prod[si] = -1;
        for (i = 0; i < WF_BLOCKS; i++)
            if (w->type[i] && ((live >> i) & 1u)) {
                comp |= (uint16_t)(1u << i);
                prod[i + 1] = (int8_t)i;
                if (w->type[i] == WO_AOUT)
                    al[na++] = (uint8_t)i;
            }
        if (sw14 >= 0 && ((comp >> sw14) & 1u))
            prod[WS_SW1] = prod[WS_SW2] = prod[WS_SW3] = prod[WS_SW4] = (int8_t)sw14;
        if (lis >= 0 && ((comp >> lis) & 1u))
            prod[WS_LISY] = (int8_t)lis;
        /* each block's inputs and what makes them; a block moving its settings each 8 samples (B MOVES, a
         * resonator's PITCH) runs 8 at a time */
        for (i = 0; i < WF_BLOCKS; i++) {
            int ty = w->type[i], src[6], ns = 0, k;
            pred[i] = 0;
            if (!((comp >> i) & 1u))
                continue;
            if (!((ctl >> i) & 1u)) {
                src[ns++] = sa[i];
                src[ns++] = sb[i];
                if (ty == WO_SW41 && sa[i])
                    for (k = 1; k < 4; k++)
                        if (sa[i] + k < WS_N)
                            src[ns++] = sa[i] + k;
            }
            for (k = 0; k < ns; k++) {
                need |= 1u << src[k];
                if (prod[src[k]] >= 0 && prod[src[k]] != i)
                    pred[i] |= (uint16_t)(1u << prod[src[k]]);
            }
            back |= pred[i] >> i;               /* (a cord from a later block: maybe a loop) */
            /* fl: 1 A in, 2 B in (as the block sees it), 4 a slow LFO, 8 B MOVES, 16 B on a resonator's pitch, 32 B MOVES
             * with a B */
            fl[i] = (uint8_t)((sa[i] != 0) | (sb[i] != 0 && !((bm >> i) & 1u)) << 1 | ((ctl >> i) & 1u) << 2 | ((bm >> i) & 1u) << 3 |
                              (sb[i] && !((bm >> i) & 1u) && (ty == WO_RING || WO_MODES(ty) || ty == WO_STRING || ty == WO_CHEBY || ty == WO_FORMANT)) << 4 |
                              (sb[i] != 0 && ((bm >> i) & 1u)) << 5);
            mv[i] = (uint8_t)((fl[i] & 16u) || (fl[i] & 40u) == 40u);
            K->PA[i] = &vb[sa[i]][1 - (prod[sa[i]] >= 0 && prod[sa[i]] >= i)];   /* (a sample late: made here, after it or by it) */
            PBo[i] = &vb[sb[i]][1 - (prod[sb[i]] >= 0 && prod[sb[i]] >= i)];
            kbo[i] = kb[i];
            K->PB[i] = (bm >> i) & 1u ? &vb[0][1] : PBo[i];   /* (B MOVES: the block itself sees no B) */
            K->kb[i] = (bm >> i) & 1u ? 0.0f : kb[i];
            K->ka[i] = ka[i];
            K->fl[i] = fl[i];
            K->ty[i] = (uint8_t)ty;
            K->sa[i] = sa[i];
            fn[i] = (fl[i] & 4u) ? wk_ctl : wk_tab[ty] ? wk_tab[ty] : wk_none;
        }
        if (outb >= 0)
            need |= 1u << (outb + 1);
        K->v = v;
        K->W = W;
        K->vb = vb;
        K->prod = prod;
        K->trg = &vb[WS_TRIG][1];
        K->hz = hz;
        K->gatev = gatev;
        K->pos = pos;
        K->pinc = pinc;
        K->t = t;
        K->lis = lis;
        K->sw14 = sw14;
        K->lead = lead;
        /* the values that hold through the step (the built-ins, and what nothing here makes), where read */
        sv[WS_GATE] = gatev;
        for (si = 1; si < WS_N; si++)
            if (prod[si] < 0 && si != WS_TRIG && !(si == WS_NOISE && noise_on)) {
                if ((need >> si) & 1u)
                    for (q = 0; q <= WR; q++)
                        vb[si][q] = sv[si];
            } else {
                vb[si][WR] = sv[si];              /* (made here: where the last run ended, moved to [0] below) */
                made[nmade++] = (uint8_t)si;
            }
        for (q = 0; q <= WR; q++)
            vb[0][q] = 0.0f;
        if (!back) {                            /* no cord from a later block: no loops, every block on its own, in order */
            for (i = 0; i < WF_BLOCKS; i++)
                if ((comp >> i) & 1u) {
                    gst[ng] = (uint8_t)ng;
                    ol[ng++] = (uint8_t)i;
                }
            gst[ng] = (uint8_t)ng;
        } else {
            /* loops: blocks that reach each other run sample by sample together */
            for (i = 0; i < WF_BLOCKS; i++)
                reach[i] = pred[i];
            for (q = 0; q < WF_BLOCKS; q++)      /* (reach[i]: every block i depends on, through any cords) */
                for (i = 0; i < WF_BLOCKS; i++)
                    if ((reach[i] >> q) & 1u)
                        reach[i] |= reach[q];
            for (i = 0; i < WF_BLOCKS; i++) {
                scc[i] = (uint16_t)(1u << i);
                for (q = 0; q < WF_BLOCKS; q++)
                    if (q != i && ((reach[i] >> q) & 1u) && ((reach[q] >> i) & 1u))
                        scc[i] |= (uint16_t)(1u << q);
            }
            {   /* the groups in an order that has every block's makers first (the lowest block first where free) */
                uint16_t done = 0;
                int nol = 0;
                while ((done & comp) != comp) {
                    for (i = 0; i < WF_BLOCKS; i++) {
                        uint16_t gm = scc[i], ext = 0;
                        if (!((comp >> i) & 1u) || ((done >> i) & 1u))
                            continue;
                        for (q = 0; q < WF_BLOCKS; q++)
                            if ((gm >> q) & 1u)
                                ext |= pred[q];
                        if (!((ext & ~gm) & ~done))
                            break;
                    }
                    if (i == WF_BLOCKS)         /* (can't be: each loop is a group) */
                        break;
                    gst[ng++] = (uint8_t)nol;
                    for (q = 0; q < WF_BLOCKS; q++)   /* (its blocks in their order) */
                        if ((scc[i] >> q) & 1u)
                            ol[nol++] = (uint8_t)q;
                    done |= scc[i];
                }
                gst[ng] = (uint8_t)nol;
            }
        }
        P->outb = outb;
        P->naud = naud;
        P->ng = ng;
        P->na = na;
        P->nmade = nmade;
        P->noise_on = noise_on;
        P->ctl = ctl;
        P->comp = comp;
    }
}

/* the scopes over a run: a moment of each block (each point the biggest of those it stands for), started on a rising
 * crossing (in the flash: only while they are on screen) */
static __attribute__((noinline)) void weft_scopes(const wplan_t *P, const wcx_t *W, const rv_weft *w, float (*vb)[WR + 1], int m)
{
    int i, j;
    for (i = 0; i < WF_BLOCKS; i++) {
        {
            int ty = w->type[i];
            if (!wsc[i].on || !((P->comp >> i) & 1u) || ((P->ctl >> i) & 1u))
                continue;                   /* (as before: a slow LFO's scope stands still) */
            for (j = 0; j < m; j++) {
                float y = vb[i + 1][1 + j];
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
                                         ty == WO_OCLK || ty == WO_EDGE || ty == WO_CHAOS || ty == WO_FOLLOW || ty == WO_RUNGLER || (ty == WO_CYCLE && W->c1[i] < 0.0005f) ? 96 : 3)) {
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
}
}

RV_HOT2 static void weft_voice(wv_t *v, int t, const float *eff, float hz, float *out, int n, int scope)   /* (in the pool: it runs every sample) */
{
    wcx_t *W = v->W;
    const rv_weft *w = &rv.weft[t];
    float *sv = v->sv, peak = 0.0f;
    float vb[WS_N][WR + 1];                     /* (2 KB of the stack: the second core's has room for it) */
    wplan_t P;
    wk_t K;
    int i, j, g, q, j0;
    RV_FAR(weft_plan)(v, t, eff, hz, scope, &P, &K, vb);
    {
        for (j0 = 0; j0 < n; j0 += WR) {
            int m = n - j0 < WR ? n - j0 : WR;
            for (q = 0; q < P.nmade; q++)               /* the sample before this run */
                vb[P.made[q]][0] = vb[P.made[q]][WR];   /* ([WR]: where the last run ended) */
            if (!j0) {                          /* TRIG: the step's first sample, if a note has just come */
                for (q = 1; q <= WR; q++)
                    vb[WS_TRIG][q] = 0.0f;
                vb[WS_TRIG][1] = v->trig ? 1.0f : 0.0f;
            } else
                vb[WS_TRIG][1] = 0.0f;
            if (P.noise_on)
                for (j = 0; j < m; j++)
                    vb[WS_NOISE][1 + j] = xs_uni(&v->rng) * 2.0f - 1.0f;
            for (g = 0; g < P.ng; g++) {
                int g0 = P.gst[g], g1 = P.gst[g + 1], single = g1 - g0 == 1, step, jj, gq;
                for (jj = 0; jj < m; jj += step) {
                    step = single ? m - jj : 1;
                    for (gq = g0; gq < g1; gq++) {
                        i = P.ol[gq];
                        if (single && P.mv[i] && step > 8 - (jj & 7))
                            step = 8 - (jj & 7);   /* (a moving block: to the next eighth sample) */
                        /* ---- block i, samples jj .. jj + step - 1 of this run ---- */
                        if (P.fl[i] & 0x18u) {   /* its settings moved by B, each eighth sample */
                            if (!((j0 + jj) & 7)) {
                                if (P.fl[i] & 8u) {        /* B MOVES: B turns the second setting */
                                    if (P.fl[i] & 32u)
                                        RV_FAR(wf_setup)(W, i, w->type[i], W->p1[i], clampf(clampf(eff[wf_sp(2 * i + 1)], 0.0f, 127.0f) * (1.0f / 127.0f) + P.PBo[i][jj] * P.kbo[i] * 0.5f, 0.0f, 1.0f), 0.0f, 1);
                                } else                  /* the resonators' PITCH, two octaves each way */
                                    RV_FAR(wf_setup)(W, i, w->type[i], W->p1[i], W->p2[i], K.PB[i][jj] * K.kb[i] * 2.0f, 0);
                            }
                        }
                        K.j0 = j0;
                        P.fn[i](&K, i, jj, jj + step);
                    }
                }
            }
            if (scope)
                RV_FAR(weft_scopes)(&P, W, w, vb, m);
            /* the voice's sound: OUT's block and the AUDIO blocks, through a soft limit */
            for (j = 0; j < m; j++) {
                float aud = 0.0f, x;
                for (q = 0; q < P.na; q++)
                    aud += vb[P.al[q] + 1][1 + j];
                x = (P.outb >= 0 ? vb[P.outb + 1][1 + j] : 0.0f) + aud;
                x = sat(x * 0.6f) * 0.7f;       /* (a patch can't run away with the level) */
                out[j0 + j] += x;
                if (fm_fabsf(x) > peak)
                    peak = fm_fabsf(x);
            }
            for (q = 0; q < P.nmade; q++)              /* (this run's last sample: the next one's [0]) */
                vb[P.made[q]][WR] = vb[P.made[q]][m];
        }
        if (n > 0)
            v->trig = 0;
        for (q = 0; q < P.nmade; q++)                  /* what was made, kept for the next step */
            sv[P.made[q]] = vb[P.made[q]][WR];
    }
    /* a released voice ends when it has been quiet a moment (or after a long while); one a patch's
     * output block needs goes on */
    if (!v->gate) {
        v->rel += (float)n * RV_DT;
        v->quiet = peak < 0.0005f ? v->quiet + (float)n * RV_DT : 0.0f;
        if (!wf_has_out[t] && (v->quiet > 0.25f || v->rel > 12.0f || (P.outb < 0 && !P.naud)))
            v->on = 0;
    }
}

/* the voice whose blocks the scopes show this step (-1 none) */
static int8_t wf_scope_v = -1;

/* WEFT voice k this step, added into out, with core c's working values (the second core may play one too);
 * its peak back */
static float weft_voice_one(int k, float *out, int n, int c)
{
    wv_t *v = &wv[k];
    const float *eff = trk[v->track & 7].eff;
    float hz = note_hz((float)v->note + eff[TP_PITCH] + eff[TP_FINE] * 0.01f), pk = 0.0f;
    int i;
    v->W = &wcxs[c & 1];
#ifdef RV_WEFT_HASH
    uint32_t t0_ = plat_cycles();
#endif
    RV_FAR(weft_voice)(v, v->track & 7, eff, hz > 12000.0f ? 12000.0f : hz, out, n, k == wf_scope_v);
#ifdef RV_WEFT_HASH
    { extern uint64_t rv_weft_ns; rv_weft_ns += plat_cycles() - t0_; }
    {   /* (the comparison build: every WEFT voice's sound, bit for bit, folded into one number) */
        extern uint32_t rv_weft_hash, rv_weft_frames;
        for (i = 0; i < n; i++) {
            uint32_t u;
            __builtin_memcpy(&u, &out[i], 4);
            rv_weft_hash = (rv_weft_hash ^ u) * 16777619u;
        }
        rv_weft_frames += (uint32_t)n;
    }
#endif
    for (i = 0; i < n; i++)
        pk = fm_maxf(pk, fm_fabsf(out[i]));
    return pk;
}

/* each step, before the voices: voices of tracks that are no longer WEFT go; a patch with an output block
 * keeps a voice; the clock's place on the tempo's grid moves on */
static void weft_tidy(int n)
{
    int i, t, c = 0;
    wf_clk_owner = wf_mod_owner = -1;
    weft_gate_mute = (uint8_t)(weft_gatet & ~weft_gate_open);   /* (GATE: quiet unless A was up last step) */
    weft_gate_open = weft_own = weft_gatet = 0;
    for (t = RV_TRACKS - 1; t >= 0; t--) {
        int oc = 0, om = 0, sq2 = 0;
        if (rv.tp[t][TP_KIND] == KIND_WEFT)
            for (i = 0; i < WF_BLOCKS; i++) {
                const rv_weft *w = &rv.weft[t];
                oc |= w->type[i] == WO_OCLK;
                om |= w->type[i] == WO_OMOD;
                if (w->type[i] == WO_SEQ) {     /* (its track, its mode: a SEQ OUT stepping a track owns its clock) */
                    int tr = clampi(rv.tp[t][wf_sp(2 * i)], 0, 7), md = clampi(rv.tp[t][wf_sp(2 * i + 1)], 0, 4);
                    sq2 = 1;
                    if (md == WSQ_STEP)
                        weft_own |= (uint8_t)(1u << tr);
                    else if (md == WSQ_GATE)
                        weft_gatet |= (uint8_t)(1u << tr);
                }
            }
        wf_has_out[t] = (uint8_t)(oc || om || sq2 || (rv.tp[t][TP_KIND] == KIND_WEFT && (rv.weft[t].run & 1u)));   /* (RUN ALWAYS too) */
        wf_lead[t] = -1;
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
    {                                           /* the scopes: the newest voice of the track on screen, while a WEFT
                                                 * page or the emulator's patcher shows them */
        uint32_t age = 0;
        int k, nv = -1, ut = rv_live.ui_track & 7;
        if (rv_live.wf_view)
            rv_live.wf_view--;
        for (i = 0; i < WV_N; i++)
            if (rv_live.wf_view && wv[i].on && wv[i].track == ut && wv[i].age > age)
                age = wv[i].age, nv = i;
        wf_scope_v = (int8_t)nv;
        for (k = 0; k < WF_BLOCKS; k++)
            wsc[k].on = nv >= 0 && rv.weft[ut].type[k] != WO_NONE;
    }
    for (i = WV_N - 1; i >= 0; i--)
        if (wv[i].on)
            wf_lead[wv[i].track & 7] = (int8_t)i;   /* (the lowest voice of a track conducts its SEQ OUTs) */
    for (i = 0; i < WV_N; i++)
        c += wv[i].on;
    rv_live.wvoices = (uint8_t)c;
    weft_pos16 += (float)n * RV_DT / (clk_period > 0.001f ? clk_period : 0.001f);
    if (weft_pos16 >= 384.0f)                   /* (a whole number of every division's length) */
        weft_pos16 -= 384.0f;
}
