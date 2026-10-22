/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the capture tape and the tape tracks.
 *
 * One tape, in Beads' own formats (clean 16-bit, tape 12-bit, dirty 8-bit mu-law), always
 * listening to its source until it is caught. Positions are in "tape time": u = 0 is the
 * oldest sample, u = 1 the newest; rows are spans of u. Tape tracks play it five ways: MLR
 * playheads (keys jump, two keys loop), slices cut at its onsets, and the sample voices: KEYS (a
 * span played across the keys, up to four at once), STRETCH (its speed apart from its pitch) and
 * SHARDS (the span broken into pieces, played one at a time in an order). A tape KIT plays six
 * lanes, each its own span of the tape. START and END trim the row for every mode.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

static uint8_t tape_buf[RV_TAPE_BYTES] RV_BIG;
static bd_tape tape;
static samp_t sv[RV_SAMP_VOICES];
static uint32_t samp_age;
static int tape_res = -1;
static uint8_t tape_frozen;
static uint8_t tape_punch;                          /* writing into the row under head 1 */
static int8_t tape_punch_track = -1;
static int32_t resample_left = -1;                  /* samples still to record, -1 = idle */
static int8_t resample_wait;                        /* waiting for the bar */
static float tape_in_lp, tape_in_hold;
static int32_t tape_in_n;

static int32_t tape_len(void) { return tape.max_delay > 8 ? tape.max_delay : 8; }

static void tape_setup(int res)
{
    bd_tape_setup(&tape, tape_buf, sizeof(tape_buf), (bd_res)res, 1, 0);
    tape_res = res;
    tape_frozen = 0;
}

float rv_tape_seconds(void) { return (float)tape_len() / (tape_res == BD_RES_8 ? 22050.0f : (float)RV_FS); }
int rv_tape_frozen(void) { return tape_frozen; }

/* the u span of a row */
static void row_span(int row, float *s, float *e)
{
    int r = clampi(row, 0, 3);
    float a = rv.gp[GP_ROW1S + 2 * r] * (1.0f / 128.0f), b = rv.gp[GP_ROW1E + 2 * r] * (1.0f / 128.0f);
    if (b <= a + 0.004f)
        b = a + 0.004f;
    if (b > 1.0f)
        b = 1.0f;
    *s = a;
    *e = b;
}

/* the part of a row a track plays: its row trimmed by START and END (both live, so they can move) */
static void track_span(const track_t *tr, float *s, float *e)
{
    float a, b, w, st = clampf(tr->eff[TP_S6] * (1.0f / 128.0f), 0.0f, 0.99f), en = clampf((tr->eff[TP_S7] + 1.0f) * (1.0f / 128.0f), 0.0f, 1.0f);
    row_span((int)(tr->eff[TP_S1] + 0.5f), &a, &b);
    if (en < st + 0.01f)
        en = st + 0.01f;
    w = b - a;
    *s = a + w * st;
    *e = a + w * fm_minf(en, 1.0f);
}

/* tape time u (0 oldest .. 1 newest) to a Beads delay, Q10 */
static inline int32_t u_to_delay_q10(float u)
{
    float d = (1.0f - u) * (float)(tape_len() - 2);
    return (int32_t)(d * 1024.0f);
}

static inline float tape_read_u(float u)
{
    bd_frame f = bd_tape_at(&tape, u_to_delay_q10(u));
    return (float)(f.l + f.r) * (0.5f / 32768.0f);
}

/* ----------------------------------------------------- the tape's input --- */
/* in: the source for this control step (mono, -1..1). dirty runs at half rate. */
static void tape_capture(const float *in, int n, int samples_per_bar_known, int bar_start)
{
    int i, want = clampi(rv.gp[GP_TAPEQUAL], 0, 2);
    float erode = rv.gp[GP_ERODE] * (1.0f / 127.0f), lpc = 1.0f - erode * 0.85f;
    if (want != tape_res)
        tape_setup(want);
    /* resample: wait for the bar, then record a length, then catch */
    if (resample_wait && bar_start) {
        static const int bars_tab[4] = {4, 8, 16, 2};   /* in quarter notes */
        float spb = 60.0f / (float)rv.gp[GP_TEMPO];
        int q = bars_tab[clampi(rv.gp[GP_RESBARS], 0, 3)];
        int32_t len = (int32_t)(spb * (float)q * (float)RV_FS);
        float rate = tape_res == BD_RES_8 ? 0.5f : 1.0f;
        int32_t cap = (int32_t)((float)tape_len() / rate);
        resample_wait = 0;
        if (len > cap)
            len = cap;
        resample_left = len;
        tape_frozen = 0;
        /* row 1 becomes exactly the take */
        {
            float frac = (float)len * rate / (float)tape_len();
            rv.gp[GP_ROW1S] = (int16_t)clampi((int)((1.0f - frac) * 128.0f + 0.5f), 0, 127);
            rv.gp[GP_ROW1E] = 128;
        }
    }
    (void)samples_per_bar_known;
    for (i = 0; i < n; i++) {
        float x = in[i];
        bd_frame fr;
        tape_in_lp += (x - tape_in_lp) * lpc;         /* erosion: each pass loses highs */
        x = tape_in_lp;
        if (erode > 0.3f) {
            float q = fm_exp2f(14.0f - (erode - 0.3f) * 14.0f);
            x = (float)(int32_t)(x * q) / q;
        }
        if (tape_punch && tape_frozen && tape_punch_track >= 0)
            continue;                                   /* punch-in writes happen at the head */
        if (tape_res == BD_RES_8 && (++tape_in_n & 1)) {  /* dirty: half rate */
            tape_in_hold = x;
            continue;
        }
        if (tape_res == BD_RES_8)
            x = (x + tape_in_hold) * 0.5f;
        fr.l = fr.r = (int32_t)clampf(x * 32767.0f, -32767.0f, 32767.0f);
        bd_tape_write(&tape, fr, tape_frozen);
        if (resample_left > 0 && --resample_left == 0) {
            resample_left = -1;
            tape_frozen = 1;
        }
    }
}

static void catch_now(void)
{
    tape_frozen = !tape_frozen;
    resample_left = -1;
    resample_wait = 0;
}

static void resample_now(void)
{
    resample_wait = 1;
    resample_left = -1;
}

/* the waveform for the screen: peaks of n columns across a row (or the whole tape) */
int rv_tape_overview(uint8_t *peaks, int n, int row)
{
    float s = 0.0f, e = 1.0f;
    int i, k;
    if (row >= 0)
        row_span(row, &s, &e);
    for (i = 0; i < n; i++) {
        float m = 0.0f;
        for (k = 0; k < 6; k++) {
            float u = s + (e - s) * ((float)i + (float)k / 6.0f) / (float)n, a = fm_fabsf(tape_read_u(u));
            if (a > m)
                m = a;
        }
        peaks[i] = (uint8_t)clampi((int)(m * 255.0f), 0, 255);
    }
    return n;
}

/* ------------------------------------------------------------ slices --- */
static void slices_find(track_t *tr)
{
    tapetrk_t *tt = &tr->tape;
    int want = clampi((int)(tr->eff[TP_S14] + 0.5f), 4, 32), i, found = 0;
    float s, e, sens = tr->eff[TP_S15] * (1.0f / 127.0f), prev = 0.0f, env;
    const int cols = 256;
    row_span((int)(tr->eff[TP_S1] + 0.5f), &s, &e);
    tt->slice[found++] = 0.0f;
    for (i = 1; i < cols && found < want; i++) {
        float x = (float)i / (float)cols, m = 0.0f;
        int k;
        for (k = 0; k < 4; k++) {
            float a = fm_fabsf(tape_read_u(s + (e - s) * (x + (float)k / (4.0f * cols))));
            if (a > m)
                m = a;
        }
        env = m;
        /* an onset: the level jumps by more than the sensitivity asks, and the last slice is not too near */
        if (env > prev * (1.6f - sens) + 0.02f && x - tt->slice[found - 1] > 0.5f / (float)want)
            tt->slice[found++] = x;
        prev = prev * 0.7f + env * 0.3f;
    }
    /* SENS 0, or not enough onsets: an even grid */
    if (sens <= 0.0f || found < want / 2) {
        found = want;
        for (i = 0; i < want; i++)
            tt->slice[i] = (float)i / (float)want;
    }
    tt->slice[found] = 1.0f;
    tt->nslices = (uint8_t)found;
    tt->slices_valid = 1;
}

int rv_tape_slices(int t, float *pos, int max)
{
    tapetrk_t *tt;
    int i, n;
    extern track_t trk[RV_TRACKS];
    tt = &trk[t & 7].tape;
    n = tt->slices_valid ? tt->nslices : 0;
    for (i = 0; i < n && i < max; i++)
        pos[i] = tt->slice[i];
    return n < max ? n : max;
}

/* ------------------------------------------------------- sample voices --- */
static inline float fm_fmodf(float x, float y) { return y > 0.0f ? x - fm_floorf(x / y) * y : 0.0f; }
/* the next note's shading (a ratchet's R MOVE; rv_engine.c sets it, the sample voices read it too) */
static hitshade_t hs_pend;

/* SHARDS: which shard plays k-th of n, in ORDER: FWD, REV, SKIP (evens then odds), SWAP (pairs
 * changed round), INWARD (the ends toward the middle), SHUF 1-3 (three fixed shuffles) */
static int shard_at(int order, int n, int k)
{
    static const uint8_t mul[5] = {5, 7, 11, 13, 3};
    int h = (n + 1) / 2, i;
    if (n < 2)
        return 0;
    k %= n;
    switch (order) {
    case 1: return n - 1 - k;
    case 2: return k < h ? 2 * k : 2 * (k - h) + 1;
    case 3: return (k ^ 1) < n ? (k ^ 1) : k;
    case 4: return k & 1 ? n - 1 - k / 2 : k / 2;
    case 5: case 6: case 7:
        for (i = order - 5; i < 5; i++) {             /* k * a + b (mod n), a sharing no factor with n: every shard once */
            int a = mul[i], x = a, y = n;
            while (y) {
                int r = x % y;
                x = y;
                y = r;
            }
            if (x == 1)
                return (k * a + order * 3) % n;
        }
        return k;
    default: return k;
    }
}

/* a kit lane's setting, with this hit's locks (the sequencer found them just before) */
static int lane_p(int t, int l, int f)
{
    return (perc_pend_mask >> f) & 1 ? perc_pend_val[f] : rv.kit[t & 7].p[l % RV_LANES][f & 7];
}

static void samp_note_on(track_t *tr, int t, int note, float vel, int lane)
{
    int i, pick = -1, count = 0, poly = clampi(rv.tp[t][TP_POLY], 0, 3) + 1;
    uint32_t oldest = 0xFFFFFFFFu;
    float tl = (float)tape_len(), rate_base = tape_res == BD_RES_8 ? 0.5f : 1.0f, semis;
    samp_t *v;
    for (i = 0; i < RV_SAMP_VOICES; i++)              /* a lane chokes its own last hit; a track past VOICES its oldest */
        if (sv[i].on && sv[i].track == t && (lane < 0 || sv[i].lane == lane)) {
            count++;
            if (sv[i].age < oldest) {
                oldest = sv[i].age;
                pick = i;
            }
        }
    if (count < (lane >= 0 ? 1 : poly)) {
        pick = -1;
        for (i = 0; i < RV_SAMP_VOICES && pick < 0; i++)
            if (!sv[i].on)
                pick = i;
        if (pick < 0) {
            oldest = 0xFFFFFFFFu;
            for (i = 0; i < RV_SAMP_VOICES; i++)
                if (sv[i].age < oldest) {
                    oldest = sv[i].age;
                    pick = i;
                }
        }
    }
    if (pick < 0)
        pick = 0;
    v = &sv[pick];
    v->on = 1;
    v->gate = 1;
    v->track = (uint8_t)t;
    v->lane = (int8_t)lane;
    v->note = (int8_t)note;
    v->age = ++samp_age;
    v->vel = vel;
    v->gain = 1.0f;
    v->sh_u = 0.0f;
    v->sh_k = 0;
    v->gph = 0.0f;
    v->hs = hs_pend;
    if (!v->hs.on) {
        v->hs.semi = 0.0f;
        v->hs.lpc = 1.0f;
        v->hs.gain = 1.0f;
        v->hs.dk = 0.0f;
    }
    v->hs.env = 1.0f;
    v->hs.lps = 0.0f;
    if (lane >= 0) {                                  /* a kit lane: its own row, span, pitch, playing, decay, shards */
        float a, b, st = lane_p(t, lane, 1) * (1.0f / 128.0f), en = (lane_p(t, lane, 2) + 1.0f) * (1.0f / 128.0f);
        int dec = lane_p(t, lane, 4), nsh = lane_p(t, lane, 5);
        row_span(rv.kit[t & 7].mach[lane % RV_LANES] & 3, &a, &b);
        if (en < st + 0.01f)
            en = st + 0.01f;
        v->s = a + (b - a) * st;
        v->e = a + (b - a) * fm_minf(en, 1.0f);
        semis = (float)(lane_p(t, lane, 0) - 64) * (24.0f / 64.0f);
        v->play = (uint8_t)clampi(lane_p(t, lane, 3), 0, TPLAY_N - 1);
        v->mode = nsh >= 2 ? TAPE_SHARDS : TAPE_KEYS;
        v->n_sh = (uint8_t)clampi(nsh, 2, 16);
        v->order = (uint8_t)clampi(lane_p(t, lane, 6), 0, 7);
        v->gap = clampf(lane_p(t, lane, 7) * (0.9f / 127.0f), 0.0f, 0.9f);
        v->gain = ((perc_pend_mask >> KL_LEVEL) & 1 ? perc_pend_val[KL_LEVEL] : rv.kit[t & 7].level[lane % RV_LANES]) * (1.0f / 100.0f);
        v->env = 1.0f;
        v->dk = dec >= 127 ? 0.0f : 1.0f / (0.02f * fm_exp2f((float)dec * (7.0f / 127.0f)));
        perc_pend_mask = 0;
    } else {
        track_span(tr, &v->s, &v->e);
        semis = (float)(note - 60) + tr->eff[TP_PITCH] + tr->eff[TP_FINE] * 0.01f;
        v->mode = (uint8_t)clampi((int)(tr->eff[TP_S0] + 0.5f), 0, TAPE_MODES - 1);
        v->play = (uint8_t)clampi((int)(tr->eff[TP_S8] + 0.5f), 0, TPLAY_N - 1);
        v->n_sh = (uint8_t)clampi((int)(tr->eff[TP_S10] + 0.5f), 2, 16);
        v->order = (uint8_t)clampi((int)(tr->eff[TP_S11] + 0.5f), 0, 7);
        v->env = 1.0f;
        v->dk = 0.0f;
        artic_cfg_track(&v->cfg, tr->eff);
        artic_start(&v->a, &v->cfg);
    }
    v->inc = fm_exp2f((semis + v->hs.semi) * (1.0f / 12.0f)) * rate_base / tl;
    v->dir = (int8_t)(v->play == TPLAY_REV || v->play == TPLAY_REVLOOP ? -1 : 1);
    v->pos = v->base = v->dir > 0 ? v->s : v->e;
}

static void samp_note_off(int t, int note)
{
    int i;
    for (i = 0; i < RV_SAMP_VOICES; i++)
        if (sv[i].on && sv[i].track == t && sv[i].lane < 0 && sv[i].note == note && sv[i].gate) {
            sv[i].gate = 0;
            artic_release(&sv[i].a, &sv[i].cfg);
        }
}

/* the end of a span: loop round, bounce back, or stop (returns 0 when the voice ends) */
static int samp_wrap(samp_t *v, float *p, float s, float e)
{
    int loops = v->play == TPLAY_LOOP || v->play == TPLAY_REVLOOP;
    if (*p >= e) {
        if (v->play == TPLAY_PING) {
            *p = e - (*p - e);
            v->dir = -1;
        } else if (loops) {
            *p = s + fm_fmodf(*p - e, e - s);
        } else {
            return 0;
        }
    } else if (*p < s) {
        if (v->play == TPLAY_PING) {
            *p = s + (s - *p);
            v->dir = 1;
        } else if (loops) {
            *p = e - fm_fmodf(s - *p, e - s);
        } else {
            return 0;
        }
    }
    return 1;
}

/* the sample voices of track t, into out; returns the loudest's envelope */
static float samp_render(track_t *tr, int t, float *out, int n)
{
    int k, i;
    float lvl = 0.0f, tl = (float)tape_len(), rate_base = tape_res == BD_RES_8 ? 0.5f : 1.0f;
    float edge = 96.0f * rate_base / tl;             /* about 2 ms of tape: the fades at a span's ends */
    float gap = clampf(tr->eff[TP_S12] * (0.9f / 127.0f), 0.0f, 0.9f);
    float speed = tr->eff[TP_S9] * 0.01f;
    float win = 1.0f / (0.05f * RV_FS);               /* STRETCH: a 50 ms window */
    for (k = 0; k < RV_SAMP_VOICES; k++) {
        samp_t *v = &sv[k];
        float s, e, a = 1.0f, ek;
        if (!v->on || v->track != t)
            continue;
        if (v->lane >= 0) {
            s = v->s;
            e = v->e;
        } else {
            track_span(tr, &s, &e);                   /* live: START and END can move under it */
            v->n_sh = (uint8_t)clampi((int)(tr->eff[TP_S10] + 0.5f), 2, 16);
            v->order = (uint8_t)clampi((int)(tr->eff[TP_S11] + 0.5f), 0, 7);
        }
        ek = v->dk > 0.0f ? fm_expf(-v->dk * RV_DT) : 1.0f;
        for (i = 0; i < n && v->on; i++) {
            float y = 0.0f;
            if (v->lane < 0) {
                a = artic_tick(&v->a, &v->cfg);
                if (v->a.stage == AS_OFF) {
                    v->on = 0;
                    break;
                }
            } else {
                v->env *= ek;
                a = v->env;
                if (a < 0.0005f) {
                    v->on = 0;
                    break;
                }
            }
            if (v->mode == TAPE_STRETCH) {            /* two windows read at the pitch while the place moves at SPEED */
                int j;
                v->base += speed * rate_base / tl * (float)v->dir;   /* SPEED: 100 % keeps the sample's own time */
                if (!samp_wrap(v, &v->base, s, e)) {
                    v->on = 0;
                    break;
                }
                v->gph += win;
                if (v->gph >= 1.0f)
                    v->gph -= 1.0f;
                for (j = 0; j < 2; j++) {
                    float ph = v->gph + 0.5f * (float)j, w, p;
                    if (ph >= 1.0f)
                        ph -= 1.0f;
                    w = 1.0f - fm_fabsf(2.0f * ph - 1.0f);
                    p = v->base + ph * (1.0f / win) * v->inc * (float)v->dir;
                    if (p >= e)
                        p = s + fm_fmodf(p - s, e - s);
                    if (p < s)
                        p = e - fm_fmodf(s - p, e - s);
                    y += tape_read_u(p) * w;
                }
                v->pos = v->base;
            } else if (v->mode == TAPE_SHARDS) {      /* one shard at a time, in ORDER, with a GAP after each */
                float seg = (e - s) / (float)v->n_sh, aud = seg * (1.0f - (v->lane >= 0 ? v->gap : gap)), f;
                int idx = shard_at(v->order, v->n_sh, v->sh_k);
                float a0 = s + seg * (float)idx;
                if (v->sh_u < aud) {
                    v->pos = v->dir > 0 ? a0 + v->sh_u : a0 + aud - v->sh_u;
                    f = fm_minf(1.0f, fm_minf(v->sh_u / edge, (aud - v->sh_u) / edge));
                    y = tape_read_u(v->pos) * f;
                }
                v->sh_u += v->inc;
                if (v->sh_u >= seg) {
                    v->sh_u -= seg;
                    if (++v->sh_k >= v->n_sh) {
                        v->sh_k = 0;
                        if (v->play == TPLAY_FWD || v->play == TPLAY_REV) {
                            v->on = 0;
                            break;
                        }
                    }
                }
            } else {                                   /* KEYS: start to end, as PLAY says */
                float d;
                v->pos += v->inc * (float)v->dir;
                if (!samp_wrap(v, &v->pos, s, e)) {
                    v->on = 0;
                    break;
                }
                d = fm_minf(v->pos - s, e - v->pos);
                y = tape_read_u(v->pos) * fm_minf(1.0f, d / edge);
            }
            if (v->hs.on) {                            /* a ratchet's R MOVE: darker, shorter */
                v->hs.lps += (y - v->hs.lps) * v->hs.lpc;
                y = v->hs.lps * v->hs.gain * v->hs.env;
                v->hs.env *= v->hs.dk > 0.0f ? 1.0f - v->hs.dk * RV_DT : 1.0f;
            }
            out[i] += y * a * v->vel * v->gain;
        }
        if (a > lvl)
            lvl = a;
    }
    return lvl;
}

/* ------------------------------------------------------- tape track API --- */
static const float tape_rate_tab[9] = {-2.0f, -1.0f, -0.5f, -0.25f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

static void mlr_jump_now(tapetrk_t *tt, float x, int loop_to)
{
    mlr_head_t *h = &tt->h[0];
    h->old_x = h->x;
    h->fade = 1.0f;
    h->x = x;
    if (loop_to >= 0) {
        float b = (float)loop_to / 16.0f + 1.0f / 16.0f;
        tt->loop_a = x < b ? x : b - 1.0f / 16.0f;
        tt->loop_b = x < b ? b : x + 1.0f / 16.0f;
    }
    tt->running = 1;
}

/* jumps arrive from keys, steps and influence; JUMP decides whether they wait */
static void tape_track_jump(track_t *tr, int pos, int loop_to, int now)
{
    tapetrk_t *tt = &tr->tape;
    int mode = (int)(tr->eff[TP_S0] + 0.5f);
    if (mode != TAPE_MLR)
        return;
    if (!now && (int)(tr->eff[TP_S4] + 0.5f) != 0 && rv_live.playing) {   /* (stopped, nothing to wait for) */
        tt->pend_pos = (int8_t)pos;
        tt->pend_loop = (int8_t)loop_to;
        return;
    }
    if (loop_to < 0) {
        tt->loop_a = 0.0f;
        tt->loop_b = 0.0f;
    }
    mlr_jump_now(tt, (float)clampi(pos, 0, 15) / 16.0f, loop_to);
    artic_start(&tt->a, &tt->cfg);
}

static void tape_track_note(track_t *tr, int note, float vel)
{
    extern track_t trk[RV_TRACKS];
    tapetrk_t *tt = &tr->tape;
    int mode = (int)(tr->eff[TP_S0] + 0.5f), t = (int)(tr - trk) & 7, kit = rv.tp[t][TP_ENGINE] == 1;
    if (vel <= 0.0f) {
        if (kit || mode == TAPE_MLR)
            return;                                   /* (lanes play out; MLR plays on) */
        if (mode == TAPE_SLICE)
            artic_release(&tt->a, &tt->cfg);
        else
            samp_note_off(t, note);
        return;
    }
    if (kit) {                                        /* a tape kit: a lane, from C4 up */
        samp_note_on(tr, t, note, vel, perc_lane_of(note));
        return;
    }
    if (mode == TAPE_MLR) {                         /* notes jump: white keys of an octave, 16 positions */
        tape_track_jump(tr, clampi(note - 53, 0, 26) % 16, -1, 0);
    } else if (mode == TAPE_SLICE) {
        int idx;
        if (!tt->slices_valid)
            slices_find(tr);
        idx = (note - 53) % tt->nslices;
        if (idx < 0)
            idx += tt->nslices;
        tt->sl_old_x = tt->sl_x;
        tt->sl_old_fade = tt->sl_on ? 1.0f : 0.0f;
        tt->sl_x = tt->slice[idx];
        tt->sl_end = tt->slice[idx + 1];
        tt->sl_on = 1;
        tt->sl_gain = vel;
        artic_start(&tt->a, &tt->cfg);
    } else {                                        /* KEYS, STRETCH, SHARDS: a sample voice */
        samp_note_on(tr, t, note, vel, -1);
    }
}

static void tape_row_now(int t, int row)
{
    extern track_t trk[RV_TRACKS];
    rv.tp[t & 7][TP_S1] = (int16_t)clampi(row, 0, 3);
    trk[t & 7].tape.slices_valid = 0;
}

static void tape_rate_now(int t, int how)
{
    extern track_t trk[RV_TRACKS];
    tapetrk_t *tt = &trk[t & 7].tape;
    if (how == 0)
        tt->rev = !tt->rev;
    else if (how == 1)
        tt->rate_mul = tt->rate_mul > 0.26f ? tt->rate_mul * 0.5f : 0.25f;
    else if (how == 2)
        tt->rate_mul = tt->rate_mul < 3.9f ? tt->rate_mul * 2.0f : 4.0f;
    else {
        tt->running = !tt->running;
        if (tt->running)
            artic_start(&tt->a, &tt->cfg);
        else
            artic_release(&tt->a, &tt->cfg);
    }
}

int rv_tape_heads(int t, float *pos, int max)
{
    extern track_t trk[RV_TRACKS];
    tapetrk_t *tt = &trk[t & 7].tape;
    int n = 0, mode = rv.tp[t & 7][TP_S0];
    if (rv.tp[t & 7][TP_KIND] != KIND_TAPE)
        return 0;
    if (mode == TAPE_MLR && tt->running) {
        pos[n++] = tt->h[0].x;
        if (rv.tp[t & 7][TP_S3] && n < max)
            pos[n++] = tt->h[1].x;
    } else if (mode == TAPE_SLICE && tt->sl_on && n < max) {
        pos[n++] = tt->sl_x;
    } else {                                        /* the sample voices, where they read in the row */
        float s, e;
        int k;
        row_span(rv.tp[t & 7][TP_S1], &s, &e);
        for (k = 0; k < RV_SAMP_VOICES && n < max; k++)
            if (sv[k].on && sv[k].track == (t & 7))
                pos[n++] = clampf((sv[k].pos - s) / (e - s), 0.0f, 1.0f);
    }
    return n;
}

int rv_tape_voices(int t)                         /* (tests) the sample voices sounding on track t */
{
    int k, n = 0;
    for (k = 0; k < RV_SAMP_VOICES; k++)
        n += sv[k].on && sv[k].track == (t & 7);
    return n;
}

/* a track's span (START to END) within its row, 0..1, for the screen */
void rv_tape_span(int t, float *a, float *b)
{
    extern track_t trk[RV_TRACKS];
    float s, e, rs, re;
    row_span(rv.tp[t & 7][TP_S1], &rs, &re);
    track_span(&trk[t & 7], &s, &e);
    *a = (s - rs) / (re - rs);
    *b = (e - rs) / (re - rs);
}

/* a quantized jump waits for STEP, BEAT or BAR: the sequencer calls this at each boundary */
static void tape_boundary(track_t *tr, int kind)
{
    tapetrk_t *tt = &tr->tape;
    int q = (int)(tr->eff[TP_S4] + 0.5f);
    if (tt->pend_pos < 0 || q == 0 || kind < q)
        return;
    if (tt->pend_loop < 0) {
        tt->loop_a = 0.0f;
        tt->loop_b = 0.0f;
    }
    mlr_jump_now(tt, (float)tt->pend_pos / 16.0f, tt->pend_loop);
    artic_start(&tt->a, &tt->cfg);
    tt->pend_pos = -1;
}

/* ----------------------------------------------------- tape track render --- */
static float tape_track_render(track_t *tr, int t, float *out, int n)
{
    tapetrk_t *tt = &tr->tape;
    const float *eff = tr->eff;
    int mode = clampi((int)(eff[TP_S0] + 0.5f), 0, TAPE_MODES - 1), i;
    float s, e, span, lvl = 0.0f;
    float tl = (float)tape_len(), rate_base = tape_res == BD_RES_8 ? 0.5f : 1.0f;
    float pitch = fm_exp2f((eff[TP_PITCH] + eff[TP_FINE] * 0.01f) * (1.0f / 12.0f));
    if (rv.tp[t][TP_ENGINE] == 1 || (mode != TAPE_MLR && mode != TAPE_SLICE))
        return samp_render(tr, t, out, n);           /* KEYS, STRETCH, SHARDS and kits: the sample voices */
    if (mode == TAPE_MLR)
        track_span(tr, &s, &e);                       /* MLR plays START to END of its row */
    else
        row_span((int)(eff[TP_S1] + 0.5f), &s, &e);
    span = e - s;
    if (mode == TAPE_MLR) {
        float r = tape_rate_tab[clampi((int)(eff[TP_S2] + 0.5f), 0, 8)] * tt->rate_mul * pitch * (tt->rev ? -1.0f : 1.0f);
        float inc = r * rate_base / (tl * span);       /* row fraction per sample */
        float xf = 1.0f / (0.0005f + eff[TP_S5] * eff[TP_S5] * (0.03f / 16129.0f)) * RV_DT;
        int h2 = clampi((int)(eff[TP_S3] + 0.5f), 0, 4);
        float la = tt->loop_b > tt->loop_a ? tt->loop_a : 0.0f, lb = tt->loop_b > tt->loop_a ? tt->loop_b : 1.0f;
        for (i = 0; i < n; i++) {
            float y = 0.0f, a = artic_tick(&tt->a, &tt->cfg);
            if (tt->running) {
                mlr_head_t *h = &tt->h[0];
                h->x += inc;
                if (h->x >= lb)
                    h->x = la + (h->x - lb);
                if (h->x < la)
                    h->x = lb - (la - h->x);
                y = tape_read_u(s + span * h->x);
                if (h->fade > 0.0f) {                  /* the jump crossfade */
                    h->old_x += inc;
                    y = lerpf(y, tape_read_u(s + span * (h->old_x - fm_floorf(h->old_x))), h->fade);
                    h->fade -= xf;
                }
                if (h2) {
                    float x2 = h2 == 4 ? lb - (h->x - la) : h->x + 0.25f * (float)h2;
                    x2 -= fm_floorf(x2);
                    tt->h[1].x = x2;
                    y = (y + tape_read_u(s + span * x2)) * 0.7f;
                }
            }
            y *= a;
            out[i] += y;
            lvl = a;
        }
    } else if (mode == TAPE_SLICE) {
        float r = rate_base * pitch * tt->rate_mul * (tt->rev ? -1.0f : 1.0f);
        float inc = r / (tl * span);
        for (i = 0; i < n; i++) {
            float y = 0.0f, a = artic_tick(&tt->a, &tt->cfg);
            if (tt->sl_on) {
                tt->sl_x += inc;
                if (tt->sl_x >= tt->sl_end || tt->sl_x < 0.0f) {
                    tt->sl_on = 0;
                } else {
                    float edge = fm_minf(1.0f, (tt->sl_end - tt->sl_x) * tl * span / 200.0f);
                    y = tape_read_u(s + span * tt->sl_x) * edge * tt->sl_gain;
                }
            }
            if (tt->sl_old_fade > 0.0f) {
                tt->sl_old_x += inc;
                y += tape_read_u(s + span * clampf(tt->sl_old_x, 0.0f, 1.0f)) * tt->sl_old_fade * 0.7f;
                tt->sl_old_fade -= 1.0f / 220.0f;
            }
            y *= a;
            out[i] += y;
            lvl = a;
        }
    }
    (void)t;
    return lvl;
}

static void tape_init(void)
{
    int t;
    extern track_t trk[RV_TRACKS];
    tape_setup(clampi(rv.gp[GP_TAPEQUAL], 0, 2));
    for (t = 0; t < RV_TRACKS; t++) {
        trk[t].tape.pend_pos = -1;
        trk[t].tape.rate_mul = 1.0f;
    }
}
