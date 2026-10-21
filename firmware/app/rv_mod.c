/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: movement and modulation. Silt's two-oscillator master clock, two LFOs, dust; the mod
 * matrix; the macros (each destination with its own start, end, curve and window on the
 * macro's travel); and the effective value of every parameter: its knob or step lock, plus the
 * matrix, the macros, the articulation's destinations and influence MOD routes.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

track_t trk[RV_TRACKS];
static float geff[GP_N];                            /* global parameters after modulation */
#define G(i) geff[i]
static uint32_t mod_rng = 0x2545F491u;

/* --------------------------------------------------------------- clock --- */
/* Oscillator A fires where its wave crosses the threshold (rising through +thr, falling through
 * -thr: a symmetric wave at threshold 0 gives straight time, two pulses a cycle). Oscillator B
 * bends A by rate, shape, phase, sum or logic. Locked keeps the average tempo; free drifts. */
static float clk_pha, clk_phb, clk_wprev = -1.0f, clk_drift = 1.0f, clk_env;
static uint8_t clk_gprev;
static float clk_period = 0.134f;                   /* seconds per pulse (one step) */

static float clock_wave(float ph, float s)
{
    float saw, tri, sine, w, t;
    ph -= fm_floorf(ph);
    saw = 2.0f * ph - 1.0f;
    tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    sine = -sin_lut(ph + 0.25f);
    s = clampf(s, 0.0f, 1.0f);
    if (s < 0.25f)
        w = ph < 0.5f + 0.2f * (s / 0.25f) ? 1.0f : -1.0f;
    else if (s < 0.5f) {
        t = (s - 0.25f) / 0.25f;
        w = (ph < 0.7f ? 1.0f : -1.0f) * (1.0f - t) + saw * t;
    } else if (s < 0.75f) {
        t = (s - 0.5f) / 0.25f;
        w = saw * (1.0f - t) + tri * t;
    } else {
        t = (s - 0.75f) / 0.25f;
        w = tri * (1.0f - t) + sine * t;
    }
    return w;
}

/* advance n samples; pulse offsets (samples into the block) go to off[], returns the count */
static int clock_tick(int n, int *off, int max)
{
    float fa0 = G(GP_TEMPO) / 60.0f * 4.0f * 0.5f;   /* two pulses per cycle of A: one per step */
    int mode = clampi((int)G(GP_CLKMODE), 0, 7), i, fired = 0;
    float depth = G(GP_CLKDEPTH) * (1.0f / 127.0f), thr = G(GP_CLKTHR) * (0.9f / 127.0f);
    float brate = 0.01f * fm_exp2f(G(GP_CLKBRATE) * (10.0f / 127.0f));
    float ashape = G(GP_CLKSHAPE) * (1.0f / 127.0f), bshape = G(GP_CLKBSHAPE) * (1.0f / 127.0f);
    int locked = G(GP_CLKLOCK) < 0.5f;
    float feff = fa0;
    if (!locked) {
        clk_drift *= fm_expf(xs_bi(&mod_rng) * 0.027f * depth);
        clk_drift = clampf(clk_drift, 0.7f, 1.4f);
    } else {
        clk_drift = 1.0f;
    }
    for (i = 0; i < n; i++) {
        float wb, fa = fa0, shape = ashape, phoff = 0.0f, wa, w;
        int fire;
        clk_phb += brate * RV_DT;
        if (clk_phb >= 1.0f)
            clk_phb -= 1.0f;
        wb = clock_wave(clk_phb, bshape);
        if (mode == 1)
            fa = locked ? fa0 * (1.0f + 0.9f * depth * wb) : fa0 * fm_exp2f(depth * 1.5f * wb) * clk_drift;
        else if (mode == 2)
            shape += depth * 0.5f * wb;
        else if (mode == 3)
            phoff = depth * 0.25f * wb;
        else if (!locked)
            fa = fa0 * clk_drift;
        feff = fa;
        clk_pha += fa * RV_DT;
        if (clk_pha >= 1.0f)
            clk_pha -= 1.0f;
        wa = clock_wave(clk_pha + phoff, shape);
        w = mode == 4 ? wa + depth * wb : wa;
        if (mode >= 5 && depth > 0.01f) {
            int ga = wa >= thr, gb = wb >= 0.0f, g = mode == 5 ? (ga && gb) : mode == 6 ? (ga || gb) : (ga != gb);
            fire = g != clk_gprev;
            clk_gprev = (uint8_t)g;
        } else {
            fire = (clk_wprev < thr && w >= thr) || (clk_wprev > -thr && w <= -thr);
        }
        clk_wprev = w;
        if (fire && fired < max) {
            off[fired++] = i;
            clk_env = 1.0f;
        }
    }
    clk_period = 1.0f / fm_maxf(0.05f, 2.0f * feff);
    clk_env *= fm_expf(-(float)n * RV_DT / fm_maxf(0.01f, clk_period * 0.5f));
    rv_live.clock_env = clk_env;
    return fired;
}

static void clock_reset(void)
{
    clk_pha = 0.2499f;                               /* the first pulse lands within a sample or two */
    clk_phb = 0.0f;
    clk_wprev = -1.0f;
    clk_gprev = 0;
}

/* ---------------------------------------------------------- LFOs, dust --- */
typedef struct { float ph, held, from, to, value; } lfo_t;
static lfo_t lfo[2];
static float dust_v;
static uint32_t dust_rng = 0xD057D057u;
static uint8_t lfo_wrapped[2], dust_fired;

static uint8_t lfo_note_trig;                          /* a note started: RETRIG and 1 SHOT LFOs start over */

/* SYM: the wave's middle moves from 5% to 95% of the cycle, so each half speeds up or slows down
 * (a sine leans, a triangle turns toward a ramp, a square becomes a pulse) */
static float lfo_warp(float q, float sym)
{
    float b = 0.5f + 0.45f * sym;
    return q < b ? 0.5f * q / b : 0.5f + 0.5f * (q - b) / (1.0f - b);
}
static float lfo_wave(int shape, float q)
{
    switch (shape) {
    case 0: return sin_lut(q);
    case 1: return q < 0.5f ? 4.0f * q - 1.0f : 3.0f - 4.0f * q;
    case 2: return 2.0f * q - 1.0f;
    case 3: return 1.0f - 2.0f * q;
    default: return q < 0.5f ? 1.0f : -1.0f;
    }
}

/* mode 0 LOOP, 1 RETRIG (a note restarts it), 2 1 SHOT (a note plays one cycle, then it holds);
 * phase: where the cycle starts, 0..1 */
static float lfo_tick(lfo_t *l, float rate, int shape, float dt, uint8_t *wrap, float sym, float phase, int mode)
{
    int w = 0;
    float q;
    if (mode == 2) {                                  /* one cycle, then still */
        if (l->ph < 1.0f) {
            l->ph += rate * dt;
            if (l->ph >= 1.0f)
                l->ph = 1.0f;
        }
    } else {
        l->ph += rate * dt;
        while (l->ph >= 1.0f) {
            l->ph -= 1.0f;
            w = 1;
        }
    }
    *wrap = (uint8_t)w;
    q = l->ph + phase;
    if (q > 1.0f)                                     /* (a finished 1 SHOT rests at the cycle's end) */
        q -= 1.0f;
    q = lfo_warp(q, sym);
    switch (shape) {
    case 5: if (w) l->held = xs_bi(&mod_rng); l->value = l->held; break;
    case 6:
        if (w) {
            l->from = l->to;
            l->to = xs_bi(&mod_rng);
        }
        l->value = l->from + (l->to - l->from) * (0.5f - 0.5f * sin_lut(q * 0.5f + 0.25f));
        break;
    default: l->value = lfo_wave(shape, q); break;
    }
    return l->value;
}

/* ------------------------------------------------- STEP ENV: the LIBRARY played --- */
/* one per track: a step (or a note, SE NOTE) starts an envelope; it runs its segments, loops back or
 * hands over to its NEXT (a chain, starting where it left off), or holds; a new one replaces it */
static struct {
    uint8_t on, env, seg;
    uint8_t hops;                   /* chain hand-overs in this block (a guard against a zero-time ring) */
    float u, from, lvl;
} senv[RV_TRACKS];
static float step_seconds(int t);

static void senv_trigger(int t, int e)
{
    const rv_senv *v;
    if (e < 0 || e >= RV_SENV || !SENV_NSEG(&rv.senv[e]))
        return;
    v = &rv.senv[e];
    senv[t].on = 1;
    senv[t].env = (uint8_t)e;
    senv[t].seg = 0;
    senv[t].u = 0.0f;
    senv[t].lvl = senv[t].from = (float)v->start * (1.0f / 127.0f);
}

static void senv_tick_one(int t, float dt)
{
    int guard;
    for (guard = 0; guard < 12 && senv[t].on; guard++) {      /* (zero-time segments pass in the same block) */
        const rv_senv *v = &rv.senv[senv[t].env % RV_SENV];
        int n = SENV_NSEG(v), s = senv[t].seg;
        float tsec, to, k;
        if (!n) {
            senv[t].on = 0;
            break;
        }
        if (s >= n) {                                          /* the end: loop, chain, or hold */
            if (v->loop && v->loop <= n && guard < 11) {
                senv[t].seg = (uint8_t)(v->loop - 1);
                senv[t].u = 0.0f;
                senv[t].from = senv[t].lvl;
                continue;
            }
            if (v->next && v->next <= RV_SENV && SENV_NSEG(&rv.senv[v->next - 1]) && guard < 11) {
                senv[t].env = (uint8_t)(v->next - 1);       /* the chain: from where this one ended */
                senv[t].seg = 0;
                senv[t].u = 0.0f;
                senv[t].from = senv[t].lvl;
                continue;
            }
            senv[t].on = 0;                                    /* holds its last level */
            break;
        }
        tsec = SENV_FREE(v) ? (v->tim[s] ? rv_time_f((float)v->tim[s]) : 0.0f) : (float)v->tim[s] * 0.125f * step_seconds(t);
        to = (float)v->lvl[s] * (1.0f / 127.0f);
        senv[t].u += tsec > 0.0001f ? dt / tsec : 1.0f;
        if (senv[t].u < 1.0f) {
            k = (float)senv_crv(v, s) * 0.9f;
            senv[t].lvl = senv[t].from + (to - senv[t].from) * fe_bend(senv[t].u, k);
            break;
        }
        dt = tsec > 0.0001f ? (senv[t].u - 1.0f) * tsec : dt;  /* what is left of the block goes on */
        senv[t].lvl = senv[t].from = to;
        senv[t].seg++;
        senv[t].u = 0.0f;
        if (dt <= 0.0f)
            break;
    }
}

/* (the emulator's tests) */
float rv_senv_level(int t, int *env)
{
    if (env)
        *env = senv[t & 7].on ? senv[t & 7].env : -1;
    return senv[t & 7].lvl;
}
void rv_senv_start(int t, int e) { senv_trigger(t & 7, e); }

static void movement_tick(int n)
{
    float dt = (float)n * RV_DT;
    float r1 = 0.01f * fm_exp2f(G(GP_LFO1RATE) * (11.0f / 127.0f)), r2 = 0.01f * fm_exp2f(G(GP_LFO2RATE) * (11.0f / 127.0f));
    float drate = 0.1f * fm_exp2f(G(GP_DUSTRATE) * (9.0f / 127.0f)), ddec = 0.01f * fm_exp2f(G(GP_DUSTDECAY) * (8.0f / 127.0f));
    int k;
    if (lfo_note_trig) {                              /* a note: RETRIG and 1 SHOT start from their phase */
        for (k = 0; k < 2; k++)
            if (rv.gp[k ? GP_LFO2MODE : GP_LFO1MODE] > 0)
                lfo[k].ph = 0.0f;
        lfo_note_trig = 0;
    }
    for (k = 0; k < 2; k++)
        lfo_tick(&lfo[k], k ? r2 : r1, clampi((int)G(k ? GP_LFO2SHAPE : GP_LFO1SHAPE), 0, 6), dt, &lfo_wrapped[k],
                 clampf(G(k ? GP_LFO2SYM : GP_LFO1SYM) * (1.0f / 64.0f), -1.0f, 1.0f),
                 clampf(G(k ? GP_LFO2PHASE : GP_LFO1PHASE) * (1.0f / 360.0f), 0.0f, 0.9999f),
                 clampi(rv.gp[k ? GP_LFO2MODE : GP_LFO1MODE], 0, 2));
    dust_v *= fm_expf(-dt / ddec);
    dust_fired = 0;
    if (xs_uni(&dust_rng) < drate * dt) {
        dust_v = fm_maxf(dust_v, 0.3f + 0.7f * xs_uni(&dust_rng));
        dust_fired = 1;
    }
    rv_live.lfo1 = lfo[0].value;
    rv_live.lfo2 = lfo[1].value;
    rv_live.dust = dust_v;
    for (k = 0; k < RV_TRACKS; k++)
        senv_tick_one(k, dt);
}

/* ------------------------------------------------------------ the sum --- */
static float midi_wheel;

static float macro_curve(int c, float u)
{
    switch (c) {
    case 1: return u * u;                            /* exp */
    case 2: return 1.0f - (1.0f - u) * (1.0f - u);   /* log */
    case 3: return u * u * (3.0f - 2.0f * u);        /* s */
    default: return u;
    }
}

static void eff_add(int pid, float amt_units)
{
    if (pid < 0)
        return;
    if (pid < RV_GPID) {
        int t = pid / RV_TP, p = pid % RV_TP;
        if (p < TP_N)
            trk[t].eff[p] += amt_units;
    } else if (pid - RV_GPID < GP_N) {
        geff[pid - RV_GPID] += amt_units;
    }
}

static float pid_range(int pid)
{
    const rv_pinfo *pi = rv_pinfo_pid(pid);
    return pi->name ? (float)(pi->max - pi->min) : 0.0f;
}

/* KEY: parameter units per semitone, so AMOUNT +63 follows the keyboard exactly */
static float key_units(int pid)
{
    int p;
    if (pid >= RV_GPID)
        return 1.0f;
    p = pid % RV_TP;
    switch (p) {
    case TP_PITCH: return 1.0f;
    case TP_FINE: return 100.0f;                         /* cents */
    case TP_CUT: return 127.0f / 120.0f;                 /* 10 octaves over the knob */
    case TP_INA: return 1.0f;                            /* the comb's TUNE is in semitones */
    default: return 1.0f;                                /* other knobs: one step a key */
    }
}

static void eff_compute(void)
{
    int t, p, i, k;
    float src[MS_N];
    /* the knobs and the step locks */
    for (t = 0; t < RV_TRACKS; t++)
        for (p = 0; p < TP_N; p++)
            trk[t].eff[p] = trk[t].lockon[p] ? (float)trk[t].lockv[p] : (float)rv.tp[t][p];
    for (i = 0; i < GP_N; i++)
        geff[i] = (float)rv.gp[i];
    /* sources */
    src[MS_OFF] = 0.0f;
    src[MS_LFO1] = lfo[0].value;
    src[MS_LFO2] = lfo[1].value;
    src[MS_DUST] = dust_v;
    src[MS_WHEEL] = midi_wheel;
    src[MS_M1] = rv.gp[GP_MACRO1] * (1.0f / 127.0f);
    src[MS_M2] = rv.gp[GP_MACRO2] * (1.0f / 127.0f);
    src[MS_M3] = rv.gp[GP_MACRO3] * (1.0f / 127.0f);
    src[MS_M4] = rv.gp[GP_MACRO4] * (1.0f / 127.0f);
    src[MS_CLOCK] = clk_env;
    src[MS_KEY] = trk[0].lead_key * (1.0f / 60.0f);
    /* matrix: ENV, VELO and KEY are the destination track's. A row may move another row's AMOUNT
     * (destination MATRIX, ROW n): those go first, twice, so one row can move a second that
     * moves a third */
    {
        float aeff[RV_MATRIX], add[RV_MATRIX];
        int pass;
        for (i = 0; i < RV_MATRIX; i++)
            aeff[i] = (float)rv.mat[i].amt;
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < RV_MATRIX; i++)
                add[i] = 0.0f;
            for (i = 0; i < RV_MATRIX; i++) {
                const rv_mslot *m = &rv.mat[i];
                int kk = (int)m->dst - RV_NPID, dt;
                float s;
                if (m->src == MS_OFF || m->src >= MS_N || kk < 0 || kk >= RV_MATRIX || kk == i)
                    continue;
                dt = rv.mat[kk].dst < RV_GPID ? rv.mat[kk].dst / RV_TP : 0;   /* the moved row's track */
                s = m->src == MS_ENV ? trk[dt].lead_env : m->src == MS_VEL ? trk[dt].lead_vel
                    : m->src == MS_KEY ? trk[dt].lead_key * (1.0f / 60.0f) : src[m->src];
                add[kk] += s * aeff[i];
            }
            for (i = 0; i < RV_MATRIX; i++)
                aeff[i] = clampf((float)rv.mat[i].amt + add[i], -64.0f, 63.0f);
        }
        for (i = 0; i < RV_MATRIX; i++) {
            const rv_mslot *m = &rv.mat[i];
            float s, amt = aeff[i] * (1.0f / 64.0f);
            rv_live.mat_amt[i] = (int8_t)aeff[i];
            if (m->src == MS_OFF || m->src >= MS_N || fm_fabsf(aeff[i]) < 0.01f || m->dst >= RV_NPID)
                continue;
            if (m->src == MS_KEY) {                     /* the destination track's note */
                int dt = m->dst < RV_GPID ? m->dst / RV_TP : 0;
                eff_add(m->dst, trk[dt].lead_key * key_units(m->dst) * clampf(aeff[i] * (1.0f / 63.0f), -1.0f, 1.0f));
                continue;
            }
            if (m->src == MS_ENV || m->src == MS_VEL) {
                int dt = m->dst < RV_GPID ? m->dst / RV_TP : 0;
                s = m->src == MS_ENV ? trk[dt].lead_env : trk[dt].lead_vel;
            } else {
                s = src[m->src];
            }
            eff_add(m->dst, s * amt * pid_range(m->dst));
        }
    }
    /* macros */
    for (k = 0; k < RV_MACROS; k++) {
        float v = src[MS_M1 + k];
        for (i = 0; i < RV_MACRO_DESTS; i++) {
            const rv_mdest *d = &rv.macro[k].d[i];
            float lo, hi, u, val;
            if (d->dst >= RV_NPID)
                continue;
            lo = d->lo * 0.01f;
            hi = d->hi * 0.01f;
            if (hi <= lo + 0.001f)
                hi = lo + 0.001f;
            u = clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
            val = (float)d->start + ((float)d->end - (float)d->start) * macro_curve(d->curve, u);
            eff_add(d->dst, val * (1.0f / 64.0f) * pid_range(d->dst));
        }
    }
    /* the articulation's own destinations and influence MOD */
    for (t = 0; t < RV_TRACKS; t++) {
        float e = trk[t].lead_env;
        int d1 = rv.tp[t][TP_EDST1], d2 = rv.tp[t][TP_EDST2];
        if (rv.tp[t][TP_EAMT1] && rv_is_moddest(d1))
            trk[t].eff[d1] += e * rv.tp[t][TP_EAMT1] * (1.0f / 64.0f) * pid_range(PID_T(t, d1));
        if (rv.tp[t][TP_EAMT2] && rv_is_moddest(d2))
            trk[t].eff[d2] += e * rv.tp[t][TP_EAMT2] * (1.0f / 64.0f) * pid_range(PID_T(t, d2));
        {   /* STEP ENV: the LIBRARY envelope the steps started */
            int ds = rv.tp[t][TP_SEDST];
            if (rv.tp[t][TP_SEAMT] && rv_is_moddest(ds) && rv_tp_applies(t, TP_SEAMT))
                trk[t].eff[ds] += senv[t].lvl * rv.tp[t][TP_SEAMT] * (1.0f / 64.0f) * pid_range(PID_T(t, ds));
        }
        for (k = 0; k < RV_TRACKS; k++) {
            const rv_route *r = &rv.infl[t][k];
            if (r->type == RT_MOD && r->amt && rv_is_moddest(r->arg))
                trk[k].eff[r->arg] += e * r->amt * (1.0f / 64.0f) * pid_range(PID_T(k, r->arg));
        }
    }
    /* back inside each range */
    for (t = 0; t < RV_TRACKS; t++)
        for (p = TP_PITCH; p < TP_N; p++) {
            const rv_pinfo *pi = rv_pinfo_track(t, p);
            if (pi->name)
                trk[t].eff[p] = clampf(trk[t].eff[p], (float)pi->min, (float)pi->max);
        }
    for (p = TP_S0; p <= TP_S15; p++)
        for (t = 0; t < RV_TRACKS; t++) {
            const rv_pinfo *pi = rv_pinfo_track(t, p);
            if (pi->name)
                trk[t].eff[p] = clampf(trk[t].eff[p], (float)pi->min, (float)pi->max);
        }
    for (i = 0; i < GP_N; i++) {
        const rv_pinfo *pi = rv_pinfo_global(i);
        if (pi->name)
            geff[i] = clampf(geff[i], (float)pi->min, (float)pi->max);
    }
    for (i = 0; i < MS_N; i++)
        rv_live.msrc[i] = src[i];
    for (t = 0; t < RV_TRACKS; t++)
        rv_live.key[t] = trk[t].lead_key;
}

float rv_param_eff(int t, int p) { return t >= 0 && t < RV_TRACKS && p >= 0 && p < TP_N ? trk[t].eff[p] : 0.0f; }
