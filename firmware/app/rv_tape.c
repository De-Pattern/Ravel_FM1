/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the capture tape and the tape tracks.
 *
 * One tape, in Beads' own formats (clean 16-bit, tape 12-bit, dirty 8-bit mu-law), always
 * listening to its source until it is caught. Positions are in "tape time": u = 0 is the
 * oldest sample, u = 1 the newest; rows are spans of u. Tape tracks play it three ways:
 * MLR playheads (keys jump, two keys loop), the Beads grain engine (reading the same tape),
 * or slices cut at its onsets and played like notes.
 * Part of the engine unit (ravel_app.c). */
#include "rv_int.h"

static uint8_t tape_buf[RV_TAPE_BYTES] RV_BIG;
static bd_tape tape;
static bd_engine beads[RV_BEADS];
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
    int b;
    bd_tape_setup(&tape, tape_buf, sizeof(tape_buf), (bd_res)res, 1, 0);
    tape_res = res;
    tape_frozen = 0;
    for (b = 0; b < RV_BEADS; b++) {
        bd_init(&beads[b], 0, 0, RV_FS);
        bd_share_tape(&beads[b], &tape);
        bd_seed_rng(&beads[b], 0x21u + (uint32_t)b * 7919u);
    }
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
    /* not enough onsets: fill evenly */
    if (found < want / 2) {
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
    tapetrk_t *tt = &tr->tape;
    int mode = (int)(tr->eff[TP_S0] + 0.5f);
    if (vel <= 0.0f) {
        if (mode != TAPE_MLR)
            artic_release(&tt->a, &tt->cfg);
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
    } else {                                        /* beads: a gate for clocked grains, pitch from the note */
        tt->gate_pending = 1;
        tt->note_ofs = (float)(note - 60);
        artic_start(&tt->a, &tt->cfg);
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
    }
    return n;
}

int rv_tape_grains(int t, float *pos, float *lvl, int max)
{
    extern track_t trk[RV_TRACKS];
    tapetrk_t *tt = &trk[t & 7].tape;
    int32_t d[32], l[32];
    int n, i, len = tape_len();
    float s, e;
    if (tt->beads < 0 || rv.tp[t & 7][TP_S0] != TAPE_BEADS)
        return 0;
    n = bd_active_grains(&beads[tt->beads], d, l, max < 32 ? max : 32);
    row_span(rv.tp[t & 7][TP_S1], &s, &e);
    for (i = 0; i < n; i++) {
        float u = 1.0f - (float)d[i] / (float)len;
        pos[i] = (u - s) / (e - s);
        lvl[i] = (float)l[i] / 32768.0f;
    }
    return n;
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
static float tape_track_render(track_t *tr, int t, float *out, int n, int beads_idx)
{
    tapetrk_t *tt = &tr->tape;
    const float *eff = tr->eff;
    int mode = clampi((int)(eff[TP_S0] + 0.5f), 0, 2), i;
    float s, e, span, lvl = 0.0f;
    float tl = (float)tape_len(), rate_base = tape_res == BD_RES_8 ? 0.5f : 1.0f;
    float pitch = fm_exp2f((eff[TP_PITCH] + eff[TP_FINE] * 0.01f) * (1.0f / 12.0f));
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
    } else if (beads_idx >= 0) {
        /* Beads reads the shared tape; its feedback is added to the tape's input by the mixer */
        bd_params p;
        bd_frame in[RV_SUB], o[RV_SUB];
        uint8_t gates[RV_SUB], frz[RV_SUB];
        int32_t k64 = 65536 / 127;
        float time_in_row = s + span * eff[TP_S6] * (1.0f / 127.0f);
        for (i = 0; i < n; i++) {
            in[i].l = in[i].r = 0;
            frz[i] = tape_frozen;
            gates[i] = 0;
        }
        if (tt->gate_pending) {
            gates[0] = BD_GATE_RISING | BD_GATE_HIGH;
            tt->gate_pending = 0;
        }
        p.time.base = (int32_t)(clampf(1.0f - time_in_row, 0.0f, 1.0f) * 65535.0f);
        p.time.amount = (int32_t)(eff[TP_S13] * k64 / 4);
        p.time.cv = 0;
        p.time.has_cv = 0;
        p.size.base = (int32_t)(eff[TP_S7] * k64);
        p.size.amount = p.time.amount;
        p.size.cv = 0;
        p.size.has_cv = 0;
        p.shape.base = (int32_t)(eff[TP_S8] * k64);
        p.shape.amount = 0;
        p.shape.cv = 0;
        p.shape.has_cv = 0;
        p.pitch.base = (int32_t)((eff[TP_S9] + tt->note_ofs + eff[TP_PITCH]) * 65536.0f);
        p.pitch.amount = p.time.amount / 2;
        p.pitch.cv = 0;
        p.pitch.has_cv = 0;
        p.density = (int32_t)(eff[TP_S10] * k64);
        p.density_cv = 0;
        p.feedback = 0;                                /* the tape's owner adds feedback */
        p.dry_wet = 65535;
        p.monophonic = 0;
        bd_process(&beads[beads_idx], &p, BD_SEED_ON, gates, frz, in, o, n);
        for (i = 0; i < n; i++) {
            float a = artic_tick(&tt->a, &tt->cfg), y = (float)(o[i].l + o[i].r) * (0.5f / 32768.0f);
            /* without a held note the cloud plays on its own while the sequencer runs, and fades out
             * (about 0.2 s) when it stops: the envelope shapes only played notes. (Stopped, a free cloud
             * listening to a tape that listens to it would go on forever, like a note that hangs.) */
            tt->free_g += ((rv_live.playing ? 1.0f : 0.0f) - tt->free_g) * 0.0001f;
            if (tt->a.stage == AS_OFF)
                a = tt->free_g;
            y *= a * eff[TP_S12] * (1.0f / 127.0f);
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
        trk[t].tape.beads = -1;
        trk[t].tape.pend_pos = -1;
        trk[t].tape.rate_mul = 1.0f;
    }
}
