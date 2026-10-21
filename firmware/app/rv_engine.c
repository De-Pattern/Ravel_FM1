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
static int buf_bad(const float *b, int n)
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
static void zero_buf(float *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        b[i] = 0.0f;
}
static float master_drive_comp = 1.0f;

uint32_t rv_rng(void) { return xs32(&eng_rng); }

/* ------------------------------------------------------- command queue --- */
enum { C_NOTE, C_LIVE, C_TRANSPORT, C_CATCH, C_RESAMPLE, C_JUMP, C_ROW, C_RATE, C_PUNCH, C_CUT, C_SCENE, C_PATTERN, C_TAP, C_ARM, C_MOTION, C_ALLOFF };
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
void rv_catch(void) { cmd(C_CATCH, 0, 0, 0); }
void rv_resample(void) { cmd(C_RESAMPLE, 0, 0, 0); }
void rv_tape_jump(int t, int pos, int loop_to) { cmd(C_JUMP, t, pos, loop_to); }
void rv_tape_row(int t, int row) { cmd(C_ROW, t, row, 0); }
void rv_tape_rate(int t, int how) { cmd(C_RATE, t, how, 0); }
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
static hitshade_t hs_pend;
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
static void hs_mix(hitshade_t *h, const float *src, float *dst, int n)
{
    int i;
    float e0 = h->env, e1 = h->env * (h->dk > 0.0f ? fm_expf(-(float)n * RV_DT * h->dk) : 1.0f), de = (e1 - e0) / (float)n;
    for (i = 0; i < n; i++) {
        h->lps += (src[i] - h->lps) * h->lpc;
        dst[i] += h->lps * (e0 + de * (float)i) * h->gain;
    }
    h->env = e1;
}

static void note_on_internal(int t, int note, int vel, int env, uint16_t chain)
{
    track_t *tr = &trk[t];
    int kind = rv.tp[t][TP_KIND], i, spark = spark_pend;
    float v = (float)vel * (1.0f / 127.0f), grain = rv.tp[t][TP_GRAIN] * (1.0f / 127.0f);
    spark_pend = 0;
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
        artic_cfg_track(&vo->cfg, tr->eff);
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
        else if (rv.tp[t][TP_ENGINE] == ENG_SQ)
            sq_note(vo, tr->eff);
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
        artic_cfg_track(&sv->cfg, tr->eff);
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
    } else {
        tapetrk_t *tt = &tr->tape;
        artic_cfg_track(&tt->cfg, tr->eff);
        tape_track_note(tr, note, v);
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
    } else {
        tape_track_note(&trk[t], note, 0.0f);
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
    for (i = 0; i < RV_TRACKS; i++)                 /* the tape tracks too, MLR loops included: a key starts them again */
        if (rv.tp[i][TP_KIND] == KIND_TAPE) {
            artic_release(&trk[i].tape.a, &trk[i].tape.cfg);
            trk[i].tape.pend_pos = -1;
        }
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
            if (c.c > 0)
                note_on_internal(c.a & 7, c.b, c.c, 0, (uint16_t)(1u << (c.a & 7)));
            else
                note_off_internal(c.a & 7, c.b);
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
        case C_CATCH:
            catch_now();
            break;
        case C_RESAMPLE:
            resample_now();
            break;
        case C_JUMP:
            tape_track_jump(&trk[c.a & 7], c.b, c.c, 0);
            record_jump(c.a & 7, c.b);
            break;
        case C_ROW:
            tape_row_now(c.a & 7, c.b);
            break;
        case C_RATE:
            tape_rate_now(c.a & 7, c.b);
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

/* ----------------------------------------------------------- render --- */
static void render_sub(float *outl, float *outr, int n)
{
    static float voc_in[RV_SUB], fb_in[RV_SUB], pair_in[RV_SUB], dly_in[RV_SUB], rev_in[RV_SUB], band_in[RV_SUB];
    static float fb_out[RV_SUB];                    /* kept: the feedback return reads the last step's */
    static float tape_src[RV_SUB], bus_mono[RV_SUB], tape_fb[RV_SUB];
    int offs[8], np, i, t, k;
    int beads_next = 0;
    static uint32_t lead_age[RV_TRACKS];
    float solo_on = rv.solo ? 1.0f : 0.0f;
    float ingain = rv.gp[GP_INPUTLEVEL] * (2.0f / 127.0f);
    int nin;
    (void)solo_on;

    movement_tick(n);
    eff_compute();
    cmd_drain();                                    /* after the parameters: a note sees this step's values */
    np = clock_tick(n, offs, 8);
    for (i = 0; i < np; i++)
        seq_pulse(offs[i]);
    seq_other_clocks();
    seq_events(n);
    glide_tick(n);

    for (i = 0; i < n; i++) {
        outl[i] = outr[i] = 0.0f;
        voc_in[i] = fb_in[i] = pair_in[i] = band_in[i] = dly_in[i] = rev_in[i] = tape_fb[i] = 0.0f;
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
        }
        for (i = 0; i < n; i++)
            tr->buf[i] = 0.0f;
    }
    /* voices */
    rv_live.voices_synth = rv_live.voices_strike = 0;
    for (k = 0; k < RV_SYNTH_VOICES; k++) {
        voice_t *v = &vs[k];
        track_t *tr;
        float lv;
        if (!v->on)
            continue;
        tr = &trk[v->track];
        if (rv.tp[v->track][TP_KIND] != KIND_SYNTH) {
            v->on = 0;
            continue;
        }
        artic_cfg_track(&v->cfg, tr->eff);
        if (v->hs.on) {                              /* a shaded hit: on its own, then into the track */
            float tmp[RV_SUB];
            zero_buf(tmp, n);
            lv = synth_voice_render(v, tr, tmp, n, 0.0f);
            hs_mix(&v->hs, tmp, tr->buf, n);
        } else
            lv = synth_voice_render(v, tr, tr->buf, n, 0.0f);
        if (buf_bad(tr->buf, n) || !(lv == lv)) {
            fault(100 + rv.tp[v->track][TP_ENGINE]);
            __builtin_memset(v, 0, sizeof(*v));
            zero_buf(tr->buf, n);
            continue;
        }
        rv_live.voices_synth++;
        if (v->age >= lead_age[v->track]) {          /* the newest voice leads */
            lead_age[v->track] = v->age;
            tr->lead_env = lv;
            rv_live.stage[v->track] = v->a.stage;
            rv_live.stage_pos[v->track] = v->a.u;
            rv_live.fenv[v->track] = v->fe.lvl;
            rv_live.fenv_pos[v->track] = v->fe.u;
            rv_live.fenv_stage[v->track] = v->fe.stage;
        }
    }
    for (k = 0; k < RV_STRIKE_VOICES; k++) {
        strike_t *v = &ss[k];
        track_t *tr;
        float lv;
        if (!v->on)
            continue;
        tr = &trk[v->track];
        if (rv.tp[v->track][TP_KIND] != KIND_STRIKE) {
            v->on = 0;
            continue;
        }
        artic_cfg_track(&v->cfg, tr->eff);
        if (v->hs.on) {
            float tmp[RV_SUB];
            zero_buf(tmp, n);
            lv = strike_render(v, tr, tmp, n);
            hs_mix(&v->hs, tmp, tr->buf, n);
        } else
            lv = strike_render(v, tr, tr->buf, n);
        if (buf_bad(tr->buf, n) || !(lv == lv)) {
            fault(200 + rv.tp[v->track][TP_S0]);
            __builtin_memset(v, 0, sizeof(*v));
            zero_buf(tr->buf, n);
            continue;
        }
        rv_live.voices_strike++;
        tr->lead_env = lv;
        rv_live.stage[v->track] = v->a.stage;
        rv_live.stage_pos[v->track] = v->a.u;
    }
    for (k = 0; k < RV_PERC_VOICES; k++) {
        perc_t *v = &ps[k];
        track_t *tr;
        float lv;
        if (!v->on)
            continue;
        tr = &trk[v->track];
        if (rv.tp[v->track][TP_KIND] != KIND_PERC) {
            v->on = 0;
            continue;
        }
        if (v->hs.on) {
            float tmp[RV_SUB];
            zero_buf(tmp, n);
            lv = perc_render(v, tr, tmp, n);
            hs_mix(&v->hs, tmp, tr->buf, n);
        } else
            lv = perc_render(v, tr, tr->buf, n);
        if (buf_bad(tr->buf, n) || !(lv == lv)) {
            fault(300 + rv.tp[v->track][TP_S0]);
            __builtin_memset(v, 0, sizeof(*v));
            zero_buf(tr->buf, n);
            continue;
        }
        rv_live.voices_strike++;
        if (v->age >= lead_age[v->track]) {
            lead_age[v->track] = v->age;
            tr->lead_env = lv;
            rv_live.stage[v->track] = AS_CONT;
            rv_live.stage_pos[v->track] = 0.0f;
        }
    }
    /* tape tracks, and the tracks' chains */
    for (t = 0; t < RV_TRACKS; t++) {
        track_t *tr = &trk[t];
        const float *eff = tr->eff;
        int kind = rv.tp[t][TP_KIND];
        float drive, fld, derez, ia, ib, imix, lvl, pan, gl, gr, peak = 0.0f, ring = tr->ring_amt;
        int bits, mask, ins, chain_bad = 0;
        int chorus = kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ ? clampi(rv.tp[t][TP_S3], 0, 3) : 0;
        uint8_t muted = (rv.mute >> t) & 1u;
        if (rv.solo && !((rv.solo >> t) & 1u))
            muted = 1;
        if (kind == KIND_TAPE) {
            int bi = -1;
            if (rv.tp[t][TP_S0] == TAPE_BEADS && beads_next < RV_BEADS)
                bi = beads_next++;
            if (tr->tape.beads != bi)
                tr->tape.beads = (int8_t)bi;
            if (!tr->tape.a.gate && tr->tape.a.stage == AS_OFF)
                artic_cfg_track(&tr->tape.cfg, eff);
            tr->lead_env = tape_track_render(tr, t, tr->buf, n, bi);
            if (buf_fix(tr->buf, n) || !(tr->lead_env == tr->lead_env)) {
                fault(300 + rv.tp[t][TP_S0]);
                tr->lead_env = 0.0f;
                __builtin_memset(&tr->tape.a, 0, sizeof(tr->tape.a));
            }
            rv_live.stage[t] = tr->tape.a.stage;
            rv_live.stage_pos[t] = tr->tape.a.u;
            /* Beads FEEDBACK goes back into the tape */
            if (bi >= 0 && eff[TP_S11] > 0.5f) {
                float g = eff[TP_S11] * (0.9f / 127.0f);
                for (i = 0; i < n; i++)
                    tape_fb[i] += tr->buf[i] * g;
            }
        } else if (kind == KIND_SYNTH || kind == KIND_STRIKE || kind == KIND_PERC) {
            /* a track with no voice still lets its incoming excitation through (influence) */
        }
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
        for (i = 0; i < n; i++) {
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
                voc_in[i] += m * eff[TP_VOC] * (1.0f / 127.0f);
                fb_in[i] += m * eff[TP_FBS] * (1.0f / 127.0f);
                pair_in[i] += m * eff[TP_PAIRS] * (1.0f / 127.0f);
                band_in[i] += m * eff[TP_BANDS] * (1.0f / 127.0f);
                dly_in[i] += m * (punch[6] ? 0.8f : eff[TP_DLYS] * (1.0f / 127.0f));
                rev_in[i] += m * (punch[6] ? 0.8f : eff[TP_REVS] * (1.0f / 127.0f));
            }
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
        tr->lead_env *= 0.995f;
    }
    rv_live.scope_w = (uint8_t)((rv_live.scope_w + n / 16) & 63);
    bed_run(rev_in, outl, outr, n);                  /* the BED, under the tracks (it listens to them) */
    if (buf_fix(outl, n) | buf_fix(outr, n)) {
        fault(950);
        buses_reset();
    }

    /* buses */
    for (i = 0; i < n; i++)
        bus_mono[i] = 0.0f;
    {
        int vb = clampi(rv.gp[GP_VOCBODY], 0, 11);
        const float *body = vb == 3 ? 0 : vb >= 4 ? trk[vb - 4].prev : 0;
        static float tape_body[RV_SUB];
        if (vb == 3) {
            for (i = 0; i < n; i++)
                tape_body[i] = tape_read_u(1.0f - (float)((now_s + (uint32_t)i) % 44100u) / 44100.0f * 0.5f) * 0.5f;
            body = tape_body;
        }
        vocoder_run(voc_in, body, n, bus_mono);
        if (buf_fix(bus_mono, n)) {
            fault(500);
            buses_reset();
        }
    }
    feedback_run(fb_in, n, fb_out);
    if (buf_fix(fb_out, n)) {
        fault(600);
        buses_reset();
    }
    for (i = 0; i < n; i++) {
        bus_mono[i] += fb_out[i];
        outl[i] += bus_mono[i];
        outr[i] += bus_mono[i];
    }
    pair_run(pair_in, n, outl, outr);
    if (buf_fix(outl, n) | buf_fix(outr, n)) {
        fault(700);
        buses_reset();
    }
    bands_run(band_in, n, outl, outr);
    if (buf_fix(outl, n) | buf_fix(outr, n)) {
        fault(750);
        buses_reset();
    }
    delay_run(dly_in, n, outl, outr);
    if (buf_fix(outl, n) | buf_fix(outr, n)) {
        fault(800);
        buses_reset();
    }
    space_run(rev_in, n, outl, outr);
    if (buf_fix(outl, n) | buf_fix(outr, n)) {
        fault(900);
        buses_reset();
    }

    /* the tape's input */
    {
        int src = clampi(rv.gp[GP_TAPESRC], 0, 10);
        for (i = 0; i < n; i++) {
            float x;
            if (src == 0)
                x = (outl[i] + outr[i]) * 0.5f;
            else if (src <= 8)
                x = trk[src - 1].buf[i] * (rv.tp[src - 1][TP_LEVEL] * (1.0f / 127.0f));
            else if (src == 9)
                x = bus_mono[i];
            else
                x = (in_buf[2 * i] + in_buf[2 * i + 1]) * 0.5f * ingain;
            tape_src[i] = x + tape_fb[i];
        }
        if (buf_fix(tape_src, n))
            fault(1000);
        tape_capture(tape_src, n, 0, (mpulse & 15) == 0 && playing ? 1 : !playing);
    }
    /* input monitor */
    if (rv.gp[GP_TAPEMON] > 0) {
        float g = rv.gp[GP_TAPEMON] * (1.0f / 127.0f) * ingain;
        for (i = 0; i < n; i++) {
            outl[i] += in_buf[2 * i] * g;
            outr[i] += in_buf[2 * i + 1] * g;
        }
    }

    /* master: drive, microcuts, punch filters, a gentle limiter */
    {
        float md = rv.gp[GP_MDRIVE] * (1.0f / 127.0f), pre = 1.0f + md * 3.0f;
        master_drive_comp = 1.0f / (1.0f + md * 1.2f);
        for (i = 0; i < n; i++) {
            outl[i] = sat(outl[i] * pre) * master_drive_comp;
            outr[i] = sat(outr[i] * pre) * master_drive_comp;
        }
        cuts_run(outl, outr, n);
        if (punch[4] || punch[5]) {
            static float sweep = 1.0f;
            sweep += ((punch[4] ? 0.15f : 0.05f) - sweep) * 0.02f;
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
            rv_live.master_l = fm_maxf(fm_fabsf(outl[i]), rv_live.master_l * 0.9999f);
            rv_live.master_r = fm_maxf(fm_fabsf(outr[i]), rv_live.master_r * 0.9999f);
        }
    }
    now_s += (uint32_t)n;
    rv_live.tick = now_s;
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
    tape_init();
    for (i = 0; i < RV_SYNTH_VOICES; i++)
        vs[i].on = 0;
    for (i = 0; i < RV_STRIKE_VOICES; i++)
        ss[i].on = 0;
    cq_r = cq_w = 0;
    eff_compute();
}
