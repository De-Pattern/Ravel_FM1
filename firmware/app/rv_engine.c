/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the engine. Allocates voices, runs every track's chain (source, articulation, filter,
 * skin, insert, mixer), the influence between tracks, the buses and the master, in control
 * steps of 32 frames. The control side talks to it through a command queue, so the FM-1's
 * audio interrupt and its control task never touch the same state at once.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

rv_project rv;
volatile rv_live_t rv_live;

static voice_t vs[RV_SYNTH_VOICES];
static strike_t ss[RV_STRIKE_VOICES];
static perc_t ps[RV_PERC_VOICES];
static float click_env, click_ph, click_hz;
/* MOOM's sound effects (rv_moom.c asks with moom_sfx_req): a click and zap, a hit, a resolved chime,
 * a hurt buzz, a pickup, a win, a fall; square waves and noise, after the limiter like the click */
static volatile uint8_t moom_sfx_req;
/* MOOM's soundtrack (moom_music: 0 off, else the level + 1): an original loop at 128 BPM in 16ths, a
 * driving bass in A minor, kick, snare and hats from noise, and a small square hook every other bar;
 * level 2 sits a fourth higher. Quiet, under the effects. */
static volatile uint8_t moom_music;
static float mus_t, mus_ph, mus_ph2, mus_lp;
static uint32_t mus_rng = 0xBEEFu;
static float mus_tick(void)
{
    static const int8_t bass[32] = {33, 33, 45, 33, 33, 33, 43, 45, 33, 33, 45, 33, 36, 36, 38, 40,
                                    33, 33, 45, 33, 33, 33, 43, 45, 29, 29, 41, 29, 31, 31, 43, 32};
    static const int8_t hook[16] = {69, 0, 72, 0, 76, 0, 74, 72, 69, 0, 67, 0, 69, 0, 0, 0};
    const float st = 60.0f / 128.0f / 4.0f;
    int k = (int)(mus_t / st), s = k & 31, tr = moom_music > 1 ? 5 : 0;
    float loc = mus_t - (float)k * st, y = 0.0f, n, hz;
    mus_t += RV_DT;
    if (mus_t > st * 64.0f)
        mus_t -= st * 64.0f;
    hz = 440.0f * fm_exp2f(((float)(bass[s] + tr) - 69.0f) * (1.0f / 12.0f));   /* the bass: a dark, plucked saw */
    mus_ph += hz * RV_DT;
    mus_ph -= fm_floorf(mus_ph);
    mus_lp += ((2.0f * mus_ph - 1.0f) - mus_lp) * 0.08f;
    y += mus_lp * 0.11f * fm_expf(-loc * 7.0f);
    mus_rng = mus_rng * 1664525u + 1013904223u;
    n = (float)(mus_rng >> 9) * (2.0f / 8388608.0f) - 1.0f;
    if ((s & 7) == 0)                                 /* kick: a falling sine */
        y += fm_sinf(6.2831853f * (55.0f + 90.0f * fm_expf(-loc * 40.0f)) * loc) * 0.16f * fm_expf(-loc * 9.0f);
    if ((s & 7) == 4)                                 /* snare */
        y += n * 0.07f * fm_expf(-loc * 22.0f);
    if (s & 1)                                        /* hats */
        y += (n - mus_lp * 0.0f) * 0.018f * fm_expf(-loc * 60.0f);
    if ((k & 32) && hook[s & 15]) {                   /* the hook, every other bar pair */
        hz = 440.0f * fm_exp2f(((float)(hook[s & 15] + tr) - 69.0f) * (1.0f / 12.0f));
        mus_ph2 += hz * RV_DT;
        mus_ph2 -= fm_floorf(mus_ph2);
        y += (mus_ph2 < 0.25f ? 1.0f : -0.33f) * 0.035f * fm_expf(-loc * 5.0f);
    }
    return y;
}
static uint8_t sfx_kind;
static float sfx_t, sfx_ph;
static uint32_t sfx_rng = 0x5EEDu;
static float sfx_tick(void)
{
    static const float chime[3] = {523.25f, 659.25f, 783.99f}, win[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
    float t = sfx_t, f = 0.0f, a = 0.0f, y;
    int noise = 0;
    switch (sfx_kind) {
    case 1:                                          /* CLICK: two mouse clicks, then a falling zap */
        if (t < 0.012f)
            return (t < 0.002f || (t > 0.008f && t < 0.010f)) ? 0.35f : 0.0f;
        f = 1400.0f * fm_expf(-(t - 0.012f) * 18.0f) + 200.0f;
        a = t < 0.12f ? 0.18f * (1.0f - t / 0.12f) : 0.0f;
        break;
    case 2: noise = 1; a = t < 0.07f ? 0.3f * (1.0f - t / 0.07f) : 0.0f; break;   /* HIT */
    case 3: f = chime[clampi((int)(t / 0.07f), 0, 2)]; a = t < 0.21f ? 0.16f : 0.0f; break;   /* RESOLVED */
    case 4: f = 70.0f; a = t < 0.18f ? 0.22f * (fm_sinf(t * 190.0f) > 0.0f ? 1.0f : 0.4f) : 0.0f; break;   /* HURT */
    case 5: f = t < 0.06f ? 660.0f : 990.0f; a = t < 0.12f ? 0.15f : 0.0f; break;   /* PICKUP */
    case 6: f = win[clampi((int)(t / 0.09f), 0, 3)]; a = t < 0.36f ? 0.16f : 0.0f; break;   /* WIN */
    case 7: f = 400.0f * fm_expf(-t * 3.5f) + 50.0f; a = t < 0.7f ? 0.2f * (1.0f - t / 0.7f) : 0.0f; break;   /* DOWN */
    case 8: {                                        /* TITLE: an original galloping riff on A, then a held power chord */
        static const uint8_t riff[16] = {45, 45, 45, 0, 48, 0, 45, 45, 50, 0, 49, 0, 45, 45, 52, 51};
        const float st = 0.11f;
        int k = (int)(t / st);
        float loc = t - (float)k * st, x;
        sfx_t += RV_DT;
        if (k < 16) {
            float y = 0.0f;
            if (riff[k] && loc < st * 0.8f) {         /* palm-muted: short, dark, pushed into a clip */
                float hz = 440.0f * fm_exp2f(((float)riff[k] - 69.0f) * (1.0f / 12.0f));
                sfx_ph += hz * RV_DT;
                sfx_ph -= fm_floorf(sfx_ph);
                x = (sfx_ph < 0.5f ? 1.0f : -1.0f) + (2.0f * sfx_ph - 1.0f);
                y = sat(x * 1.6f) * 0.2f * fm_expf(-loc * 9.0f);
            }
            if ((k & 7) == 4) {                       /* the snare on 2 and 4 */
                sfx_rng = sfx_rng * 1664525u + 1013904223u;
                y += ((float)(sfx_rng >> 9) * (2.0f / 8388608.0f) - 1.0f) * 0.18f * fm_expf(-loc * 28.0f);
            }
            return y;
        }
        loc = t - 16.0f * st;
        if (loc < 1.2f) {                             /* A2, E3, A3 rung out */
            static float p2, p3;
            float g = 0.16f * fm_expf(-loc * 2.2f);
            sfx_ph += 110.0f * RV_DT;
            p2 += 164.81f * RV_DT;
            p3 += 220.0f * RV_DT;
            sfx_ph -= fm_floorf(sfx_ph);
            p2 -= fm_floorf(p2);
            p3 -= fm_floorf(p3);
            x = (2.0f * sfx_ph - 1.0f) + (2.0f * p2 - 1.0f) + 0.7f * (2.0f * p3 - 1.0f);
            return sat(x * 1.3f) * g;
        }
        sfx_kind = 0;
        return 0.0f;
    }
    default: return 0.0f;
    }
    sfx_t += RV_DT;
    if (a <= 0.0f) {
        sfx_kind = 0;
        return 0.0f;
    }
    if (noise) {
        sfx_rng = sfx_rng * 1664525u + 1013904223u;
        return a * ((float)(sfx_rng >> 9) * (2.0f / 8388608.0f) - 1.0f);
    }
    sfx_ph += f * RV_DT;
    sfx_ph -= fm_floorf(sfx_ph);
    y = sfx_ph < 0.5f ? a : -a;
    return y;
}
static uint32_t age_ctr;
static uint32_t eng_rng = 0x13579BDFu;
static float in_buf[RV_SUB * 2];
static uint8_t punch[8];
static hitshade_t hs_pend;                         /* the next hit's shading (R MOVE, GRAIN), taken by its voice */
static svf_t punch_f[2];
static float lim_gain = 1.0f;
static dcb_t master_dc[2];

/* The guard. Any sound that goes non-finite (NaN, infinity) or runs far past full scale is
 * silenced and its memory cleared where it started (a voice, a track's chain, a bus), so a
 * blown-up state can never reach the output or stay stuck there. rv_fault_* say what tripped. */
#ifndef RV_GUARD
#define RV_GUARD 256.0f
#endif
int rv_fault_count, rv_fault_where, rv_fault_track = -1;
static void fault(int where)
{
    rv_fault_count++;
    rv_fault_where = where;
}
RV_INL int buf_bad(const float *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!(b[i] > -RV_GUARD && b[i] < RV_GUARD))   /* false for NaN too */
            return 1;
    return 0;
}
/* clear the bad samples; 1 if there were any */
static int buf_fix(float *b, int n)
{
    int i, bad = 0;
    for (i = 0; i < n; i++)
        if (!(b[i] > -RV_GUARD && b[i] < RV_GUARD)) {
            b[i] = 0.0f;
            bad = 1;
        }
    return bad;
}
RV_INL void zero_buf(float *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        b[i] = 0.0f;
}
static float master_drive_comp = 1.0f;

uint32_t rv_rng(void) { return xs32(&eng_rng); }

/* ------------------------------------------------------- command queue --- */
enum { C_NOTE, C_LIVE, C_TRANSPORT, C_PUNCH, C_CUT, C_SCENE, C_PATTERN, C_TAP, C_ARM, C_MOTION, C_ALLOFF };
typedef struct { uint8_t op, a; int16_t b, c; } cmd_t;
static cmd_t cq[128];
static volatile uint32_t cq_w, cq_r;

static void cmd(uint8_t op, int a, int b, int c)
{
    uint32_t w = cq_w;
    if (w - cq_r >= (op == C_MOTION ? 96u : 128u))
        return;                                     /* full: drop. Knob motion gives way first, so a burst of
                                                     * knob moves never costs a note-off (a hanging note) */
    cq[w & 127u].op = op;
    cq[w & 127u].a = (uint8_t)a;
    cq[w & 127u].b = (int16_t)b;
    cq[w & 127u].c = (int16_t)c;
    cq_w = w + 1u;
}

void rv_note(int t, int note, int vel) { cmd(C_NOTE, t, note, vel); }
void rv_live_note(int t, int note, int vel) { cmd(C_LIVE, t, note, vel); }
void rv_transport(int run) { cmd(C_TRANSPORT, 0, run, 0); }
void rv_punch(int fx, int on) { cmd(C_PUNCH, fx, on, 0); }
void rv_cut(int action) { cmd(C_CUT, 0, action, 0); }
void rv_scene_launch(int s) { cmd(C_SCENE, 0, s, 0); }
void rv_pattern_launch(int t, int p) { cmd(C_PATTERN, t, p, 0); }
void rv_tap_tempo(void) { cmd(C_TAP, 0, 0, 0); }
void rv_record_arm(int t, int on) { cmd(C_ARM, t, on, 0); }
void rv_motion(int pid, int v) { cmd(C_MOTION, 0, pid, v); }
void rv_all_off(void) { cmd(C_ALLOFF, 0, 0, 0); }

/* ------------------------------------------------------------- notes --- */
/* a hit's shading: the sequencer sets the next note's (a ratchet's R MOVE) just before it plays;
 * GRAIN adds its own on strike and perc hits. A voice keeps its shading for its whole sound */
static void hs_clear(hitshade_t *h)
{
    h->on = 0;
    h->semi = 0.0f;
    h->lpc = 1.0f;
    h->gain = 1.0f;
    h->dk = 0.0f;
    h->env = 1.0f;
}
static void hs_take(hitshade_t *h, float grain, uint32_t *rng)   /* the pending shading, then GRAIN (0..1) */
{
    float lps = h->lps;
    *h = hs_pend;
    h->lps = lps;                                   /* (a retriggered voice keeps its filter's state) */
    if (!hs_pend.on)
        hs_clear(h);
    if (grain > 0.0f) {                             /* each hit a little different: tuning, weight, brightness, length */
        float d = xs_uni(rng), dark = d * d * grain, sh = xs_uni(rng);   /* (most hits a little, a few a lot) */
        h->on = 1;
        h->semi += xs_bi(rng) * grain * 0.5f;
        h->gain *= 1.0f + xs_bi(rng) * grain * 0.3f;
        h->lpc = fm_minf(h->lpc, 1.0f - fm_expf(-6.2831853f * 18000.0f * fm_exp2f(-dark * 3.0f) * RV_DT));
        h->dk += sh * sh * grain * 10.0f;
    }
}
/* a ratchet repeat's R MOVE: UP and DOWN move a kit lane's pitch (a pitched track's notes move in the
 * scale instead); DARK closes each repeat down, quieter and shorter, so a burst turns from metal to wood */
static void hs_move(int rmv, int kit)
{
    int mv = rmv & 3, k = (rmv >> 2) & 7, cnt = ((rmv >> 5) & 7) + 1;
    float u = (float)k / (float)cnt;
    if (!mv || !k)
        return;
    hs_clear(&hs_pend);
    hs_pend.on = 1;
    if (mv == 3) {
        hs_pend.lpc = 1.0f - fm_expf(-6.2831853f * 14000.0f * fm_exp2f(-u * 5.5f) * RV_DT);
        hs_pend.dk = u * 30.0f;
        hs_pend.gain = 1.0f - 0.3f * u;
    } else if (kit) {
        hs_pend.semi = (mv == 1 ? 2.0f : -2.0f) * (float)k;
    } else {
        hs_pend.on = 0;
    }
}

/* a shaded voice's sound, rendered on its own, into the track */
RV_INL void hs_mix(hitshade_t *h, const float *src, float *dst, int n)
{
    int i;
    float e0 = h->env, e1 = h->env * (h->dk > 0.0f ? fm_expf(-(float)n * RV_DT * h->dk) : 1.0f), de = (e1 - e0) / (float)n;
    for (i = 0; i < n; i++) {
        h->lps += (src[i] - h->lps) * h->lpc;
        dst[i] += h->lps * (e0 + de * (float)i) * h->gain;
    }
    h->env = e1;
}

static void guard_note(void);
static int guard_wants(int t);
/* the voices' decaying state, flushed once a block before it reaches the denormals (fm_flush_n):
 * filters, envelopes and above all the ringing modes of TINE, PHYS, the strikes and the drums */
RV_INL void synth_flush(voice_t *v)
{
    fm_flush_n(v->lad.s, 4);
    fm_flush_n(&v->svf.ic1, 2);
    fm_flush_n(&v->svf2.ic1, 2);
    fm_flush_n(&v->zp.s[0][0], 12);
    fm_flush_n(&v->hs.lps, 1);
    switch (v->e_eng - 1) {                           /* the engine its state belongs to (a step lock may change the track's) */
    case ENG_TINE:
        fm_flush_n(v->e.tn.re, TN_M);
        fm_flush_n(v->e.tn.im, TN_M);
        fm_flush_n(&v->e.tn.box[0].ic1, 2);
        fm_flush_n(&v->e.tn.box[1].ic1, 2);
        fm_flush_n(&v->e.tn.blp, 1);
        fm_flush_n(&v->e.tn.thlp, 1);
        break;
    case ENG_PHYS:
        fm_flush_n(&v->e.ph.mode_s[0][0], 12);
        fm_flush_n(&v->e.ph.lp, 6);
        break;
    case ENG_FORMANT:
        fm_flush_n(&v->e.fo.f[0].ic1, 2);
        fm_flush_n(&v->e.fo.f[1].ic1, 2);
        fm_flush_n(&v->e.fo.f[2].ic1, 2);
        break;
    default:
        break;
    }
}
RV_INL void strike_flush(strike_t *v)
{
    fm_flush_n(v->md_y1, 8);
    fm_flush_n(v->md_y2, 8);
    fm_flush_n(&v->ping.ic1, 2);
    fm_flush_n(&v->exc_env, 3);
    fm_flush_n(&v->lp1, 2);
    fm_flush_n(&v->hs.lps, 1);
}
RV_INL void perc_flush(perc_t *v)
{
    fm_flush_n(v->my1, 8);
    fm_flush_n(v->my2, 8);
    fm_flush_n(&v->aenv, 5);
    fm_flush_n(&v->fbv, 4);
    fm_flush_n(&v->f1.ic1, 2);
    fm_flush_n(&v->f2.ic1, 2);
    fm_flush_n(&v->hpf.ic1, 2);
    fm_flush_n(&v->lpf.ic1, 2);
    fm_flush_n(&v->eq.ic1, 2);
    fm_flush_n(&v->hs.lps, 1);
}

static void note_on_internal(int t, int note, int vel, int env, uint16_t chain)
{
    track_t *tr = &trk[t];
    int kind = rv.tp[t][TP_KIND], i, spark = spark_pend;
    float v = (float)vel * (1.0f / 127.0f), grain = rv.tp[t][TP_GRAIN] * (1.0f / 127.0f);
    if (kind == KIND_COMP)                          /* COMP plays no notes */
        return;
    if (!arp_out && !spark && vel > 0 && arp_on(t)) {   /* ARP: the arpeggiator holds it and plays it in its turn */
        arp_key(t, note, vel, 1);
        hs_clear(&hs_pend);
        return;
    }
    if (!arp_out && arp_live_in && !spark && vel > 0 && hold_on(t))   /* HOLD: the key's note plays, and stays */
        hold_key_on(t, note);
    spark_pend = 0;
    if (vel > 0 && !spark && guard_wants(t))     /* (only a note that takes a voice of its own) */
        guard_note();
    if (vel > 0 && !spark)
        lfo_note_trig = 1;
    if (vel > 0 && !spark && !(kind == KIND_PERC && rv.tp[t][TP_ENGINE] == 1)) {   /* STEP ENV: the step's envelope, else the note's (a kit's env bits are accents) */
        int e = env > 0 ? env : rv.tp[t][TP_SEDEF];
        if (e > 0)
            senv_trigger(t, e - 1);
    }
    if (kind == KIND_SYNTH) {
        int poly = clampi(rv.tp[t][TP_POLY], 0, 3) + 1, count = 0, pick = -1;
        uint32_t oldest = 0xFFFFFFFFu;
        voice_t *vo;
        int legato = 0;
        if (poly == 1) {                            /* mono: the track's one voice, legato while held */
            for (i = 0; i < RV_SYNTH_VOICES; i++)
                if (vs[i].on && vs[i].track == t) {
                    if (pick < 0)
                        pick = i;
                    else
                        vs[i].on = 0;
                }
            if (pick >= 0)
                legato = vs[pick].a.gate && (rv.tp[t][TP_ENGINE] == ENG_MONO ? rv.tp[t][TP_S14] == 0   /* MONO: held notes slur */
                                                                              : rv.tp[t][TP_GLIDE] > 0);
        } else {
            for (i = 0; i < RV_SYNTH_VOICES; i++)
                if (vs[i].on && vs[i].track == t) {
                    count++;
                    if (vs[i].age < oldest) {
                        oldest = vs[i].age;
                        pick = i;
                    }
                }
            if (count < poly) {
                pick = -1;
                for (i = 0; i < RV_SYNTH_VOICES && pick < 0; i++)
                    if (!vs[i].on)
                        pick = i;
                if (pick < 0) {                       /* steal: a released voice first, else the oldest */
                    oldest = 0xFFFFFFFFu;
                    for (i = 0; i < RV_SYNTH_VOICES; i++)
                        if (!vs[i].a.gate && vs[i].age < oldest) {
                            oldest = vs[i].age;
                            pick = i;
                        }
                    if (pick < 0)
                        for (i = 0; i < RV_SYNTH_VOICES; i++)
                            if (vs[i].age < oldest) {
                                oldest = vs[i].age;
                                pick = i;
                            }
                }
            }
        }
        if (pick < 0) {
            for (i = 0; i < RV_SYNTH_VOICES && pick < 0; i++)
                if (!vs[i].on)
                    pick = i;
            if (pick < 0)
                pick = 0;
        }
        vo = &vs[pick];
        if (vo->on && vo->track != t)
            vo->on = 0;
        vo->rng = xs32(&eng_rng);
        vo->track = (uint8_t)t;
        vo->age = ++age_ctr;
        synth_voice_note_on(vo, note, v, legato, clampi(rv.tp[t][TP_ENGINE], 0, ENG_N - 1));
        hs_take(&vo->hs, 0.0f, &eng_rng);
        RV_FAR(artic_cfg_track)(&vo->cfg, tr->eff);
        if (!legato) {
            artic_start(&vo->a, &vo->cfg);
            fenv_start(&vo->fe);
        } else
            vo->a.gate = 1;
        if (rv.tp[t][TP_ENGINE] == ENG_PHYS)
            phys_strike(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_CHIP)
            chip_note(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_CZ)
            cz_note(vo, tr->eff, t);
        else if (rv.tp[t][TP_ENGINE] == ENG_WAVE)
            wave_note(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_LOGIC)
            logic_note(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_ORGAN)
            organ_note(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_RESIN)
            resin_note(vo, tr->eff, legato);
        else if (rv.tp[t][TP_ENGINE] == ENG_TINE)
            tine_note(vo, tr->eff);
        else if (rv.tp[t][TP_ENGINE] == ENG_MONO)
            mono_note(vo, tr->eff, legato);
    } else if (kind == KIND_STRIKE) {
        int poly = clampi(rv.tp[t][TP_POLY], 0, 1) + 1, count = 0, pick = -1;
        uint32_t oldest = 0xFFFFFFFFu;
        strike_t *sv;
        for (i = 0; i < RV_STRIKE_VOICES; i++)
            if (ss[i].on && ss[i].track == t) {
                count++;
                if (ss[i].age < oldest) {
                    oldest = ss[i].age;
                    pick = i;
                }
            }
        if (count < poly) {
            pick = -1;
            for (i = 0; i < RV_STRIKE_VOICES && pick < 0; i++)
                if (!ss[i].on)
                    pick = i;
            if (pick < 0) {
                oldest = 0xFFFFFFFFu;
                for (i = 0; i < RV_STRIKE_VOICES; i++)
                    if (ss[i].age < oldest) {
                        oldest = ss[i].age;
                        pick = i;
                    }
            }
        }
        if (pick < 0)
            pick = 0;
        sv = &ss[pick];
        sv->track = (uint8_t)t;
        sv->age = ++age_ctr;
        sv->rng = xs32(&eng_rng) | 1u;
        sv->gate = 1;
        RV_FAR(artic_cfg_track)(&sv->cfg, tr->eff);
        if (!sv->on)
            sv->a.p = sv->a.s = sv->a.v = 0.0f;
        strike_note_on(sv, note, v, tr->eff);
        sv->spark = (uint8_t)spark;
        hs_take(&sv->hs, grain, &eng_rng);
        artic_start(&sv->a, &sv->cfg);
    } else if (kind == KIND_PERC) {
        int kit = rv.tp[t][TP_ENGINE] == 1, lane = kit ? perc_lane_of(note) : -1;
        int poly = kit ? 1 : clampi(rv.tp[t][TP_POLY], 0, 1) + 1, count = 0, pick = -1;
        uint32_t oldest = 0xFFFFFFFFu;
        for (i = 0; i < RV_PERC_VOICES; i++)          /* a drum chokes its own oldest hit past POLY (a kit: each lane its last) */
            if (ps[i].on && ps[i].track == t && ps[i].lane == lane) {
                count++;
                if (ps[i].age < oldest) {
                    oldest = ps[i].age;
                    pick = i;
                }
            }
        if (count < poly) {
            pick = -1;
            for (i = 0; i < RV_PERC_VOICES && pick < 0; i++)
                if (!ps[i].on)
                    pick = i;
            if (pick < 0) {
                oldest = 0xFFFFFFFFu;
                for (i = 0; i < RV_PERC_VOICES; i++)
                    if (ps[i].age < oldest) {
                        oldest = ps[i].age;
                        pick = i;
                    }
            }
        }
        if (pick < 0)
            pick = 0;
        ps[pick].track = (uint8_t)t;
        ps[pick].age = ++age_ctr;
        perc_note_on(&ps[pick], note, v, tr->eff, xs32(&eng_rng) | 1u, kit ? rv.kit[t].mach[lane] : -1, lane);
        hs_take(&ps[pick].hs, grain, &eng_rng);
    }
    hs_clear(&hs_pend);
    if (spark)                                       /* a tap on the body: not a note the track played */
        return;
    tr->lead_vel = v;
    tr->lead_key = (float)(note - 60);
    rv_live.hit[t]++;
    infl_fire(t, note, vel, chain, now_s);
}

static void note_off_internal(int t, int note)
{
    int i;
    int kind = rv.tp[t][TP_KIND];
    if (!arp_out && arp_on(t) && arp_holds(t, note)) {
        arp_key(t, note, 0, 0);
        return;
    }
    if (!arp_out && arp_live_in && hold_on(t) && hold_has(t, note)) {   /* HOLD: the key let go, the note sounds on */
        if (hold[t].down)
            hold[t].down--;
        return;
    }
    if (kind == KIND_SYNTH) {
        for (i = 0; i < RV_SYNTH_VOICES; i++)
            if (vs[i].on && vs[i].track == t && vs[i].note == note && vs[i].a.gate) {
                artic_release(&vs[i].a, &vs[i].cfg);
                fenv_release(&vs[i].fe);
            }
    } else if (kind == KIND_STRIKE) {
        for (i = 0; i < RV_STRIKE_VOICES; i++)
            if (ss[i].on && ss[i].track == t && ss[i].note == note && ss[i].gate) {
                ss[i].gate = 0;
                artic_release(&ss[i].a, &ss[i].cfg);
            }
    } else if (kind == KIND_PERC) {
        for (i = 0; i < RV_PERC_VOICES; i++)          /* drums play out; the gate only matters to HOLD */
            if (ps[i].on && ps[i].track == t && ps[i].note == note)
                ps[i].gate = 0;
    }
}

static void all_off_now(void)
{
    int i;
    for (i = 0; i < RV_SYNTH_VOICES; i++)
        if (vs[i].on) {
            artic_release(&vs[i].a, &vs[i].cfg);
            fenv_release(&vs[i].fe);
        }
    for (i = 0; i < RV_STRIKE_VOICES; i++)
        if (ss[i].on) {
            ss[i].gate = 0;
            artic_release(&ss[i].a, &ss[i].cfg);
        }
    for (i = 0; i < RV_PERC_VOICES; i++)
        ps[i].gate = 0;
}

static void cmd_drain(void)
{
    while (cq_r != cq_w) {
        cmd_t c = cq[cq_r & 127u];
        cq_r++;
        switch (c.op) {
        case C_NOTE:
            if (c.c > 0)
                note_on_internal(c.a & 7, c.b, c.c, 0, (uint16_t)(1u << (c.a & 7)));
            else
                note_off_internal(c.a & 7, c.b);
            break;
        case C_LIVE:
            arp_live_in = 1;
            if (c.c > 0)
                note_on_internal(c.a & 7, c.b, c.c, 0, (uint16_t)(1u << (c.a & 7)));
            else
                note_off_internal(c.a & 7, c.b);
            arp_live_in = 0;
            record_note(c.a & 7, c.b, c.c);
            break;
        case C_TRANSPORT:
            if (c.b && !playing)
                seq_start();
            else if (!c.b && playing) {
                seq_stop();
                __builtin_memset(senv, 0, sizeof senv);   /* STEP ENV: stopped, every parameter back where its knob is */
            }
            rv_live.playing = playing;
            break;
        case C_PUNCH:
            punch[c.a & 7] = (uint8_t)c.b;
            if (c.a == 0 && c.b)
                cut_now(1 | (rv.gp[GP_CUTSHAPE] << 2), 0.0f);
            if (c.a == 1 && c.b)
                cut_now(2 | (rv.gp[GP_CUTSHAPE] << 2), 0.0f);
            if (c.a == 2 && c.b)
                cut_now(3 | (rv.gp[GP_CUTSHAPE] << 2), 0.0f);
            dly_freeze = punch[3];
            fill_on = punch[7];
            break;
        case C_CUT:
            cut_now(c.b, 0.0f);
            break;
        case C_SCENE:
            scene_launch_now(c.b);
            break;
        case C_PATTERN:
            pattern_launch_now(c.a & 7, c.b);
            break;
        case C_TAP:
            tap_tempo_now();
            break;
        case C_ARM:
            record_arm_now(c.a & 7, c.b);
            break;
        case C_MOTION:
            motion_now(c.b, c.c);
            break;
        case C_ALLOFF:
            all_off_now();
            arp_reset();
            break;
        default:
            break;
        }
    }
}

/* --------------------------------------------------- the track chain --- */
static inline float skin(track_t *tr, float x, float drive, float fld, int bits, float derez, int mask)
{
    if (drive > 0.01f)
        x = sat(x * (1.0f + drive * 9.0f)) * (1.0f / (1.0f + drive * 1.5f));
    if (fld > 0.01f)
        x = fold(x, fld);
    if (derez > 0.01f) {                            /* rate reduction: hold for 1..48 samples */
        tr->derez_ph += 1.0f;
        if (tr->derez_ph >= 1.0f + derez * derez * 47.0f) {
            tr->derez_ph = 0.0f;
            tr->derez_hold = x;
        }
        x = tr->derez_hold;
    }
    if (bits > 0) {                                 /* bit reduction: 16 .. 1 bits */
        float q = fm_exp2f(15.0f - (float)bits * (14.0f / 127.0f));
        x = (float)(int32_t)(x * q) / q;
    }
    if (mask > 0) {                                 /* Leibniz-style bit masking on 8 bits */
        int v8 = (int)clampf(x * 127.0f, -128.0f, 127.0f);
        static const uint8_t masks[8] = {0xFE, 0xFC, 0xF5, 0xEA, 0xD5, 0xAA, 0x92, 0x81};
        v8 = (v8 & 0x80) | ((v8 & 0x7F) & masks[clampi(mask / 16, 0, 7)]);
        x = (float)(int8_t)(uint8_t)v8 * (1.0f / 127.0f);
    }
    return x;
}

/* the Juno-60's chorus: a BBD swept by a triangle. I: 0.513 Hz, II: 0.863 Hz over 1.66-5.35 ms;
 * I+II: 9.75 Hz over 3.3-3.7 ms. The wet path is darkened like a BBD's anti-alias filters. */
static inline float chorus_tick(track_t *tr, float x, int mode)
{
    static const float hz[4] = {0.0f, 0.513f, 0.863f, 9.75f}, lo[4] = {0.0f, 1.66f, 1.66f, 3.3f}, hi[4] = {0.0f, 5.35f, 5.35f, 3.7f};
    float tri, ms, d, fr, a, b;
    int32_t di, r0;
    if ((uint32_t)tr->ch_w >= 256u)
        tr->ch_w = 0;
    tr->ch_buf[tr->ch_w] = (int16_t)clampf(x * 16000.0f, -32000.0f, 32000.0f);
    tr->ch_ph += hz[mode] * RV_DT;
    tr->ch_ph -= fm_floorf(tr->ch_ph);
    tri = tr->ch_ph < 0.5f ? tr->ch_ph * 2.0f : 2.0f - tr->ch_ph * 2.0f;
    ms = lo[mode] + (hi[mode] - lo[mode]) * tri;
    d = ms * 0.001f * RV_FS;
    di = (int32_t)d;
    fr = d - (float)di;
    r0 = (tr->ch_w - di) & 255;
    a = (float)tr->ch_buf[r0];
    b = (float)tr->ch_buf[(r0 - 1) & 255];
    tr->ch_w = (tr->ch_w + 1) & 255;
    tr->ch_lp += ((a + (b - a) * fr) * (1.0f / 16000.0f) - tr->ch_lp) * 0.62f;   /* about 8 kHz */
    return tr->ch_lp;
}

static inline float insert_tick(track_t *tr, int ins, float x, float a, float b)
{
    int k;
    switch (ins) {
    case INS_COMB: {
        /* TUNE in semitones, 64 = C4 (so KEY at +63 keeps it in unison with the keys); the
         * buffer reaches down to about F3. 0.42: the loop's smoothing delays it */
        float fr = 261.6256f * fm_exp2f((a * 127.0f - 64.0f) * (1.0f / 12.0f)), d = clampf(44100.0f / fr - 0.42f, 2.0f, 253.0f), z, y, fd;
        int32_t di = (int32_t)d, i0 = tr->comb_w - di, i1;
        fd = d - (float)di;                              /* fractional delay: in tune up high */
        if (i0 < 0)
            i0 += 256;
        i1 = (i0 - 1) & 255;
        z = (tr->comb[i0] + (tr->comb[i1] - tr->comb[i0]) * fd) * (1.0f / 16384.0f);
        tr->comb_lp += (z - tr->comb_lp) * 0.7f;
        y = x + tr->comb_lp * b * 0.96f;
        tr->comb[tr->comb_w] = (int16_t)clampf(sat(y) * 16384.0f, -32000.0f, 32000.0f);
        tr->comb_w = (tr->comb_w + 1) & 255;
        return y * (1.0f - b * 0.4f);
    }
    case INS_SHIFT: {
        float c = (a - 0.5f) * 2.0f, ac = fm_fabsf(c), hz = 0.0f, ai = x, aq = x, s;
        if (ac > 0.03f) {
            hz = 0.5f * fm_powf(400.0f, (ac - 0.03f) / 0.97f);
            if (c < 0.0f)
                hz = -hz;
        }
        for (k = 0; k < 4; k++) {
            ai = ap2(&tr->hilb_i[k * 4], hil_ci[k], ai);
            aq = ap2(&tr->hilb_q[k * 4], hil_cq[k], aq);
        }
        s = tr->sh_z;
        tr->sh_z = ai;
        tr->sh_ph += hz * RV_DT;
        tr->sh_ph -= fm_floorf(tr->sh_ph);
        return lerpf(x, s * sin_lut(tr->sh_ph + 0.25f) + aq * sin_lut(tr->sh_ph), 0.5f + b * 0.5f);
    }
    case INS_CRUSH: {
        int hold = 1 + (int)(a * a * 40.0f);
        float q = fm_exp2f(14.0f - b * 12.0f);
        if (++tr->crush_n >= hold) {
            tr->crush_n = 0;
            tr->crush_hold = x;
        }
        return (float)(int32_t)(tr->crush_hold * q) / q;
    }
    case INS_SMEAR: {
        static const int lens[4] = {29, 53, 83, 113}, base[4] = {0, 32, 88, 174};
        float g = b * 0.75f, y = x;
        for (k = 0; k < 4; k++) {
            int L = clampi((int)((float)lens[k] * (0.3f + a * 0.7f)), 2, lens[k] - 1);
            int sz = k == 3 ? 116 : base[k + 1] - base[k];
            int ri = (tr->sm_i[k] - L + sz) % sz;
            float z = tr->smear[base[k] + ri] * (1.0f / 16384.0f), v = y + z * g;
            tr->smear[base[k] + tr->sm_i[k]] = (int16_t)clampf(v * 16384.0f, -32000.0f, 32000.0f);
            y = z - v * g;
            tr->sm_i[k] = (tr->sm_i[k] + 1) % sz;
        }
        return y;
    }
    default:
        return x;
    }
}

/* ------------------------------------------------------------- idling --- */
/* a bus (or a track's chain) with nothing coming in and nothing left ringing is skipped: its input
 * and its own output have both been silent for a while (BUS_HOLD control steps, about 70 ms).
 * A frozen delay or an endless reverb never falls silent, so they keep running */
#define BUS_HOLD (96u * 32u / RV_SUB)   /* (about 70 ms of steps) */
enum { BUS_FB, BUS_DLY, BUS_SPACE, BUS_N };
static uint16_t bus_quiet[BUS_N];
static float peak_of(const float *b, int n)
{
    float m = 0.0f;
    int i;
    for (i = 0; i < n; i++)
        if (fm_fabsf(b[i]) > m)
            m = fm_fabsf(b[i]);
    return m;
}
static int bus_skip(int k, const float *in, int n)
{
    if (peak_of(in, n) > 1e-7f) {
        bus_quiet[k] = 0;
        return 0;
    }
    return bus_quiet[k] >= BUS_HOLD;
}
static void bus_note(int k, float out_peak)
{
    if (out_peak < 1e-6f) {
        if (bus_quiet[k] < 0xFFFFu)
            bus_quiet[k]++;
    } else {
        bus_quiet[k] = 0;
    }
}
/* a stereo bus run on its own, so its output can be measured, then added */
static void bus_stereo(int k, void (*run)(const float *, int, float *, float *), const float *in, int n, float *l, float *r)
{
    float bl[RV_SUB], br[RV_SUB];
    int i;
    if (bus_skip(k, in, n))
        return;
    for (i = 0; i < n; i++)
        bl[i] = br[i] = 0.0f;
    run(in, n, bl, br);
    bus_note(k, fm_maxf(peak_of(bl, n), peak_of(br, n)));
    for (i = 0; i < n; i++) {
        l[i] += bl[i];
        r[i] += br[i];
    }
}

/* ------------------------------------------------------------ profile --- */
/* GLO > CPU: where the render's time goes, part by part, as a share of the time it has (the
 * platform's cycle counter; none in the browser). Measured a quarter second at a time, smoothed */
enum { PF_CTRL, PF_SYNTH, PF_STRIKE, PF_PERC, PF_CHAIN, PF_BED, PF_FB, PF_DLY, PF_SPACE, PF_MASTER, PF_N };   /* (GLO > CPU names them in this order) */
static uint32_t prof_acc[PF_N], prof_t, prof_n, prof_fetch, prof_rcyc;
static uint32_t c2_acc, c2_wacc;                          /* the second core's work, and this one's waiting for it (cycles) */
#define PROF(k) do { uint32_t c_ = plat_cycles(); prof_acc[k] += c_ - prof_t; prof_t = c_; } while (0)
static void prof_end(int n)
{
    int k;
    prof_n += (uint32_t)n;
    if (prof_n < (uint32_t)RV_FS / 4u)
        return;
    {
        float budget = (float)plat_cycles_hz() / (float)RV_FS * (float)prof_n;   /* the cycles these samples had */
        for (k = 0; k < PF_N; k++) {
            float pct = budget > 0.0f ? (float)prof_acc[k] * 100.0f / budget : 0.0f;
            rv_live.prof[k] += (pct - rv_live.prof[k]) * 0.5f;
            prof_acc[k] = 0;
        }
        rv_live.c2_load += ((budget > 0.0f ? (float)c2_acc * 100.0f / budget : 0.0f) - rv_live.c2_load) * 0.5f;
        rv_live.c2_wait += ((budget > 0.0f ? (float)c2_wacc * 100.0f / budget : 0.0f) - rv_live.c2_wait) * 0.5f;
        c2_acc = c2_wacc = 0;
    }
    if (rv_live.fetch_on && prof_rcyc)                /* FLASH WAIT: the share of the render spent fetching code */
        rv_live.fetch_pct += ((float)prof_fetch * 100.0f / (float)prof_rcyc - rv_live.fetch_pct) * 0.5f;
    prof_fetch = prof_rcyc = 0;
    prof_n = 0;
}

/* ------------------------------------------------------------ two cores --- */
/* With the second core on (GLO > CPU, CORES 2) it takes every other sounding voice, synth, strike or
 * drum, in their order (so each engine's voices are shared out, and the two cores read the same code
 * through the cache they share), up to a number that keeps it from holding this one up; then the
 * buses (below). It plays its voices into buffers of its own, added to the tracks after; the faults,
 * the counts and the screen's envelopes are all done here, after both, in the voices' order. Off,
 * or failed: all here, as before. */
enum { VK_SYNTH, VK_STRIKE, VK_PERC };
#define VL_MAX (RV_SYNTH_VOICES + RV_STRIKE_VOICES + RV_PERC_VOICES)
static uint8_t vl_kind[VL_MAX], vl_idx[VL_MAX], vl_c2[VL_MAX], vl_bad[VL_MAX];
static float vl_lv[VL_MAX];
static int vl_n, par_n;
static float c2_buf[RV_TRACKS][RV_SUB] RV_RAMBSS;    /* the second core's voices, by track (plain RAM: the pool keeps its room) */
static uint8_t c2_trk;                                /* the tracks it plays into this step */
static uint8_t c2_vcap = VL_MAX;                      /* at most this many voices to it (it adapts) */
static uint32_t c2_vcyc[3];                           /* its voices' cycles, by kind */
static uint8_t c2_voices_given;                       /* how many it had this step */

static int vl_track(int e)
{
    int k = vl_idx[e];
    return (vl_kind[e] == VK_SYNTH ? vs[k].track : vl_kind[e] == VK_STRIKE ? ss[k].track : ps[k].track) & 7;
}

static int vl_sortkey(int e)
{
    int k = vl_idx[e];
    return vl_kind[e] == VK_SYNTH ? vs[k].e_eng * 16 + k : vl_kind[e] == VK_PERC ? ps[k].mach * 16 + k : k;
}

/* one voice of the list, added into out; its level and whether it went bad are kept for the count */
RV_HOT static void voice_one(int e, float *out, int n)
{
    int k = vl_idx[e];
    float lv, tmp[RV_SUB];
    switch (vl_kind[e]) {
    case VK_SYNTH: {
        voice_t *v = &vs[k];
        track_t *tr = &trk[v->track];
        if (v->hs.on) {                              /* a shaded hit: on its own, then into the track */
            zero_buf(tmp, n);
            lv = RV_FAR(synth_voice_render)(v, tr, tmp, n, 0.0f);
            hs_mix(&v->hs, tmp, out, n);
        } else
            lv = RV_FAR(synth_voice_render)(v, tr, out, n, 0.0f);
        vl_bad[e] = (uint8_t)(buf_bad(out, n) || !(lv == lv));
        if (!vl_bad[e])
            synth_flush(v);
        break;
    }
    case VK_STRIKE: {
        strike_t *v = &ss[k];
        track_t *tr = &trk[v->track];
        if (v->hs.on) {
            zero_buf(tmp, n);
            lv = RV_FAR(strike_render)(v, tr, tmp, n);
            hs_mix(&v->hs, tmp, out, n);
        } else
            lv = RV_FAR(strike_render)(v, tr, out, n);
        vl_bad[e] = (uint8_t)(buf_bad(out, n) || !(lv == lv));
        if (!vl_bad[e])
            strike_flush(v);
        break;
    }
    default: {
        perc_t *v = &ps[k];
        track_t *tr = &trk[v->track];
        if (v->hs.on) {
            zero_buf(tmp, n);
            lv = RV_FAR(perc_render)(v, tr, tmp, n);
            hs_mix(&v->hs, tmp, out, n);
        } else
            lv = RV_FAR(perc_render)(v, tr, out, n);
        vl_bad[e] = (uint8_t)(buf_bad(out, n) || !(lv == lv));
        if (!vl_bad[e])
            perc_flush(v);
        break;
    }
    }
    if (vl_bad[e])
        zero_buf(out, n);
    vl_lv[e] = lv;
}

/* the second core's voices */
static void c2_voices(void)
{
    int e;
    uint32_t c = plat_par_cycles(), c1;
    for (e = 0; e < vl_n; e++)
        if (vl_c2[e]) {
            RV_FAR(voice_one)(e, c2_buf[vl_track(e)], par_n);
            c1 = plat_par_cycles();
            c2_vcyc[vl_kind[e]] += c1 - c;
            c = c1;
        }
}

/* after both cores: faults, the counts, the leading voice's envelopes for the screen */
static void voices_count(uint32_t *lead_age)
{
    int e;
    for (e = 0; e < vl_n; e++) {
        int k = vl_idx[e];
        float lv = vl_lv[e];
        switch (vl_kind[e]) {
        case VK_SYNTH: {
            voice_t *v = &vs[k];
            if (vl_bad[e]) {
                fault(100 + rv.tp[v->track][TP_ENGINE]);
                __builtin_memset(v, 0, sizeof(*v));
                break;
            }
            rv_live.voices_synth++;
            if (v->age >= lead_age[v->track]) {      /* the newest voice leads */
                lead_age[v->track] = v->age;
                trk[v->track].lead_env = lv;
                rv_live.stage[v->track] = v->a.stage;
                rv_live.stage_pos[v->track] = v->a.u;
                rv_live.fenv[v->track] = v->fe.lvl;
                rv_live.fenv_pos[v->track] = v->fe.u;
                rv_live.fenv_stage[v->track] = v->fe.stage;
            }
            break;
        }
        case VK_STRIKE: {
            strike_t *v = &ss[k];
            if (vl_bad[e]) {
                fault(200 + rv.tp[v->track][TP_S0]);
                __builtin_memset(v, 0, sizeof(*v));
                break;
            }
            rv_live.voices_strike++;
            trk[v->track].lead_env = lv;
            rv_live.stage[v->track] = v->a.stage;
            rv_live.stage_pos[v->track] = v->a.u;
            break;
        }
        default: {
            perc_t *v = &ps[k];
            if (vl_bad[e]) {
                fault(300 + rv.tp[v->track][TP_S0]);
                __builtin_memset(v, 0, sizeof(*v));
                break;
            }
            rv_live.voices_strike++;
            if (v->age >= lead_age[v->track]) {
                lead_age[v->track] = v->age;
                trk[v->track].lead_env = lv;
                rv_live.stage[v->track] = AS_CONT;
                rv_live.stage_pos[v->track] = 0.0f;
            }
            break;
        }
        }
    }
}

/* ------------------------------------------------------- the buses' job --- */
/* The buses (the BED, the feedback loop, the delay, the space) play one control step behind the
 * tracks: each step they take what the tracks sent them the step before (about 3 ms at 44.1 kHz).
 * So the second core can run them while this one plays the voices and the tracks' chains; their
 * code is in the chip's RAM (RV_HOT), so the two cores don't fight over the cache for it. With one
 * core they run here, at the same point in the sound, so it is the same either way. */
#define BX_LEN (2 * RV_SUB)
static float bx_fb[BX_LEN], bx_dly[BX_LEN], bx_rev[BX_LEN];   /* the tracks' sends, a ring */
static int bx_w;                                           /* where this step's go */
static float bx_act;                                       /* how loud the tracks were (the BED ducks) */
static float bo_l[RV_SUB], bo_r[RV_SUB], bo_fb[RV_SUB];   /* the buses' output this step */
static float bo_act;
static int bo_n, bo_fault;
static uint32_t bo_cyc[4], bo_all;                        /* BED, FEEDBACK, DELAY, SPACE; and in all */
static volatile uint8_t par_vdone;                       /* the second core's synth voices are done */
static uint8_t par_job;                                   /* it has this step's job */

static float bj_fb[RV_SUB] RV_RAMBSS, bj_dly[RV_SUB] RV_RAMBSS, bj_rev[RV_SUB] RV_RAMBSS;   /* (off the stack: the second core's is small) */
static __attribute__((noinline)) void bus_job(void)
{
    float *fin = bj_fb, *din = bj_dly, *rin = bj_rev;
    int i, n = bo_n, j = bx_w - RV_SUB;
    uint32_t c0 = plat_par_cycles(), c = c0, c1;
    if (j < 0)
        j += BX_LEN;
    for (i = 0; i < n; i++) {
        fin[i] = bx_fb[j];
        din[i] = bx_dly[j];
        rin[i] = bx_rev[j];
        bo_l[i] = bo_r[i] = 0.0f;
        if (++j >= BX_LEN)
            j = 0;
    }
    bo_fault = 0;
    RV_FAR(bed_run)(rin, bo_l, bo_r, n, bo_act);      /* the BED, under the tracks (it ducks under them) */
    if (buf_fix(bo_l, n) | buf_fix(bo_r, n))
        bo_fault = 950;
    c1 = plat_par_cycles();
    bo_cyc[0] += c1 - c;
    c = c1;
    if (bus_skip(BUS_FB, fin, n)) {
        for (i = 0; i < n; i++)
            bo_fb[i] = 0.0f;
    } else {
        RV_FAR(feedback_run)(fin, n, bo_fb);
        bus_note(BUS_FB, peak_of(bo_fb, n));
    }
    if (buf_fix(bo_fb, n) && !bo_fault)
        bo_fault = 600;
    for (i = 0; i < n; i++) {
        bo_l[i] += bo_fb[i];
        bo_r[i] += bo_fb[i];
    }
    c1 = plat_par_cycles();
    bo_cyc[1] += c1 - c;
    c = c1;
    if (dly_freeze || rv.gp[GP_DLYFREEZE])            /* frozen, it plays on with nothing coming in */
        bus_quiet[BUS_DLY] = 0;
    bus_stereo(BUS_DLY, delay_run, din, n, bo_l, bo_r);
    if ((buf_fix(bo_l, n) | buf_fix(bo_r, n)) && !bo_fault)
        bo_fault = 800;
    c1 = plat_par_cycles();
    bo_cyc[2] += c1 - c;
    c = c1;
    bus_stereo(BUS_SPACE, halfrate_run, rin, n, bo_l, bo_r);
    if ((buf_fix(bo_l, n) | buf_fix(bo_r, n)) && !bo_fault)
        bo_fault = 900;
    c1 = plat_par_cycles();
    bo_cyc[3] += c1 - c;
    bo_all += c1 - c0;
}

/* the second core's work for a step: its voices (if any), then the buses */
static void c2_job(uint32_t voices)
{
    if (voices) {
        c2_voices();
        plat_par_sync();
        par_vdone = 1;
        plat_par_sync();
    }
    bus_job();
}

/* this core waits for the second's voices: 0 = they are done; < 0 = it failed (and is off now:
 * its voices are to be played here) */
static int par_voices_wait(void)
{
    uint32_t t0 = plat_par_cycles();
    while (!par_vdone) {
        plat_par_sync();
        if (plat_par_cycles() - t0 > plat_cycles_hz() / 125u)   /* 8 ms: far past any step */
            return plat_par_wait() < 0 ? -1 : 0;   /* (done after all, or failed) */
    }
    plat_par_sync();
    return 0;
}

/* ------------------------------------------------------- overload guard --- */
/* When the render takes most of the time the audio has, the FM-1 falls behind and its output breaks
 * up into loud noise. The guard watches the render's own time and, past GUARD_HI, fades out the
 * voice that matters least (released ones first, then a note of the busiest track, then the oldest) over
 * about 3 ms, more than one when far over, and holds the voice count there: a new
 * note past the limit fades out the least needed to make room. Once the reading is clear of the edge
 * (under GUARD_LO for eight blocks running) the limit comes back up by one, and again. Where there is no clock (the browser)
 * it never acts. A voice being faded is marked by its shading's decay, SHED_DK */
#define GUARD_HI 0.80f                  /* (the interface needs the rest of the time) */
#define GUARD_LO 0.76f                  /* (below it, the limit rises a voice at a time) */
#define SHED_DK 300.0f                  /* about 3 ms (the Deluge's soft cull) */
#define GUARD_WIN 512u                  /* frames read at a time */
static float guard_load, guard_vc = 0.05f, guard_x, guard_last;   /* guard_x, guard_last: this window's reading, the one before */
static uint32_t guard_acc, guard_vacc, guard_n;
static uint16_t guard_cool, guard_calm;
static uint8_t guard_cap = 255;

static int shedding(const hitshade_t *h) { return h->on && h->dk >= SHED_DK; }
static void shed_start(hitshade_t *h)
{
    if (!h->on) {
        h->on = 1;
        h->semi = 0.0f;
        h->lpc = 1.0f;
        h->gain = 1.0f;
        h->env = 1.0f;
    }
    h->dk = SHED_DK;
}

static int voices_sounding(void)
{
    int k, c = 0;
    for (k = 0; k < RV_SYNTH_VOICES; k++)
        c += vs[k].on && !shedding(&vs[k].hs);
    for (k = 0; k < RV_STRIKE_VOICES; k++)
        c += ss[k].on && !shedding(&ss[k].hs);
    for (k = 0; k < RV_PERC_VOICES; k++)
        c += ps[k].on && !shedding(&ps[k].hs);
    return c;
}

/* the voice to fade out first starts fading (1: one did). Its order, after Synthstrom's Deluge: a
 * released voice before a held one; then one of the track that has the most voices sounding (a
 * chord loses a note before a lone line goes silent); then the oldest */
static int shed_one(void)
{
    hitshade_t *pick = 0;
    uint32_t best_age = 0;
    int k, best_rel = -1, best_cnt = -1, cnt[RV_TRACKS] = {0};
#define SHED_COUNT(v) \
    if ((v).on && !shedding(&(v).hs)) cnt[(v).track & 7]++;
    for (k = 0; k < RV_SYNTH_VOICES; k++)
        SHED_COUNT(vs[k])
    for (k = 0; k < RV_STRIKE_VOICES; k++)
        SHED_COUNT(ss[k])
    for (k = 0; k < RV_PERC_VOICES; k++)
        SHED_COUNT(ps[k])
#undef SHED_COUNT
#define SHED_CAND(v, held) \
    if ((v).on && !shedding(&(v).hs)) { \
        int rel_ = !(held), c_ = cnt[(v).track & 7]; \
        if (rel_ > best_rel || (rel_ == best_rel && (c_ > best_cnt || (c_ == best_cnt && (v).age < best_age)))) { \
            best_rel = rel_; best_cnt = c_; best_age = (v).age; pick = &(v).hs; } }
    for (k = 0; k < RV_SYNTH_VOICES; k++)
        SHED_CAND(vs[k], vs[k].a.gate)
    for (k = 0; k < RV_STRIKE_VOICES; k++)
        SHED_CAND(ss[k], ss[k].gate)
    for (k = 0; k < RV_PERC_VOICES; k++)
        SHED_CAND(ps[k], ps[k].gate)
#undef SHED_CAND
    if (!pick)
        return 0;
    shed_start(pick);
    rv_live.shed++;
    return 1;
}

/* a note about to start: past the guard's limit, the oldest voice makes room */
/* does a note on track t start a voice of its own (not a mono retrigger, a tape track's playhead
 * or a drum choking its own last hit)? */
static int guard_wants(int t)
{
    int kind = rv.tp[t][TP_KIND];
    if (kind == KIND_SYNTH)
        return rv.tp[t][TP_POLY] > 0;
    if (kind == KIND_PERC)
        return rv.tp[t][TP_ENGINE] != 1 && rv.tp[t][TP_POLY] > 0;
    return 1;
}
static void guard_note(void)
{
    if (guard_cap != 255 && !rv_live.guard_off && voices_sounding() >= guard_cap)
        shed_one();
}

static void guard_tick(uint32_t cyc, uint32_t vcyc, int n)   /* vcyc: the voices' part of cyc */
{
    float hz = (float)plat_cycles_hz(), x, vx;
    int k;
    /* faded out: off */
    for (k = 0; k < RV_SYNTH_VOICES; k++)
        if (vs[k].on && shedding(&vs[k].hs) && vs[k].hs.env < 0.001f)
            vs[k].on = 0;
    for (k = 0; k < RV_STRIKE_VOICES; k++)
        if (ss[k].on && shedding(&ss[k].hs) && ss[k].hs.env < 0.001f)
            ss[k].on = 0;
    for (k = 0; k < RV_PERC_VOICES; k++)
        if (ps[k].on && shedding(&ps[k].hs) && ps[k].hs.env < 0.001f)
            ps[k].on = 0;
    if (!cyc || hz <= 0.0f || n <= 0)
        return;                                     /* no clock here */
    /* read over the FM-1's audio block (512 frames, 11.6 ms), as it falls behind or not: one step
     * alone can be busy (a bar's events) and still fit */
    guard_acc += cyc;
    guard_vacc += vcyc;
    guard_n += (uint32_t)n;
    if (guard_n < GUARD_WIN)
        return;
    x = (float)guard_acc / (hz / (float)RV_FS * (float)guard_n);
    vx = (float)guard_vacc / (hz / (float)RV_FS * (float)guard_n);
    guard_acc = guard_vacc = guard_n = 0;
    guard_last = guard_x;
    guard_x = x;
    {
        int s = voices_sounding();                  /* what a voice costs, about */
        if (s > 0)
            guard_vc += (vx / (float)s - guard_vc) * 0.3f;
    }
    /* the reading rises at once and falls slowly (the Deluge's way): a sudden load, a chord on top of
     * the last one's releases, is met in the block it comes, not after the average catches up */
    guard_load = x > guard_load ? x : guard_load + (x - guard_load) * 0.25f;
    rv_live.load = guard_load * 100.0f;
    if (guard_cool)
        guard_cool--;
    if (rv_live.guard_off) {
        guard_cap = 255;
    } else if (guard_load > GUARD_HI) {
        guard_calm = 0;
        if ((!guard_cool || x > guard_last) && vx > 0.08f) {   /* (when the voices are what costs: fewer of them
                                                               * helps; again at once while it still rises) */
            int s = voices_sounding(), m = 1 + (int)((x - GUARD_HI) * 12.5f), shed = 0;   /* one more for each 8 % over */
            if (s > guard_cap)
                s = guard_cap;
            while (m-- > 0 && shed_one())
                shed++;
            if (shed) {
                guard_cap = (uint8_t)(s - shed > 2 ? s - shed : 2);
                guard_cool = 4;                     /* (let the fade show in the reading first: 46 ms) */
            }
        }
    } else if (guard_load < GUARD_LO) {             /* clear of the edge: the limit comes back up */
        if (guard_cap != 255 && ++guard_calm >= 8) {          /* (93 ms of it) */
            guard_calm = 0;
            if (++guard_cap >= RV_SYNTH_VOICES + RV_STRIKE_VOICES + RV_PERC_VOICES)
                guard_cap = 255;
        }
    } else {
        guard_calm = 0;
    }
    rv_live.cap = guard_cap;
}

/* the master's last stage, a sample at a time: no DC, the peak limiter at -1 dB (instant attack, about
 * 80 ms release), the metronome's blip, the meters (RAM code on the FM-1: it runs on every block) */
RV_HOT static void master_tail(float *outl, float *outr, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        float a;
        float yl = outl[i] - master_dc[0].x1 + 0.9995f * master_dc[0].y1;   /* DC blocker, about 3.5 Hz */
        float yr = outr[i] - master_dc[1].x1 + 0.9995f * master_dc[1].y1;
        master_dc[0].x1 = outl[i];
        master_dc[1].x1 = outr[i];
        master_dc[0].y1 = fm_flush(yl);
        master_dc[1].y1 = fm_flush(yr);
        outl[i] = yl;
        outr[i] = yr;
        a = fm_maxf(fm_fabsf(outl[i]), fm_fabsf(outr[i])) * lim_gain;
        if (a > 0.89f)
            lim_gain *= 0.89f / a;
        else
            lim_gain += (1.0f - lim_gain) * 0.0003f;
        outl[i] = clampf(outl[i] * lim_gain, -0.95f, 0.95f);
        outr[i] = clampf(outr[i] * lim_gain, -0.95f, 0.95f);
        if (click_armed && (int32_t)(now_s + (uint32_t)i - click_when) >= 0) {   /* the metronome: a short blip */
            click_armed = 0;
            click_env = click_amp;
            click_ph = 0.0f;
            click_hz = click_down ? 1600.0f : 1100.0f;
        }
        if (click_env > 0.0005f) {
            float c = sin_lut(click_ph) * click_env;
            click_ph += click_hz * RV_DT;
            click_ph -= fm_floorf(click_ph);
            click_env *= 0.9978f;
            outl[i] = clampf(outl[i] + c, -0.98f, 0.98f);
            outr[i] = clampf(outr[i] + c, -0.98f, 0.98f);
        }
        rv_live.master_l = fm_maxf(fm_fabsf(outl[i]), rv_live.master_l * 0.9999f);
        rv_live.master_r = fm_maxf(fm_fabsf(outr[i]), rv_live.master_r * 0.9999f);
    }
}


/* ------------------------------------------------- COMP: the bus compressor --- */
/* T8 as COMP: a glue compressor on the main mix (the tracks and the buses' returns, before the
 * master's drive), after the SSL G-series bus compressor: stereo-linked, feed-forward, a hard knee,
 * its stepped ratios, attacks and releases; AUTO releases in two stages (quickly after a short peak,
 * slowly out of a held squeeze). Cheap: the level is the peak of each 16 frames, the gain is worked
 * out in dB once per 16 and swept across them, a multiply a sample per side */
#define COMP_SUB 16
typedef struct { float thr, slope, aa, ar, aas, ars, mk, mix; int autor; } comp_cfg_t;
static float comp_gr, comp_grs, comp_g = 1.0f, comp_db = -120.0f;   /* reduction (dB: fast, AUTO's slow), the gain now */

RV_HOT static void comp_run(float *l, float *r, int n, const comp_cfg_t *c)
{
    int i, j;
    for (i = 0; i < n; i += COMP_SUB) {
        float pk = 0.0f, db, tgt, gr, g1, dg;
        int m = n - i < COMP_SUB ? n - i : COMP_SUB;
        for (j = 0; j < m; j++)
            pk = fm_maxf(pk, fm_maxf(fm_fabsf(l[i + j]), fm_fabsf(r[i + j])));
        db = 6.0206f * fm_log2f(pk + 1e-6f);
        tgt = db > c->thr ? (db - c->thr) * c->slope : 0.0f;
        comp_gr += (tgt - comp_gr) * (tgt > comp_gr ? c->aa : c->ar);
        gr = comp_gr;
        if (c->autor) {
            comp_grs += (tgt - comp_grs) * (tgt > comp_grs ? c->aas : c->ars);
            gr = fm_maxf(gr, comp_grs);
        }
        g1 = fm_exp2f((c->mk - gr) * 0.16609640f);   /* (dB to a gain: 2^(dB / 6.02)) */
        g1 = 1.0f - c->mix + c->mix * g1;
        dg = (g1 - comp_g) / (float)m;
        for (j = 0; j < m; j++) {
            comp_g += dg;
            l[i + j] *= comp_g;
            r[i + j] *= comp_g;
        }
        comp_db = db;
    }
}

static void comp_tick(float *l, float *r, int n)
{
    static const float ratio_slope[3] = {0.5f, 0.75f, 0.9f};
    static const float atk_s[6] = {0.0001f, 0.0003f, 0.001f, 0.003f, 0.01f, 0.03f};
    static const float rel_s[5] = {0.1f, 0.3f, 0.6f, 1.2f, 0.12f};   /* (AUTO: its fast stage) */
    const float *ef = trk[7].eff;
    comp_cfg_t c;
    int rel;
    if (rv.tp[7][TP_KIND] != KIND_COMP) {
        comp_gr = comp_grs = 0.0f;
        comp_g = 1.0f;
        rv_live.comp_gr = 0.0f;
        return;
    }
    rel = clampi((int)(ef[TP_S3] + 0.5f), 0, 4);
    c.thr = clampf(ef[TP_S0], -30.0f, 0.0f);
    c.slope = ratio_slope[clampi((int)(ef[TP_S1] + 0.5f), 0, 2)];
    c.aa = 1.0f - fm_expf(-(float)COMP_SUB / (atk_s[clampi((int)(ef[TP_S2] + 0.5f), 0, 5)] * RV_FS));
    c.ar = 1.0f - fm_expf(-(float)COMP_SUB / (rel_s[rel] * RV_FS));
    c.autor = rel == 4;
    c.aas = 1.0f - fm_expf(-(float)COMP_SUB / (0.6f * RV_FS));
    c.ars = 1.0f - fm_expf(-(float)COMP_SUB / (1.5f * RV_FS));
    if (!c.autor)
        comp_grs = 0.0f;
    c.mk = clampf(ef[TP_S4], 0.0f, 15.0f);
    c.mix = clampf(ef[TP_S5], 0.0f, 100.0f) * 0.01f;
    RV_FAR(comp_run)(l, r, n, &c);
    rv_live.comp_gr = fm_maxf(comp_gr, comp_grs);
    rv_live.comp_in = comp_db;
}

/* ----------------------------------------------------------- render --- */
static void render_sub(float *outl, float *outr, int n)
{
    static float fb_in[RV_SUB], dly_in[RV_SUB], rev_in[RV_SUB];
    static float fb_out[RV_SUB];                    /* kept: the feedback return reads the last step's */
    int offs[8], np, i, t, k;
    static uint32_t lead_age[RV_TRACKS];
    float solo_on = rv.solo ? 1.0f : 0.0f;
    float ingain = rv.gp[GP_INPUTLEVEL] * (2.0f / 127.0f);
    int nin;
    (void)solo_on;

    uint32_t cyc0, vcyc0, vcyc, fw0 = rv_live.fetch_on ? plat_fetch_wait() : 0;
    prof_t = cyc0 = plat_cycles();
    movement_tick(n);
    RV_FAR(eff_compute)();
    cmd_drain();                                    /* after the parameters: a note sees this step's values */
    np = clock_tick(n, offs, 8);
    arp_control();
    for (i = 0; i < np; i++) {
        seq_pulse(offs[i]);
        arp_pulse(offs[i]);
    }
    seq_other_clocks();
    seq_events(n);
    glide_tick(n);
    PROF(PF_CTRL);
    vcyc0 = prof_t;

    for (i = 0; i < n; i++) {
        outl[i] = outr[i] = 0.0f;
        fb_in[i] = dly_in[i] = rev_in[i] = 0.0f;
    }
    nin = plat_audio_in(in_buf, n);
    for (i = nin; i < n; i++)
        in_buf[2 * i] = in_buf[2 * i + 1] = 0.0f;

    /* incoming excitation and ring: influence from the last control step's audio, feedback return */
    for (t = 0; t < RV_TRACKS; t++) {
        track_t *tr = &trk[t];
        int a, ret = clampi(rv.gp[GP_FBRET], 0, 8);
        tr->ring_amt = 0.0f;
        for (i = 0; i < n; i++) {
            tr->exc[i] = 0.0f;
            tr->ring[i] = 0.0f;
        }
        for (a = 0; a < RV_TRACKS; a++) {
            const rv_route *r = &rv.infl[a][t];
            if (r->type == RT_EXCITE && r->amt) {
                float g = r->amt * (2.0f / 64.0f);
                for (i = 0; i < n; i++)
                    tr->exc[i] += trk[a].prev[i] * g;
            } else if (r->type == RT_RING && r->amt) {
                float g = r->amt * (1.0f / 64.0f);
                tr->ring_amt += fm_fabsf(g);
                for (i = 0; i < n; i++)
                    tr->ring[i] += trk[a].prev[i] * g * 4.0f;
            }
        }
        if (ret == t + 1)
            for (i = 0; i < n; i++)
                tr->exc[i] += fb_out[i] * 1.5f;
        if (rv.tp[t][TP_KIND] == KIND_STRIKE && rv.tp[t][TP_S0] == EXC_INPUT)
            for (i = 0; i < n; i++)
                tr->exc[i] += (in_buf[2 * i] + in_buf[2 * i + 1]) * 0.5f * ingain;
        {
            float pk = 0.0f;
            for (i = 0; i < n; i++)
                if (fm_fabsf(tr->exc[i]) > pk)
                    pk = fm_fabsf(tr->exc[i]);
            rv_live.exc_in[t] = pk > rv_live.exc_in[t] ? pk : rv_live.exc_in[t] * 0.995f;   /* held, falling slowly */
            tr->exc_on = pk > 0.0f;                  /* (the voices skip adding none) */
        }
        for (i = 0; i < n; i++)
            tr->buf[i] = 0.0f;
    }
    /* the voices: synth, strike, drum, in that order; every other one to the second core when it is
     * on (the buses go there too); then what they did is counted, here */
    rv_live.voices_synth = rv_live.voices_strike = 0;
    {
        int e, nc2 = 0;
        uint32_t c, c1, wv = 0;
        vl_n = 0;
        for (k = 0; k < RV_SYNTH_VOICES; k++) {
            voice_t *v = &vs[k];
            if (!v->on)
                continue;
            if (rv.tp[v->track][TP_KIND] != KIND_SYNTH) {
                v->on = 0;
                continue;
            }
            RV_FAR(artic_cfg_track)(&v->cfg, trk[v->track].eff);
            vl_kind[vl_n] = VK_SYNTH;
            vl_idx[vl_n++] = (uint8_t)k;
        }
        for (k = 0; k < RV_STRIKE_VOICES; k++) {
            strike_t *v = &ss[k];
            if (!v->on)
                continue;
            if (rv.tp[v->track][TP_KIND] != KIND_STRIKE) {
                v->on = 0;
                continue;
            }
            RV_FAR(artic_cfg_track)(&v->cfg, trk[v->track].eff);
            vl_kind[vl_n] = VK_STRIKE;
            vl_idx[vl_n++] = (uint8_t)k;
        }
        for (k = 0; k < RV_PERC_VOICES; k++) {
            perc_t *v = &ps[k];
            if (!v->on)
                continue;
            if (rv.tp[v->track][TP_KIND] != KIND_PERC) {
                v->on = 0;
                continue;
            }
            vl_kind[vl_n] = VK_PERC;
            vl_idx[vl_n++] = (uint8_t)k;
        }
        {   /* the same code back to back: the synth voices by engine, the drums by machine (the cache holds
             * one engine's code while its voices play, as Mutable's and the Deluge's renders do) */
            int a, b;
            for (a = 1; a < vl_n; a++)
                for (b = a; b > 0 && vl_kind[b - 1] == vl_kind[b] && vl_sortkey(b - 1) > vl_sortkey(b); b--) {
                    uint8_t ti = vl_idx[b];
                    vl_idx[b] = vl_idx[b - 1];
                    vl_idx[b - 1] = ti;
                }
        }
        c2_trk = 0;
        for (e = 0; e < vl_n; e++) {                 /* every other voice, up to its limit */
            vl_c2[e] = (uint8_t)(rv_live.cores2 && (e & 1) && nc2 < c2_vcap);
            if (vl_c2[e]) {
                nc2++;
                c2_trk |= (uint8_t)(1u << vl_track(e));
            }
        }
        for (t = 0; t < RV_TRACKS; t++)
            if ((c2_trk >> t) & 1u)
                zero_buf(c2_buf[t], n);
        par_n = n;
        bo_n = n;
        bo_act = bx_act;
        par_vdone = 0;
        par_job = 0;
        rv_live.c2_used = 0;
        if (rv_live.cores2) {                        /* the second core: its voices, then the buses */
            if (plat_par_run(c2_job, (uint32_t)nc2)) {
                par_job = 1;
                rv_live.c2_used = 1;
            } else
                rv_live.c2_fail = 1;
        }
        if (!par_job)
            for (e = 0; e < vl_n; e++)
                vl_c2[e] = 0;
        c = plat_cycles();
        for (e = 0; e < vl_n; e++)                   /* this core's */
            if (!vl_c2[e]) {
                RV_FAR(voice_one)(e, trk[vl_track(e)].buf, n);
                c1 = plat_cycles();
                prof_acc[PF_SYNTH + vl_kind[e]] += c1 - c;
                c = c1;
            }
        if (par_job && nc2) {                        /* the second core's */
            uint32_t w0 = plat_par_cycles();
            int r = par_voices_wait();
            wv = plat_par_cycles() - w0;
            c2_wacc += wv;
            if (r < 0) {                             /* it failed (and is off now): its voices here, again */
                rv_live.c2_fail = 1;
                par_job = 0;                         /* (and the buses) */
                for (e = 0; e < vl_n; e++)
                    if (vl_c2[e]) {
                        vl_c2[e] = 0;
                        RV_FAR(voice_one)(e, trk[vl_track(e)].buf, n);
                    }
            } else {
                for (t = 0; t < RV_TRACKS; t++)
                    if ((c2_trk >> t) & 1u)
                        for (i = 0; i < n; i++)
                            trk[t].buf[i] += c2_buf[t][i];
                for (k = 0; k < 3; k++) {            /* (its voices' time, in their bars and in its load) */
                    prof_acc[PF_SYNTH + k] += c2_vcyc[k];
                    c2_acc += c2_vcyc[k];
                }
            }
        }
        c2_vcyc[0] = c2_vcyc[1] = c2_vcyc[2] = 0;
        voices_count(lead_age);
        prof_t = plat_cycles();                      /* (the waiting: in no part's bar, in the total) */
        c2_voices_given = (uint8_t)nc2;
    }
    vcyc = prof_t - vcyc0;
    /* tape tracks, and the tracks' chains */
    for (t = 0; t < RV_TRACKS; t++) {
        track_t *tr = &trk[t];
        const float *eff = tr->eff;
        int kind = rv.tp[t][TP_KIND];
        float drive, fld, derez, ia, ib, imix, lvl, pan, gl, gr, peak = 0.0f, ring = tr->ring_amt;
        int bits, mask, ins, chain_bad = 0, n_chain = n;
        int chorus = kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ ? clampi(rv.tp[t][TP_S3], 0, 3) : 0;
        uint8_t muted = (rv.mute >> t) & 1u;
        if (rv.solo && !((rv.solo >> t) & 1u))
            muted = 1;
        drive = eff[TP_DRIVE] * (1.0f / 127.0f);
        fld = eff[TP_FOLD] * (1.0f / 127.0f);
        bits = (int)(eff[TP_BITS] + 0.5f);
        derez = eff[TP_DEREZ] * (1.0f / 127.0f);
        mask = (int)(eff[TP_MASK] + 0.5f);
        ins = clampi((int)(eff[TP_INS] + 0.5f), 0, INS_N - 1);
        ia = eff[TP_INA] * (1.0f / 127.0f);
        ib = eff[TP_INB] * (1.0f / 127.0f);
        imix = eff[TP_INMIX] * (1.0f / 127.0f);
        lvl = eff[TP_LEVEL] * (1.0f / 127.0f);
        lvl *= lvl * 0.7f;
        pan = eff[TP_PAN] * (1.0f / 64.0f);
        gl = lvl * fm_sqrtf(clampf(0.5f - pan * 0.5f, 0.0f, 1.0f)) * 1.414f;
        gr = lvl * fm_sqrtf(clampf(0.5f + pan * 0.5f, 0.0f, 1.0f)) * 1.414f;
        if (punch[6]) {                              /* THROW: everything into delay and space */
            gl *= 0.6f;
            gr *= 0.6f;
        }
        if (ring <= 0.001f && peak_of(tr->buf, n) <= 1e-7f) {   /* nothing in: once its chain has rung out, skip it */
            if (tr->quiet >= BUS_HOLD)
                n_chain = 0;
        } else {
            tr->quiet = 0;
        }
        for (i = 0; i < n_chain; i++) {
            float x = tr->buf[i];
            x = skin(tr, x, drive, fld, bits, derez, mask);
            if (ins)
                x = lerpf(x, insert_tick(tr, ins, x, ia, ib), imix);
            if (ring > 0.001f)
                x = lerpf(x, x * sat(tr->ring[i] / ring), clampf(ring, 0.0f, 1.0f));   /* a bounded carrier: rings that feed each other cannot grow */
            x = dcb_tick(&tr->dc, x);
            if (!(x > -RV_GUARD && x < RV_GUARD)) {
                x = 0.0f;
                chain_bad = 1;
            }
            tr->buf[i] = x;
            if (fm_fabsf(x) > peak)
                peak = fm_fabsf(x);
            if (!muted) {
                float sl = x * gl, sr = x * gr, m = x * lvl;
                if (chorus) {                       /* Juno-60 style: dry + bbd left, dry - bbd right */
                    float wet = chorus_tick(tr, x, chorus);
                    sl = (x + wet) * gl * 0.75f;
                    sr = (x - wet) * gr * 0.75f;
                }
                outl[i] += sl;
                outr[i] += sr;
                fb_in[i] += m * eff[TP_FBS] * (1.0f / 127.0f);
                dly_in[i] += m * (punch[6] ? 0.8f : eff[TP_DLYS] * (1.0f / 127.0f));
                rev_in[i] += m * (punch[6] ? 0.8f : eff[TP_REVS] * (1.0f / 127.0f));
            }
        }
        if (peak < 1e-6f) {                          /* silent out: counting toward skipping it */
            if (tr->quiet < 0xFFFFu)
                tr->quiet++;
        } else {
            tr->quiet = 0;
        }
        if (chain_bad) {                             /* the chain blew up: clear its memory */
            fault(400 + ins);
            rv_fault_track = t;
            __builtin_memset(&tr->dc, 0, sizeof(tr->dc));
            __builtin_memset(tr->comb, 0, sizeof(tr->comb));
            __builtin_memset(tr->smear, 0, sizeof(tr->smear));
            __builtin_memset(tr->hilb_i, 0, sizeof(tr->hilb_i));
            __builtin_memset(tr->hilb_q, 0, sizeof(tr->hilb_q));
            tr->comb_lp = tr->sh_z = tr->crush_hold = tr->derez_hold = 0.0f;
            tr->ring_amt = 0.0f;
            zero_buf(tr->exc, n);
            zero_buf(tr->ring, n);
        }
        if (muted)
            peak *= 0.0f;
        tr->meter = fm_maxf(peak, tr->meter * 0.93f);
        rv_live.meter[t] = tr->meter;
        rv_live.env[t] = tr->lead_env;
        /* the scope: one point per 16 samples */
        for (i = 0; i < n; i += 16) {
            rv_live.scope[t][(rv_live.scope_w + i / 16) & 63] = (int16_t)clampf(tr->buf[i] * 8192.0f, -32767.0f, 32767.0f);
        }
        for (i = 0; i < n; i++)
            tr->prev[i] = tr->buf[i];
        /* a voice-less track decays its lead envelope */
        tr->lead_env *= fm_expf(-0.005013f * (float)n * (1.0f / 32.0f));   /* (0.995 a 32-frame step) */
    }
    rv_live.scope_w = (uint8_t)((rv_live.scope_w + n / 16) & 63);
    {   /* the chains: all the tracks' loop */
        uint32_t c = plat_cycles();
        prof_acc[PF_CHAIN] += c - prof_t;
        prof_t = c;
    }
    /* the buses: this step's sends go into the ring (they play next step); what they made of the
     * last step's comes back from the second core, or is made here now */
    {
        float act = 0.0f;
        int j = bx_w;
        for (i = 0; i < n; i++) {
            bx_fb[j] = fb_in[i];
            bx_dly[j] = dly_in[i];
            bx_rev[j] = rev_in[i];
            if (++j >= BX_LEN)
                j = 0;
            act += fm_fabsf(outl[i]) + fm_fabsf(outr[i]);
        }
        if (par_job) {
            uint32_t w0 = plat_par_cycles(), we, bud = (uint32_t)((float)plat_cycles_hz() / (float)RV_FS * (float)n);
            int r = plat_par_wait();
            we = plat_par_cycles() - w0;
            c2_wacc += we;
            if (r < 0) {                             /* it failed (and is off now): here */
                rv_live.c2_fail = 1;
                par_job = 0;
            } else if (we > bud / 50u)               /* this core waited for it: fewer voices to it */
                c2_vcap = (uint8_t)(c2_voices_given ? c2_voices_given - 1 : 0);
            else if (we < bud / 200u && c2_vcap <= c2_voices_given && c2_vcap < VL_MAX)
                c2_vcap++;                           /* it was done in time: one more, if there are more */
        }
        if (!par_job) {
            bo_all = 0;
            bus_job();
            prof_acc[PF_BED] += bo_cyc[0];
            prof_acc[PF_FB] += bo_cyc[1];
            prof_acc[PF_DLY] += bo_cyc[2];
            prof_acc[PF_SPACE] += bo_cyc[3];
            prof_t += bo_all;                        /* (counted in the parts above, not the master's) */
        } else {
            prof_acc[PF_BED] += bo_cyc[0];
            prof_acc[PF_FB] += bo_cyc[1];
            prof_acc[PF_DLY] += bo_cyc[2];
            prof_acc[PF_SPACE] += bo_cyc[3];
            c2_acc += bo_all;
        }
        bo_cyc[0] = bo_cyc[1] = bo_cyc[2] = bo_cyc[3] = bo_all = 0;
        par_job = 0;
        bx_w = j;
        bx_act = act / (float)n;
        if (bo_fault) {
            fault(bo_fault);
            buses_reset();
        }
        for (i = 0; i < n; i++) {
            fb_out[i] = bo_fb[i];                    /* (the feedback return next step) */
            outl[i] += bo_l[i];
            outr[i] += bo_r[i];
        }
    }

    comp_tick(outl, outr, n);                       /* COMP (T8): the glue on the mix */
    /* master: drive, microcuts, punch filters, a gentle limiter */
    {
        float md = geff[GP_MDRIVE] * (1.0f / 127.0f), pre = 1.0f + md * 3.0f;
        master_drive_comp = 1.0f / (1.0f + md * 1.2f);
        if (md > 0.0f) {                              /* off at 0 (the limiter alone keeps the peaks); faded in over the
                                                       * first few steps, so moving it through 0 makes no jump */
            float w = fm_minf(1.0f, md * 20.0f);
            for (i = 0; i < n; i++) {
                outl[i] += (sat(outl[i] * pre) * master_drive_comp - outl[i]) * w;
                outr[i] += (sat(outr[i] * pre) * master_drive_comp - outr[i]) * w;
            }
        }
        cuts_run(outl, outr, n);
        if (punch[4] || punch[5]) {
            static float sweep = 1.0f;
            sweep += ((punch[4] ? 0.15f : 0.05f) - sweep) * (1.0f - fm_expf(-0.0202f * (float)n * (1.0f / 32.0f)));
            svf_set(&punch_f[0], punch[4] ? 200.0f + sweep * 8000.0f : 300.0f + (1.0f - sweep) * 3000.0f, 1.5f);
            punch_f[1] = punch_f[0];
            for (i = 0; i < n; i++) {
                float hp;
                if (punch[4]) {
                    outl[i] = svf_tick(&punch_f[0], outl[i], 0, 0);
                    outr[i] = svf_tick(&punch_f[1], outr[i], 0, 0);
                } else {
                    svf_tick(&punch_f[0], outl[i], 0, &hp);
                    outl[i] = hp;
                    svf_tick(&punch_f[1], outr[i], 0, &hp);
                    outr[i] = hp;
                }
            }
        }
        /* the safety stage: no DC, a peak limiter at -1 dB (instant attack, about 80 ms
         * release), and nothing non-finite past it */
        if (buf_fix(outl, n) | buf_fix(outr, n)) {
            fault(1100);
            buses_reset();
        }
        if (!(lim_gain > 0.0f && lim_gain <= 1.0f))
            lim_gain = 1.0f;
        RV_FAR(master_tail)(outl, outr, n);
        if (moom_sfx_req || moom_music || sfx_kind || mus_t != 0.0f)   /* MOOM's sounds, when it plays */
            for (i = 0; i < n; i++) {
            if (moom_sfx_req) {                       /* a MOOM sound starts */
                sfx_kind = moom_sfx_req;
                moom_sfx_req = 0;
                sfx_t = 0.0f;
                sfx_ph = 0.0f;
            }
            if (moom_music) {                         /* MOOM's soundtrack, under its effects */
                float m = mus_tick();
                outl[i] = clampf(outl[i] + m, -0.98f, 0.98f);
                outr[i] = clampf(outr[i] + m, -0.98f, 0.98f);
            } else
                mus_t = 0.0f;
            if (sfx_kind) {
                float c = sfx_tick();
                if (sfx_kind == 1 && sfx_t < 0.012f)
                    sfx_t += RV_DT;                   /* (the clicks advance their own time) */
                outl[i] = clampf(outl[i] + c, -0.98f, 0.98f);
                outr[i] = clampf(outr[i] + c, -0.98f, 0.98f);
            }
            }
    }
    now_s += (uint32_t)n;
    rv_live.tick = now_s;
    PROF(PF_MASTER);
    if (rv_live.fetch_on) {
        prof_fetch += plat_fetch_wait() - fw0;
        prof_rcyc += prof_t - cyc0;
    }
    prof_end(n);
    guard_tick(prof_t - cyc0, vcyc, n);
}

void rv_render(int32_t *out, int frames)
{
    float l[RV_SUB], r[RV_SUB];
    int done = 0, i;
    while (done < frames) {
        int n = frames - done > RV_SUB ? RV_SUB : frames - done;
        render_sub(l, r, n);
        for (i = 0; i < n; i++) {
            float a = l[i] == l[i] ? clampf(l[i], -1.0f, 1.0f) : 0.0f;   /* never NaN into an integer */
            float b = r[i] == r[i] ? clampf(r[i], -1.0f, 1.0f) : 0.0f;
            out[2 * (done + i)] = (int32_t)(a * 8388607.0f);
            out[2 * (done + i) + 1] = (int32_t)(b * 8388607.0f);
        }
        done += n;
    }
}

void rv_engine_init(void)
{
    int i;
    synth_tables();
    seq_init();
    buses_init();
    for (i = 0; i < RV_SYNTH_VOICES; i++)
        vs[i].on = 0;
    for (i = 0; i < RV_STRIKE_VOICES; i++)
        ss[i].on = 0;
    cq_r = cq_w = 0;
    RV_FAR(eff_compute)();
}
