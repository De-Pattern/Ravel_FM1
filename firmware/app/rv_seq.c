/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the sequencer. Every track has its own pattern, length, rate, direction (forward,
 * reverse, ping-pong, snake, or Silt's graph: cords say where the playhead goes next) and clock
 * (the master, an LFO, dust, or another track through an influence CLOCK route: René-style).
 * Steps carry note, velocity, length, chance, microtiming, ratchets, a condition, two parameter
 * locks, a library articulation, a microcut and graph cords. Scenes and the arrangement switch
 * patterns on the bar; drawn density arcs thin a part out without new patterns.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

static event_t evq[RV_EVQ];
static int evn;
static uint32_t now_s;                              /* the sample clock: samples since boot */
static seqtrk_t sq[RV_TRACKS];
static uint8_t playing;
static uint32_t mpulse;                             /* master pulses since start */
static float mpulse_f;
static int8_t pend_scene = -1, pend_pat[RV_TRACKS];
static uint8_t arr_row, arr_bar_left;
static uint32_t song_bar;
static uint8_t fill_on;
static uint32_t seq_rng = 0x5EED5EEDu;
static uint64_t turn_bits[RV_TRACKS];

/* ------------------------------------------------- graph mode and heads --- */
static int is_kit(int t) { return rv.tp[t][TP_KIND] == KIND_PERC && rv.tp[t][TP_ENGINE] == 1; }
static int is_graph(int t) { return !is_kit(t) && (rv.tp[t][TP_SEQMODE] == SEQ_GRAPH || rv.tp[t][TP_DIR] == DIR_GRAPH); }
/* HEAD 2 picks the pair: OFF or NOTES (head 1 plays notes too), MELODY (head 2 sets the pitch,
 * head 1 plays it: ROLE_PITCH) or RHYTHM (head 2 plays head 1's pitches: ROLE_GATE). Head 1's role
 * follows, so no setting leaves the track silent. */
static int head_role(int t, int h)
{
    int r2 = is_graph(t) ? clampi(rv.tp[t][TP_H2ROLE], 0, 3) : ROLE_OFF;
    if (h == 1)
        return r2;
    return r2 == ROLE_PITCH ? ROLE_GATE : r2 == ROLE_GATE ? ROLE_PITCH : ROLE_NOTES;
}
static const float head_rates[9] = {0.25f, 0.3333f, 0.5f, 0.6667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};

/* a head's function generator (Silt's traversal shape): 0..1 over CYCLE steps travelled, or a
 * modulator's level */
static float fg_value(int t, int h, float *pos)
{
    int shape = clampi(rv.tp[t][h ? TP_H2SHAPE : TP_H1SHAPE], 0, FG_N - 1);
    int cyc = clampi(rv.tp[t][h ? TP_H2CYCLE : TP_H1CYCLE], 2, 64);
    float u = (float)(sq[t].travelled[h] % (uint32_t)cyc) / (float)cyc;
    *pos = u;
    switch (shape) {
    case FG_RISE: return u;
    case FG_FALL: return 1.0f - u;
    case FG_ARCH: return sin_lut(u * 0.5f);
    case FG_DECAY: return fm_expf(-8.0f * u);
    case FG_SWELL: return 0.5f - 0.5f * sin_lut(u + 0.25f);
    case FG_LFO1: return 0.5f + 0.5f * lfo[0].value;
    case FG_LFO2: return 0.5f + 0.5f * lfo[1].value;
    case FG_DUST: return clampf(dust_v, 0.0f, 1.0f);
    default: return 0.0f;
    }
}

/* a head's rate now, in steps per master clock pulse: its RATE, moved by its function generator,
 * either times the clock (x CLOCK: up to two octaves faster or slower) or plus it (+ CLOCK) */
static float head_rate(int t, int h)
{
    float r = head_rates[clampi(rv.tp[t][h ? TP_H2RATE : TP_RATE], 0, 8)], pos, v, d;
    int shape = rv.tp[t][h ? TP_H2SHAPE : TP_H1SHAPE];
    if (!is_graph(t) || shape == FG_OFF) {
        rv_live.fg[t][h] = 0.0f;
        rv_live.fg_rate[t][h] = r;
        return r;
    }
    v = fg_value(t, h, &pos);
    d = rv.tp[t][h ? TP_H2DEPTH : TP_H1DEPTH] * (1.0f / 64.0f);
    if (rv.tp[t][h ? TP_H2COMB : TP_H1COMB] == COMB_ADD)
        r = r + d * v * 2.0f;
    else
        r = r * fm_exp2f(d * v * 2.0f);
    r = clampf(r, 0.03f, 8.0f);
    rv_live.fg[t][h] = v;
    rv_live.fg_pos[t][h] = pos;
    rv_live.fg_rate[t][h] = r;
    return r;
}
#define LATENCY_TICKS 6                             /* sequenced notes sound 6/24 step late: room for early micro */

static void infl_fire(int a, int note, int vel, uint16_t chain, uint32_t when);
static void note_on_internal(int t, int note, int vel, int env, uint16_t chain);
static void note_off_internal(int t, int note);
static void all_off_now(void);

static void ev_push(const event_t *e)
{
    if (evn < RV_EVQ)
        evq[evn++] = *e;
}

static float step_seconds(int t)
{
    static const float rates[9] = {0.25f, 0.3333f, 0.5f, 0.6667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    return clk_period / rates[clampi(rv.tp[t][TP_RATE], 0, 8)];
}

static int pattern_empty(int t)
{
    int i;
    const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    for (i = 0; i < RV_STEPS; i++)
        if (p->s[i].flags & ST_ON)
            return 0;
    return 1;
}

/* a step's rank in its bar, downbeats first: what density arcs thin out last */
static int step_rank(int si)
{
    int r = si & 15;
    return ((r & 1) << 3) | ((r & 2) << 1) | ((r & 4) >> 1) | ((r & 8) >> 3);
}

static const uint16_t scale_masks[12] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x295, 0x4A9, 0x555, 0x18D, 0x463};

static int in_scale(int n)
{
    int sc = clampi(rv.gp[GP_SCALE], 0, 11), root = clampi(rv.gp[GP_ROOT], 0, 11);
    int k = ((n - root) % 12 + 12) % 12;
    return (scale_masks[sc] >> k) & 1u;              /* the masks hold C in bit 0 */
}

/* the nearest note of the scale (a tie goes down) */
int rv_quantize_note(int n)
{
    int d;
    if (clampi(rv.gp[GP_SCALE], 0, 11) == 0)
        return n;
    for (d = 0; d < 7; d++) {
        if (in_scale(n - d))
            return n - d;
        if (in_scale(n + d + 1) && !in_scale(n - d - 1))
            return n + d + 1;
    }
    return n;
}

/* the keys, folded (as Elektron's fold): with a scale set, key k plays the k-th scale note from
 * the first one at or above the keyboard's lowest key, so every key plays and none is out */
int rv_fold_key(int base, int k)
{
    int n = base, i;
    if (clampi(rv.gp[GP_SCALE], 0, 11) == 0)
        return base + k;
    while (!in_scale(n))
        n++;
    for (i = 0; i < k; i++)
        do
            n++;
        while (!in_scale(n));
    return n;
}

static int cond_pass(const rv_step *s, int t)
{
    uint32_t p = sq[t].passes;
    switch (s->cond) {
    case 0: return 1;
    case 1: return p == 0;
    case 2: return p != 0;
    case 3: return p % 2 == 0;
    case 4: return p % 2 == 1;
    case 5: return p % 3 == 0;
    case 6: return p % 3 == 1;
    case 7: return p % 3 == 2;
    case 8: return p % 4 == 0;
    case 9: return p % 4 == 1;
    case 10: return p % 4 == 2;
    case 11: return p % 4 == 3;
    case 12: return fill_on;
    case 13: return !fill_on;
    default: return 1;
    }
}

/* the step a head goes to next */
static int next_step(int t, int h, int cur)
{
    int len = clampi(rv.tp[t][TP_LEN], 1, RV_STEPS), dir = is_graph(t) ? DIR_GRAPH : clampi(rv.tp[t][TP_DIR], 0, DIR_N - 1), n;
    seqtrk_t *s = &sq[t];
    if (cur < 0) {                                  /* the first step */
        if (h == 1)
            return clampi(rv.tp[t][TP_H2START] - 1, 0, len - 1);
        return dir == DIR_REV ? len - 1 : 0;
    }
    switch (dir) {
    case DIR_REV:
        n = cur - 1;
        if (n < 0) {
            n = len - 1;
            if (h == 0)
                s->passes++;
        }
        return n;
    case DIR_PING:
        if (s->dir[h] == 0)
            s->dir[h] = 1;
        n = cur + s->dir[h];
        if (n >= len || n < 0) {
            s->dir[h] = (int8_t)-s->dir[h];
            n = cur + s->dir[h];
            if (n < 0)
                n = 0;
            if (n >= len)
                n = len - 1;
            if (h == 0 && s->dir[h] > 0)
                s->passes++;
        }
        return n;
    case DIR_SNAKE: {
        /* steps as rows of four, walked like a snake: the playhead position k maps to a step */
        int k = 0, r, c;
        for (k = 0; k < len; k++) {
            r = k / 4;
            c = k % 4;
            if (r * 4 + ((r & 1) ? 3 - c : c) == cur)
                break;
        }
        k++;
        if (k >= len) {
            k = 0;
            if (h == 0)
                s->passes++;
        }
        r = k / 4;
        c = k % 4;
        n = r * 4 + ((r & 1) ? 3 - c : c);
        return n < len ? n : k;
    }
    case DIR_GRAPH: {
        const rv_step *st = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[cur];
        int a = st->to[0] && st->to[0] <= len ? st->to[0] - 1 : (cur + 1) % len;   /* past LENGTH: NEXT */
        n = a;
        if (st->to[1] && st->to[1] <= len) {
            int take2 = 0, br = clampi(rv.tp[t][TP_BRANCH], 0, 4);
            if (br == 0)
                take2 = xs_uni(&seq_rng) < 0.5f;
            else if (br == 1) {
                turn_bits[t] ^= (uint64_t)1 << cur;
                take2 = (int)((turn_bits[t] >> cur) & 1u);
            } else if (br <= 3)
                take2 = lfo[br - 2].value > 0.0f;
            else {                                   /* MOTION: the head's function generator */
                float pos;
                take2 = fg_value(t, h, &pos) > 0.5f;
            }
            if (take2)
                n = st->to[1] - 1;
        }
        n = clampi(n, 0, len - 1);
        if (n <= cur && h == 0 && n == 0)
            s->passes++;
        return n;
    }
    default:
        n = cur + 1;
        if (n >= len) {
            n = 0;
            if (h == 0)
                s->passes++;
        }
        return n;
    }
}

/* a step plays: locks, then notes (with ratchets), jumps and cuts, all scheduled */
/* a KIT's step: each lane reads its own step (a lane with its own LENGTH runs against the others),
 * a step holds which lanes play (note: a bit per lane), which are accented (env) and which ratchet
 * (len); velocity, chance, timing and conditions belong to the step, and each lane adds its chance */
static void kit_fire(int t, int si, uint32_t when)
{
    const rv_kit *k = &rv.kit[t];
    rv_pattern *pt = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    int tlen = clampi(rv.tp[t][TP_LEN], 1, RV_STEPS), l, r;
    float sd = step_seconds(t), tick = sd * (1.0f / 24.0f), quant = rv.gp[GP_QUANT] * 0.01f;
    float density = rv.arcs[t][song_bar & 15];
    for (l = 0; l < RV_LANES; l++) {
        int len = k->len[l] ? clampi(k->len[l], 1, RV_STEPS) : tlen, li, cnt, curve, sw;
        const rv_step *s;
        float chance;
        event_t e;
        li = len == tlen ? si : (int)((sq[t].travelled[0] - 1u) % (uint32_t)len);
        rv_live.lane_step[t][l] = (int8_t)li;
        s = &pt->s[li];
        if (!(s->flags & ST_ON) || !((s->note >> l) & 1))
            continue;
        if (!cond_pass(s, t))
            continue;
        chance = (float)s->chance * (float)k->chance[l] * 0.01f;
        if (chance < 100.0f && xs_uni(&seq_rng) * 100.0f >= chance)
            continue;
        if (density < 100 && (float)step_rank(li) >= density * 0.16f)
            continue;
        sw = (li & 1) ? rv.gp[GP_SWING] : 0;
        e.when = when + (uint32_t)(((float)LATENCY_TICKS + (float)s->micro * (1.0f - quant)) * tick * RV_FS + (float)sw * 0.005f * sd * RV_FS);
        e.track = (uint8_t)t;
        e.chain = (uint16_t)(1u << t);
        e.env = (uint8_t)(rv.pat_cur[t] % RV_PATTERNS);   /* the pattern and step its locks are found under */
        e.step = (uint8_t)li;
        e.head = 0x40;                                /* a kit hit: look up its locks */
        e.type = EV_ON;
        e.note = (uint8_t)(60 + l);
        e.len_s = 0.0f;
        cnt = ((s->len >> l) & 1) ? (s->ratchet & 7) + 1 : 1;
        curve = (s->ratchet >> 3) & 3;
        for (r = 0; r < cnt; r++) {
            float u = (float)r / (float)cnt;
            event_t on = e;
            if (curve == 1)
                u = fm_powf(u, 0.6f);
            else if (curve == 2)
                u = fm_powf(u, 1.6f);
            on.vel = (uint8_t)(((s->env >> l) & 1) ? 127 : clampi(s->vel ? s->vel : 100, 1, 110));   /* accent */
            if ((s->ratchet & 32) && cnt > 1)
                on.vel = (uint8_t)clampi((int)((float)on.vel * (1.0f - 0.6f * u)), 1, 127);
            on.when = e.when + (uint32_t)(u * sd * RV_FS);
            ev_push(&on);
        }
    }
}

static void step_fire(int t, int h, int si, uint32_t when)
{
    rv_step *s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[si];
    float sd = step_seconds(t), tick = sd * (1.0f / 24.0f);
    int k, note, cnt, curve, sw;
    int role = head_role(t, h), tt = t;               /* head 2 may play another track */
    if (h == 1 && rv.tp[t][TP_H2TARGET] > 0)
        tt = clampi(rv.tp[t][TP_H2TARGET] - 1, 0, RV_TRACKS - 1);
    float quant = rv.gp[GP_QUANT] * 0.01f, micro, density;
    event_t e;
    if (h == 0) {                                   /* the locks hold for this step */
        for (k = 0; k < TP_N; k++)
            trk[t].lockon[k] = 0;
        for (k = 0; k < 2; k++)
            if (s->lp[k] && s->lp[k] - 1 < TP_N) {
                int p = s->lp[k] - 1;
                const rv_pinfo *pi = rv_pinfo_track(t, p);
                trk[t].lockon[p] = 1;
                trk[t].lockv[p] = (int16_t)(pi->min + s->lv[k]);
            }
        rv_live.step[t][0] = (int16_t)si;
        tape_boundary(&trk[t], 1);
    } else {
        rv_live.step[t][1] = (int16_t)si;
    }
    if (is_kit(t)) {
        if (h == 0)
            kit_fire(t, si, when);
        return;
    }
    if (!(s->flags & ST_ON))
        return;
    if (!cond_pass(s, t))
        return;
    if (s->chance < 100 && xs_uni(&seq_rng) * 100.0f >= (float)s->chance)
        return;
    density = rv.arcs[t][song_bar & 15];
    if (density < 100 && (float)step_rank(si) >= density * 0.16f)
        return;
    micro = (float)s->micro * (1.0f - quant);
    sw = (si & 1) ? rv.gp[GP_SWING] : 0;
    e.when = when + (uint32_t)(((float)LATENCY_TICKS + micro) * tick * RV_FS + (float)sw * 0.005f * sd * RV_FS);
    e.track = (uint8_t)tt;
    e.chain = (uint16_t)(1u << tt);
    e.env = s->env;
    e.step = (uint8_t)si;
    e.head = (uint8_t)h;
    if (s->cut && role != ROLE_PITCH) {
        e.type = EV_CUT;
        e.note = s->cut;
        ev_push(&e);
    }
    if ((s->flags & ST_JUMP) && role != ROLE_PITCH) {
        e.type = EV_JUMP;
        e.note = s->note;
        ev_push(&e);
        return;
    }
    note = s->note;
    if (rv.tp[tt][TP_KIND] != KIND_TAPE) {
        int ivl = clampi(rv.tp[t][TP_INTERVAL], 1, 12);
        if (is_graph(t))
            note += rv.tp[t][h ? TP_H2TRANS : TP_H1TRANS];
        note += rv.gp[GP_TRANSPOSE];
        note = rv_quantize_note(note);
        if (role == ROLE_PITCH) {                    /* a PITCH head only sets the pitch */
            sq[t].pitch_reg = (int16_t)note;
            sq[t].pitch_set = 1;
            return;
        }
        if (role == ROLE_GATE && sq[t].pitch_set)    /* a GATE head plays the PITCH head's note */
            note = sq[t].pitch_reg;
        else if (ivl > 1 && ((note - rv.gp[GP_ROOT]) % ivl + ivl) % ivl)
            return;                                 /* INTERVAL: only notes a multiple of N semitones from the root */
    } else if (role == ROLE_PITCH) {
        return;
    }
    cnt = (s->ratchet & 7) + 1;
    curve = (s->ratchet >> 3) & 3;
    for (k = 0; k < cnt; k++) {
        float u = (float)k / (float)cnt, len_s;
        if (curve == 1)
            u = fm_powf(u, 0.6f);
        else if (curve == 2)
            u = fm_powf(u, 1.6f);
        e.type = EV_ON;
        e.note = (uint8_t)clampi(note, 0, 127);
        e.vel = (uint8_t)clampi((int)((float)s->vel * ((s->ratchet & 32) ? 1.0f - 0.6f * (float)k / (float)cnt : 1.0f)), 1, 127);
        len_s = cnt > 1 ? sd / (float)cnt * 0.7f : sd * (float)(s->len ? s->len : 1) * 0.125f;
        e.len_s = len_s;
        {
            event_t on = e, off = e;
            on.when = e.when + (uint32_t)(u * sd * (float)(cnt > 1 ? 1 : 0) * RV_FS);
            if (s->flags & ST_TIE)
                on.head |= 0x80u;                   /* tied: no note-off; the track's next note ends it */
            ev_push(&on);
            if (!(s->flags & ST_TIE)) {
                off.type = EV_OFF;
                off.when = on.when + (uint32_t)(len_s * RV_FS);
                ev_push(&off);
            }
        }
    }
}

static int track_clocked_by_route(int t)
{
    int a;
    for (a = 0; a < RV_TRACKS; a++)
        if (rv.infl[a][t].type == RT_CLOCK)
            return 1;
    return 0;
}

/* one head moves one step and plays it */
static void head_advance(int t, int h, uint32_t when)
{
    seqtrk_t *s = &sq[t];
    s->step[h] = (int16_t)next_step(t, h, s->step[h]);
    s->travelled[h]++;
    if (h == 0) {
        s->step_time = when;
        s->step_dur = step_seconds(t);
        if (s->first_take)
            s->take_steps++;
    }
    step_fire(t, h, s->step[h], when);
}

static int master_clocked(int t) { return rv.tp[t][TP_CLKSRC] == 0 && !track_clocked_by_route(t); }

/* a track's step from another clock (an LFO, dust, a route): head 1 moves, and head 2 follows at
 * its rate relative to head 1. On the master clock each head is scheduled on its own. */
static void track_advance(int t, uint32_t when)
{
    seqtrk_t *s = &sq[t];
    head_advance(t, 0, when);
    if (head_role(t, 1) != ROLE_OFF && !master_clocked(t)) {
        s->next_at[1] += head_rate(t, 1) / head_rate(t, 0);
        while (s->next_at[1] >= 1.0f) {
            s->next_at[1] -= 1.0f;
            head_advance(t, 1, when);
        }
    }
}

/* the bar: scenes, patterns and the arrangement switch here */
static void bar_boundary(void)
{
    int t;
    song_bar++;
    if (rv.gp[GP_SONGMODE] == 1 && rv.arr_len) {
        if (arr_bar_left > 0)
            arr_bar_left--;
        if (arr_bar_left == 0) {
            arr_row = (uint8_t)((arr_row + 1) % rv.arr_len);
            arr_bar_left = rv.arr[arr_row].bars ? rv.arr[arr_row].bars : 1;
            pend_scene = (int8_t)rv.arr[arr_row].scene;
        }
    }
    if (pend_scene >= 0) {
        const rv_scene *sc = &rv.scene[pend_scene & 15];
        if (sc->used) {
            for (t = 0; t < RV_TRACKS; t++) {
                rv.pat_cur[t] = sc->pattern[t] % RV_PATTERNS;
                rv.tp[t][TP_LEVEL] = sc->level[t];
                sq[t].passes = 0;
            }
            rv.mute = sc->mute;
            for (t = 0; t < RV_MACROS; t++)
                rv.gp[GP_MACRO1 + t] = sc->macro[t];
            {
                int a, b;
                for (a = 0; a < RV_TRACKS; a++)
                    for (b = 0; b < RV_TRACKS; b++)
                        rv.infl[a][b] = sc->infl[a][b];
            }
            if (sc->resample)
                resample_now();
        }
        rv.scene_cur = (uint8_t)(pend_scene & 15);
        pend_scene = -1;
    }
    for (t = 0; t < RV_TRACKS; t++)
        if (pend_pat[t] >= 0) {
            rv.pat_cur[t] = (uint8_t)(pend_pat[t] % RV_PATTERNS);
            pend_pat[t] = -1;
            sq[t].passes = 0;
        }
}

/* a master pulse at sample offset off in this control step */
static void seq_pulse(int off)
{
    int t;
    uint32_t base = now_s + (uint32_t)off;
    if (!playing)
        return;
    if ((mpulse & 15) == 0 && mpulse)
        bar_boundary();
    rv_live.beat = (uint8_t)((mpulse >> 2) & 3);
    rv_live.bar = (uint8_t)(song_bar & 255);
    for (t = 0; t < RV_TRACKS; t++) {
        seqtrk_t *s = &sq[t];
        static const float rates[9] = {0.25f, 0.3333f, 0.5f, 0.6667f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
        float r = rates[clampi(rv.tp[t][TP_RATE], 0, 8)];
        if ((mpulse & 3) == 0)
            tape_boundary(&trk[t], 2);
        if ((mpulse & 15) == 0)
            tape_boundary(&trk[t], 3);
        if (!master_clocked(t))
            continue;
        {
            int h, heads = head_role(t, 1) != ROLE_OFF ? 2 : 1;
            for (h = 0; h < heads; h++) {
                int guard = 16;
                while (s->next_at[h] < mpulse_f + 1.0f && guard--) {
                    event_t e;
                    float d = (s->next_at[h] - mpulse_f) * clk_period;
                    e.type = EV_ADV;
                    e.track = (uint8_t)t;
                    e.head = (uint8_t)h;
                    e.when = base + (uint32_t)(d > 0.0f ? d * RV_FS : 0.0f);
                    ev_push(&e);
                    s->next_at[h] += 1.0f / (h == 0 && !is_graph(t) ? r : head_rate(t, h));
                }
                if (s->next_at[h] < mpulse_f)           /* (a guard tripped: catch up, don't burst) */
                    s->next_at[h] = mpulse_f + 1.0f;
            }
        }
    }
    mpulse++;
    mpulse_f += 1.0f;
}

/* other clocks: LFO wraps and dust fire the tracks that follow them */
static void seq_other_clocks(void)
{
    int t;
    if (!playing)
        return;
    for (t = 0; t < RV_TRACKS; t++) {
        int src = rv.tp[t][TP_CLKSRC];
        if ((src == 1 && lfo_wrapped[0]) || (src == 2 && lfo_wrapped[1]) || (src == 3 && dust_fired))
            track_advance(t, now_s);
    }
}

static void seq_start(void)
{
    int t;
    clock_reset();
    mpulse = 0;
    mpulse_f = 0.0f;
    song_bar = 0;
    arr_row = 0;
    arr_bar_left = rv.arr_len ? (rv.arr[0].bars ? rv.arr[0].bars : 1) : 0;
    if (rv.gp[GP_SONGMODE] == 1 && rv.arr_len)
        pend_scene = (int8_t)rv.arr[0].scene;
    for (t = 0; t < RV_TRACKS; t++) {
        sq[t].tied = 0;
        sq[t].step[0] = sq[t].step[1] = -1;
        sq[t].dir[0] = sq[t].dir[1] = 1;
        sq[t].next_at[0] = 0.0f;
        sq[t].next_at[1] = 0.0f;
        sq[t].passes = 0;
        sq[t].travelled[0] = sq[t].travelled[1] = 0;
        sq[t].pitch_set = 0;
        turn_bits[t] = 0;
    }
    if (pend_scene >= 0)
        bar_boundary();
    playing = 1;
}

static void seq_stop(void)
{
    int t, k;
    playing = 0;
    evn = 0;
    for (t = 0; t < RV_TRACKS; t++) {
        for (k = 0; k < TP_N; k++)
            trk[t].lockon[k] = 0;
        rv_live.step[t][0] = rv_live.step[t][1] = -1;
        sq[t].tied = 0;
    }
    all_off_now();
}

/* events due in this control step */
static void seq_events(int n)
{
    int i = 0;
    uint32_t end = now_s + (uint32_t)n;
    while (i < evn) {
        event_t e = evq[i];
        if ((int32_t)(e.when - end) >= 0) {
            i++;
            continue;
        }
        evq[i] = evq[--evn];
        switch (e.type) {
        case EV_ADV:
            if (playing)
                head_advance(e.track, e.head & 1, e.when);
            break;
        case EV_ON: {
            seqtrk_t *q = &sq[e.track];
            if (q->tied && q->tied - 1 == e.note) {
                /* a tie into the same note carries on: no new attack */
            } else if (e.head & 0x40) {               /* a kit lane's hit: with its step's locks */
                perc_lock_prepare(e.track, e.note - 60, e.env, e.step);
                note_on_internal(e.track, e.note, e.vel, 0, e.chain);
            } else {
                note_on_internal(e.track, e.note, e.vel, e.env, e.chain);
                if (q->tied)
                    note_off_internal(e.track, q->tied - 1);   /* the tied note ends as the next begins */
            }
            q->tied = (uint8_t)((e.head & 0x80u) ? e.note + 1 : 0);
            break;
        }
        case EV_OFF:
            note_off_internal(e.track, e.note);
            break;
        case EV_JUMP:
            tape_track_jump(&trk[e.track], e.note & 15, -1, 0);
            break;
        case EV_CUT:
            cut_now(e.note);
            break;
        case EV_TRIG:
            note_on_internal(e.track, e.note, e.vel, 0, e.chain);
            {
                event_t off = e;
                off.type = EV_OFF;
                off.when = e.when + (uint32_t)(step_seconds(e.track) * 0.5f * RV_FS);
                ev_push(&off);
            }
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------- influence --- */
static void infl_fire(int a, int note, int vel, uint16_t chain, uint32_t when)
{
    int b;
    for (b = 0; b < RV_TRACKS; b++) {
        const rv_route *r = &rv.infl[a][b];
        event_t e;
        if (r->type == RT_OFF)
            continue;
        switch (r->type) {
        case RT_TRIG:
            /* a chain never re-enters a track it has already played this time round */
            if (chain & (1u << b))
                break;
            if (r->chance && r->chance < 100 && xs_uni(&seq_rng) * 100.0f >= (float)r->chance)
                break;
            e.type = EV_TRIG;
            e.track = (uint8_t)b;
            e.note = (uint8_t)clampi(note + (r->amt > 0 ? 0 : 0), 0, 127);
            e.vel = (uint8_t)clampi(vel * (64 + (r->amt < 0 ? r->amt : 0)) / 64, 1, 127);
            e.chain = (uint16_t)(chain | (1u << b));
            e.when = when + (uint32_t)((float)r->arg * step_seconds(a) * RV_FS);
            ev_push(&e);
            rv_live.route_fire[a][b] = 255;
            break;
        case RT_TAPE:
            if (r->arg >= 16) {
                if (!rv_tape_frozen())
                    catch_now();
            } else {
                tape_track_jump(&trk[b], r->arg, -1, 1);
            }
            rv_live.route_fire[a][b] = 255;
            break;
        case RT_CLOCK:
            if (playing && !(chain & (1u << b) && a != b))
                track_advance(b, when);
            rv_live.route_fire[a][b] = 255;
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------- the API side --- */
static void seq_transport_now(int run)
{
    if (run && !playing)
        seq_start();
    else if (!run && playing)
        seq_stop();
    rv_live.playing = playing;
}

static void scene_launch_now(int s)
{
    if (!playing) {
        pend_scene = (int8_t)(s & 15);
        bar_boundary();
        song_bar = 0;
        return;
    }
    pend_scene = (int8_t)(s & 15);
}

void rv_scene_store(int s)
{
    rv_scene *sc = &rv.scene[s & 15];
    int t, b;
    sc->used = 1;
    for (t = 0; t < RV_TRACKS; t++) {
        sc->pattern[t] = rv.pat_cur[t];
        sc->level[t] = (uint8_t)rv.tp[t][TP_LEVEL];
        for (b = 0; b < RV_TRACKS; b++)
            sc->infl[t][b] = rv.infl[t][b];
    }
    sc->mute = rv.mute;
    for (t = 0; t < RV_MACROS; t++)
        sc->macro[t] = (int8_t)rv.gp[GP_MACRO1 + t];
}

static void pattern_launch_now(int t, int p)
{
    if (!playing) {
        rv.pat_cur[t & 7] = (uint8_t)(p % RV_PATTERNS);
        return;
    }
    pend_pat[t & 7] = (int8_t)(p % RV_PATTERNS);
}

/* ---------------------------------------------------------- kit locks --- */
int rv_kitlock_find(int t, int pat, int step, int lane, int field)
{
    const rv_kitlock *k = rv.kitlk[t & 7];
    uint8_t st = (uint8_t)((step & 63) | ((pat & 1) << 6)), w = (uint8_t)(lane << 4 | field);
    int i;
    for (i = 0; i < RV_KITLOCKS; i++)
        if (k[i].what == w && k[i].step == st)
            return i;
    return -1;
}

int rv_kitlock_set(int t, int pat, int step, int lane, int field, int v)
{
    rv_kitlock *k = rv.kitlk[t & 7];
    int i = rv_kitlock_find(t, pat, step, lane, field);
    if (i < 0)
        for (i = 0; i < RV_KITLOCKS && k[i].what != 0xFF; i++)
            ;
    if (i >= RV_KITLOCKS)
        return 0;
    k[i].step = (uint8_t)((step & 63) | ((pat & 1) << 6));
    k[i].what = (uint8_t)(lane << 4 | field);
    k[i].val = (uint8_t)clampi(v, 0, 127);
    return 1;
}

void rv_kitlock_clear(int t, int pat, int step, int lane)
{
    rv_kitlock *k = rv.kitlk[t & 7];
    int i;
    for (i = 0; i < RV_KITLOCKS; i++)
        if (k[i].what != 0xFF && (step < 0 || (k[i].step & 63) == (step & 63)) && ((k[i].step >> 6) & 1) == (pat & 1) &&
            (lane < 0 || (k[i].what >> 4) == lane))
            k[i].what = 0xFF;
}

void rv_pattern_clear(int t)
{
    int i;
    rv_pattern *p = &rv.pat[t & 7][rv.pat_cur[t & 7] % RV_PATTERNS];
    rv_kitlock_clear(t, rv.pat_cur[t & 7] % RV_PATTERNS, -1, -1);   /* a kit's locks go with its steps */
    for (i = 0; i < RV_STEPS; i++) {
        rv_step *s = &p->s[i];
        uint8_t *b = (uint8_t *)s;
        int k;
        for (k = 0; k < (int)sizeof(*s); k++)
            b[k] = 0;
        s->note = 60;
        s->vel = 100;
        s->len = 4;
        s->chance = 100;
    }
}

static uint32_t tap_last, tap_n;
static float tap_avg;
static void tap_tempo_now(void)
{
    uint32_t t = now_s;
    float d = (float)(t - tap_last) * RV_DT;
    tap_last = t;
    if (d > 0.2f && d < 2.0f) {
        tap_avg = tap_n ? tap_avg * 0.6f + d * 0.4f : d;
        tap_n++;
        if (tap_n >= 2)
            rv.gp[GP_TEMPO] = (int16_t)clampi((int)(60.0f / tap_avg + 0.5f), 30, 240);
    } else {
        tap_n = 0;
    }
}

static void record_arm_now(int t, int on)
{
    seqtrk_t *s = &sq[t & 7];
    if (on) {
        rv_live.recording = 1;
        rv_live.rec_track = (uint8_t)(t & 7);
        s->first_take = (uint8_t)pattern_empty(t & 7);
        s->take_steps = 0;
    } else {
        if (s->first_take && s->take_steps > 0) {
            int len = (s->take_steps + 3) & ~3;
            rv.tp[t & 7][TP_LEN] = (int16_t)clampi(len, 4, RV_STEPS);
        }
        s->first_take = 0;
        rv_live.recording = 0;
    }
}

/* where a live event lands: the step and its micro offset */
static int live_step_pos(int t, int *micro)
{
    seqtrk_t *s = &sq[t];
    float sd = s->step_dur > 0.0f ? s->step_dur : step_seconds(t);
    float frac = (float)(now_s - s->step_time) * RV_DT / sd - (float)LATENCY_TICKS / 24.0f;
    int si = s->step[0] < 0 ? 0 : s->step[0], len = clampi(rv.tp[t][TP_LEN], 1, RV_STEPS);
    if (frac > 0.5f) {
        si = (si + 1) % len;
        frac -= 1.0f;
    }
    *micro = clampi((int)(frac * 24.0f + (frac >= 0.0f ? 0.5f : -0.5f)), -6, 12);
    return si;
}

static void record_note(int t, int note, int vel)
{
    int micro, si, k;
    rv_step *s;
    if (!playing || !rv_live.recording || rv_live.rec_track != t)
        return;
    si = live_step_pos(t, &micro);
    s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[si];
    if (is_kit(t)) {                                  /* a kit: the key's lane plays on this step */
        if (vel > 0) {
            if (!(s->flags & ST_ON)) {
                s->note = 0;
                s->env = 0;
                s->len = 0;
                s->vel = 100;
                s->chance = 100;
                s->micro = (int8_t)micro;
            }
            s->flags |= ST_ON;
            s->note |= (uint8_t)(1u << perc_lane_of(note));
        }
        return;
    }
    if (vel > 0) {
        s->flags = ST_ON;
        s->note = (uint8_t)note;
        s->vel = (uint8_t)vel;
        s->micro = (int8_t)micro;
        s->len = 4;
        for (k = 0; k < 16; k++)
            if (sq[t].held_note[k] < 0 || k == 15) {
                sq[t].held_note[k] = (int8_t)si;
                sq[t].held_on[k] = now_s;
                break;
            }
        (void)note;
    } else {
        for (k = 0; k < 16; k++)
            if (sq[t].held_note[k] >= 0 && rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[sq[t].held_note[k]].note == note) {
                rv_step *st = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[sq[t].held_note[k]];
                float steps = (float)(now_s - sq[t].held_on[k]) * RV_DT / (sq[t].step_dur > 0.0f ? sq[t].step_dur : 0.125f);
                st->len = (uint8_t)clampi((int)(steps * 8.0f + 0.5f), 1, 127);
                sq[t].held_note[k] = -1;
                break;
            }
    }
}

/* a knob moved while recording: write it into the playing step as a lock */
static void motion_now(int pid, int v)
{
    int t, p, k, slot = -1;
    rv_step *s;
    const rv_pinfo *pi;
    if (!playing || !rv_live.recording || pid >= RV_GPID)
        return;
    t = pid / RV_TP;
    p = pid % RV_TP;
    if (sq[t].step[0] < 0)
        return;
    s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[sq[t].step[0]];
    for (k = 0; k < 2; k++)
        if (s->lp[k] == p + 1)
            slot = k;
    if (slot < 0)
        slot = !s->lp[0] ? 0 : !s->lp[1] ? 1 : 1;
    pi = rv_pinfo_track(t, p);
    s->lp[slot] = (uint8_t)(p + 1);
    s->lv[slot] = (uint8_t)clampi(v - pi->min, 0, 255);
}

static void record_jump(int t, int pos)
{
    int micro, si;
    rv_step *s;
    if (!playing || !rv_live.recording || rv_live.rec_track != t)
        return;
    si = live_step_pos(t, &micro);
    s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[si];
    s->flags = ST_ON | ST_JUMP;
    s->note = (uint8_t)pos;
    s->micro = (int8_t)micro;
}

static void seq_init(void)
{
    int t, k;
    for (t = 0; t < RV_TRACKS; t++) {
        pend_pat[t] = -1;
        sq[t].step[0] = sq[t].step[1] = -1;
        for (k = 0; k < 16; k++)
            sq[t].held_note[k] = -1;
        rv_live.step[t][0] = rv_live.step[t][1] = -1;
    }
    clock_reset();
}
