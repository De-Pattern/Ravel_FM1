/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the GLITCH LOOPER (KIND_LOOP). Four lanes of audio recorded from the main mix (or one track)
 * and played back, each with its own LEVEL, PITCH and SPEED (pitch and speed apart: a two-grain
 * stretch), overdubbed as they record. The FM-1 has no audio input and no RAM to spare, so the lanes
 * live in the memory of the DELAY, FEEDBACK, SPACE and microcut buffers (which stand still while a
 * project has a LOOP track), as 8-bit companded samples: 63 KB, about 16 000 samples a lane, spread
 * over the loop's LENGTH (so a short loop at a quick tempo sounds clearer than a long slow one: 16
 * steps at 120 BPM is about 8 kHz, lo-fi on purpose).
 *
 * Time: each lane is the loop's LENGTH in steps, laid over its buffer; the loop's clock follows the
 * track's sequencer while it plays and runs on at the tempo while stopped. The sequencer places
 * record and play trigs per lane, with a RANGE of steps (MLR-style: record into 5-11, play 8-14); the
 * keys record and play lanes by hand and play slices of what's playing, in the order they are held.
 * Part of the engine unit (ravel_app.c), after rv_bus.c (it borrows its buffers). */
#include "rv_int.h"

#define LP_LANES 4
static int8_t loop_t = -1;                            /* which track it is (-1: none) */

/* the lanes' memory: the four buffers as one, byte by byte */
static struct { int8_t *p; uint32_t n; } lm_seg[4];
static uint32_t lm_total, lm_lane;                    /* all of it, and a lane's (samples) */
static float lm_dec[256] RV_RAMBSS;                             /* the companding, decoded */
static void lm_init(int mask)                         /* the buffers borrowed (LM_*), laid end to end */
{
    int8_t *const p[4] = {(int8_t *)dly_buf, (int8_t *)cut_buf, (int8_t *)fb_buf, (int8_t *)space_mem};
    const uint32_t n[4] = {sizeof dly_buf, sizeof cut_buf, sizeof fb_buf, sizeof space_mem};
    const uint8_t bit[4] = {LM_DLY, LM_CUT, LM_FB, LM_SPACE};
    int i, k = 0;
    for (i = 0; i < 4; i++)
        if (mask & bit[i])
            lm_seg[k].p = p[i], lm_seg[k++].n = n[i];
    for (i = k; i < 4; i++)
        lm_seg[i].p = lm_seg[0].p, lm_seg[i].n = 0;  /* (unused: lm_at never reaches them) */
    lm_total = lm_seg[0].n + lm_seg[1].n + lm_seg[2].n + lm_seg[3].n;
    lm_lane = lm_total / LP_LANES;
    if (lm_dec[255] != 0.0f)
        return;
    for (i = 0; i < 256; i++) {                       /* mu-law-like (mu 255) on a signed byte: 0 is silence */
        int c = (int)(int8_t)(uint8_t)i, m = c < 0 ? -c : c;
        float y = (fm_exp2f((float)m * (8.0f / 127.0f)) - 1.0f) * (1.0f / 255.0f);
        lm_dec[i] = c < 0 ? -y : y;
    }
}
RV_INL int8_t *lm_at(uint32_t i)                      /* (i < lm_total) */
{
    if (i < lm_seg[0].n)
        return lm_seg[0].p + i;
    i -= lm_seg[0].n;
    if (i < lm_seg[1].n)
        return lm_seg[1].p + i;
    i -= lm_seg[1].n;
    if (i < lm_seg[2].n)
        return lm_seg[2].p + i;
    return lm_seg[3].p + (i - lm_seg[2].n);
}
RV_INL int lm_enc(float x)
{
    float a = fm_fabsf(x);
    int m;
    if (a > 1.0f)
        a = 1.0f;
    m = (int)(fm_log2f(1.0f + a * 255.0f) * (127.0f / 8.0f) + 0.5f);
    if (m > 127)
        m = 127;
    return x < 0.0f ? -m : m;
}
static void lm_clear(int lane)                        /* a lane, or all (-1) */
{
    uint32_t i, a = lane < 0 ? 0 : (uint32_t)lane * lm_lane, b = lane < 0 ? lm_total : a + lm_lane;
    for (i = a; i < b; i++)
        *lm_at(i) = 0;
}
/* a lane's sample at a position in steps (0 .. len), linearly between two */
RV_INL float lm_read(int lane, float pos, float spb, uint32_t n)
{
    float f = pos * spb, fr;
    int32_t i0;
    uint32_t a, b;
    if (!(f >= 0.0f))
        f = 0.0f;
    i0 = (int32_t)f;
    fr = f - (float)i0;
    a = (uint32_t)i0 % n;
    b = (a + 1u) % n;
    a += (uint32_t)lane * lm_lane;
    b += (uint32_t)lane * lm_lane;
    return lm_dec[(uint8_t)*lm_at(a)] * (1.0f - fr) + lm_dec[(uint8_t)*lm_at(b)] * fr;
}

typedef struct {
    uint8_t play, rec, wait, man;                     /* playing; recording; waiting for the threshold; recording by hand */
    float pa, pl, q;                                  /* the play range (start, length, in steps) and where in it */
    float ra;                                         /* the record range's start (steps) */
    int16_t rleft;                                    /* steps the range has left to record (-1: by hand, on and on) */
    int16_t rlast;                                    /* (the step it last wrote in) */
    int32_t widx;                                     /* the sample being written (-1: none yet) */
    float wsum;
    int16_t wcnt;
    float g[2], gph;                                  /* the two grains' read heads (steps) and the phase between them */
    float peak;
} lane_t;
static lane_t lane[LP_LANES];
static float loop_pos;                                /* the loop's clock, in steps (0 .. len) */

/* slices: keys held, in the order they were pressed; one plays at a time, each for a quarter of its
 * lane's play range, round and round (as the arp's PLAYED does with notes) */
static uint8_t sl_list[16], sl_n, sl_cur;
static float sl_left, sl_q;                           /* the slice playing: samples left, where in it (steps) */

static int loop_len(int t) { return clampi(rv.tp[t][TP_LEN], 1, RV_STEPS); }
static float wrapf(float x, float m) { while (x >= m) x -= m; while (x < 0.0f) x += m; return x; }
static float loop_lvl(int t, int l) { float v = rv.tp[t][TP_S0 + l] * (1.0f / 127.0f); return v * v; }
static float loop_speed(int t, int l)
{
    static const float sp[7] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, -1.0f};
    return sp[clampi(trk[t].eff[TP_S8 + l] + 0.5f, 0, 6)];
}

/* the step fires a lane's trigs: play from it, record into it, for RANGE steps (0: the whole loop) */
static void loop_play_from(int l, float a, float n)
{
    lane_t *ln = &lane[l];
    ln->play = 1;
    ln->pa = a;
    ln->pl = n;
    ln->q = 0.0f;
    ln->g[0] = ln->g[1] = a;
}
static void loop_rec_into(int t, int l, float a, int n)
{
    lane_t *ln = &lane[l];
    ln->rec = 1;
    ln->man = 0;
    ln->ra = a;
    ln->rleft = (int16_t)n;
    ln->rlast = (int16_t)a;
    ln->widx = -1;
    ln->wait = rv.tp[t][TP_S13] > 0;                  /* THRESH: waits for the sound */
}
static int loop_range(const rv_step *s, int l)        /* a lane's RANGE on a step (0: all) */
{
    return l == 0 ? (uint8_t)s->chord[0] : l == 1 ? (uint8_t)s->chord[1] : l == 2 ? (uint8_t)s->chord[2] : s->vel;
}
static void loop_fire(int t, int si)
{
    const rv_step *s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[si];
    int l, len = loop_len(t);
    if (!(s->flags & ST_ON) || !loop_mem)             /* (LOOPER OFF: the trigs wait) */
        return;
    for (l = 0; l < LP_LANES; l++) {
        int r = loop_range(s, l), n = r > 0 ? clampi(r, 1, len) : len;
        if ((s->note >> (4 + l)) & 1)
            loop_rec_into(t, l, (float)si, n);
        if ((s->note >> l) & 1)
            loop_play_from(l, (float)si, (float)n);
    }
}

/* by hand (the keys, rv_ui.c): op 0 record on/off, 1 play on/off, 2 clear (lane 4: all), 3 slice on, 4 off */
static void loop_cmd(int op, int l, int v)
{
    int t = loop_t, i;
    if (t < 0)
        return;
    if (op == 0 && l < LP_LANES) {
        lane_t *ln = &lane[l];
        if (ln->rec) {
            ln->rec = ln->wait = 0;
        } else {
            ln->rec = 1;
            ln->man = 1;
            ln->rleft = -1;
            ln->widx = -1;
            ln->wait = rv.tp[t][TP_S13] > 0;
            if (!ln->play) {                          /* (and it plays what it records, the loop's whole length) */
                loop_play_from(l, 0.0f, (float)loop_len(t));
                ln->q = loop_pos;
            }
        }
    } else if (op == 1 && l < LP_LANES) {
        if (lane[l].play)
            lane[l].play = 0;
        else {
            loop_play_from(l, 0.0f, (float)loop_len(t));
            lane[l].q = loop_pos;                     /* in time with the loop */
        }
    } else if (op == 2) {
        for (i = 0; i < LP_LANES; i++)
            if (l == i || l >= LP_LANES) {
                lm_clear(i);
                lane[i].play = lane[i].rec = lane[i].wait = 0;
            }
    } else if (op == 3 && sl_n < 16) {
        for (i = 0; i < sl_n && sl_list[i] != v; i++)
            ;
        if (i == sl_n) {
            sl_list[sl_n++] = (uint8_t)v;
            if (sl_n == 1) {
                sl_cur = 0;
                sl_left = 0.0f;                       /* (starts at once) */
            }
        }
    } else if (op == 4) {
        for (i = 0; i < sl_n && sl_list[i] != v; i++)
            ;
        if (i < sl_n) {
            for (; i + 1 < sl_n; i++)
                sl_list[i] = sl_list[i + 1];
            sl_n--;
            if (sl_cur >= sl_n)
                sl_cur = 0;
        }
    }
}

/* the memory changes hands, as the LOOPER knob says (OFF, ON, ON keeping SPACE, ON keeping DELAY):
 * the buffers the LOOP track takes are cleared for its lanes, and those it gives back cleared for their
 * buses. Any change empties the lanes: they are laid out again over what it has now */
static void loop_mem_check(void)
{
    static const uint8_t masks[4] = {0, LM_DLY | LM_CUT | LM_FB | LM_SPACE, LM_DLY | LM_CUT | LM_FB, LM_CUT | LM_FB | LM_SPACE};
    int t, f = -1, m;
    for (t = 0; t < RV_TRACKS; t++)
        if (rv.tp[t][TP_KIND] == KIND_LOOP) {
            f = t;
            break;
        }
    m = f >= 0 ? masks[clampi(rv.tp[f][TP_S15], 0, 3)] : 0;
    if (m != loop_mem) {
        loop_mem = (uint8_t)m;                        /* (the buses skip the work of those borrowed from now on) */
        buses_reset();                                /* (those given back: clean, before they run) */
        __builtin_memset(lane, 0, sizeof lane);
        sl_n = 0;
        loop_pos = 0.0f;
        if (m) {
            lm_init(m);
            lm_clear(-1);
        } else
            lm_total = lm_lane = 0;
    }
    loop_t = (int8_t)(m ? f : -1);
}

/* one lane's playing, into out: the loop's position runs at its SPEED; PITCH moves the grains' reading */
static void lane_play(int t, int l, float *out, int n, float dstep, float spb, int sliced, float sa, float sl)
{
    lane_t *ln = &lane[l];
    float sp = loop_speed(t, l), g = loop_lvl(t, l), pk = 0.0f;
    float pr = fm_exp2f(trk[t].eff[TP_S4 + l] * (1.0f / 12.0f)), gstep = 1.0f / (0.035f * RV_FS);
    float a = sliced ? sa : ln->pa, len = sliced ? sl : ln->pl, *qp = sliced ? &sl_q : &ln->q;
    int grains = fm_fabsf(pr - 1.0f) > 0.001f || fm_fabsf(sp) != 1.0f, i, L = loop_len(t);
    float q = *qp, rd = (sp < 0.0f ? -1.0f : 1.0f) * pr * dstep;
    if (len < 0.01f)
        len = (float)L;
    for (i = 0; i < n; i++) {
        float x, pos;
        q += sp * dstep;
        while (q >= len)
            q -= len;
        while (q < 0.0f)
            q += len;
        pos = a + q;
        if (pos >= (float)L)
            pos -= (float)L;
        if (!grains)
            x = lm_read(l, pos, spb, lm_lane);
        else {                                        /* two grains, half a grain apart, read at PITCH, restarted at the position */
            float w0, w1;
            ln->gph += gstep;
            if (ln->gph >= 1.0f) {
                ln->gph -= 1.0f;
                ln->g[0] = pos;
            }
            if (ln->gph >= 0.5f && ln->gph - gstep < 0.5f)
                ln->g[1] = pos;
            ln->g[0] += rd;
            ln->g[1] += rd;
            if (ln->g[0] < 0.0f) ln->g[0] += (float)L;
            if (ln->g[1] < 0.0f) ln->g[1] += (float)L;
            if (ln->g[0] >= (float)L) ln->g[0] -= (float)L;
            if (ln->g[1] >= (float)L) ln->g[1] -= (float)L;
            w0 = sin_lut(ln->gph * 0.5f);             /* (sin² and cos² of the phase: they sum to one) */
            w0 *= w0;
            w1 = 1.0f - w0;
            x = lm_read(l, ln->g[0], spb, lm_lane) * w0 + lm_read(l, ln->g[1], spb, lm_lane) * w1;
        }
        x *= g;
        out[i] += x;
        if (fm_fabsf(x) > pk)
            pk = fm_fabsf(x);
    }
    *qp = q;
    ln->peak = fm_maxf(pk, ln->peak * 0.9f);
}

/* the LOOP track's sound this step: its lanes (or the slice held), into buf */
static void loop_render(int t, float *buf, int n)
{
    int l, L = loop_len(t), held[LP_LANES] = {0, 0, 0, 0};
    float sd = step_seconds(t), dstep = 1.0f / (sd * RV_FS), spb = (float)lm_lane / (float)L;
    if (!loop_mem) {                                  /* LOOPER OFF: silent, the effects have the memory */
        for (l = 0; l < LP_LANES; l++)
            rv_live.loop_st[l] = 0, rv_live.loop_peak[l] = 0.0f;
        rv_live.loop_in = 0.0f;
        return;
    }
    if (playing && sq[t].step[0] >= 0) {              /* the loop's clock: the sequencer's, while it plays */
        float fr = (float)(now_s - sq[t].step_time) * RV_DT / (sq[t].step_dur > 0.0f ? sq[t].step_dur : sd);
        loop_pos = (float)sq[t].step[0] + clampf(fr, 0.0f, 0.999f);
    }
    for (l = 0; l < sl_n; l++)
        held[(sl_list[l] >> 2) & 3] = 1;
    if (sl_n) {                                       /* a slice: a quarter of its lane's play range */
        int e = sl_list[sl_cur % sl_n], sll = (e >> 2) & 3, s4 = e & 3;
        lane_t *ln = &lane[sll];
        float pa = ln->play ? ln->pa : 0.0f, pl = ln->play && ln->pl > 0.01f ? ln->pl : (float)L;
        float sp = fm_fabsf(loop_speed(t, sll));
        if (sl_left <= 0.0f) {                        /* the next one */
            if (sl_left < 0.0f || sl_n > 1)
                sl_cur = (uint8_t)((sl_cur + 1) % sl_n);
            e = sl_list[sl_cur % sl_n];
            sll = (e >> 2) & 3;
            ln = &lane[sll];
            pa = ln->play ? ln->pa : 0.0f;
            pl = ln->play && ln->pl > 0.01f ? ln->pl : (float)L;
            sp = fm_fabsf(loop_speed(t, sll));
            sl_q = 0.0f;
            sl_left = (pl * 0.25f) / (sp * dstep);
            s4 = e & 3;
        }
        lane_play(t, sll, buf, n, dstep, spb, 1, pa + pl * 0.25f * (float)s4, pl * 0.25f);
        sl_left -= (float)n;
        if (sl_left <= 0.0f)
            sl_left = -1.0f;                          /* (moves on next step) */
    }
    for (l = 0; l < LP_LANES; l++) {
        lane_t *ln = &lane[l];
        if (ln->play && !held[l])
            lane_play(t, l, buf, n, dstep, spb, 0, 0.0f, 0.0f);
        else
            ln->peak *= 0.9f;
        rv_live.loop_st[l] = (uint8_t)(ln->play | ln->rec << 1 | ln->wait << 2 | held[l] << 3);
        rv_live.loop_q[l] = wrapf(ln->pa + ln->q, (float)L) / (float)L;
        rv_live.loop_pa[l] = ln->pa / (float)L;
        rv_live.loop_pl[l] = ln->pl / (float)L;
        rv_live.loop_ra[l] = ln->ra / (float)L;
        rv_live.loop_peak[l] = ln->peak;
    }
    rv_live.loop_pos = loop_pos / (float)L;
}

/* what comes in this step (the main mix without the LOOP track, or one track) into the lanes
 * recording, at the loop's clock: each lane sample the average of the sound that falls on it, laid
 * over what was there kept by OVERDUB (0: replaced) */
static void loop_record(int t, const float *in, int n)
{
    int l, i, L = loop_len(t), any = 0;
    float sd = step_seconds(t), dstep = 1.0f / (sd * RV_FS), spb = (float)lm_lane / (float)L;
    float keep = rv.tp[t][TP_S14] * (1.0f / 127.0f), thr = rv.tp[t][TP_S13] * (1.0f / 127.0f), pk = 0.0f;
    float p0 = loop_pos;
    thr = thr * thr * 0.5f;
    for (i = 0; i < n; i++)
        pk = fm_maxf(pk, fm_fabsf(in[i]));
    rv_live.loop_in = fm_maxf(pk, rv_live.loop_in * 0.9f);
    for (l = 0; l < LP_LANES; l++) {
        lane_t *ln = &lane[l];
        float pos = p0;
        if (!ln->rec)
            continue;
        if (ln->wait) {
            if (pk < thr)
                continue;
            ln->wait = 0;                             /* the sound came: from here */
        }
        any = 1;
        for (i = 0; i < n; i++) {
            int32_t w;
            int step;
            pos += dstep;
            if (pos >= (float)L)
                pos -= (float)L;
            step = (int)pos;
            if (ln->rleft >= 0 && step != ln->rlast) {   /* a range: its steps counted down as they pass */
                ln->rlast = (int16_t)step;
                if (--ln->rleft <= 0) {
                    ln->rec = 0;
                    break;
                }
            }
            w = (int32_t)(pos * spb) % (int32_t)lm_lane;
            if (w != ln->widx) {
                if (ln->widx >= 0 && ln->wcnt) {     /* the last sample, written */
                    int8_t *p = lm_at((uint32_t)l * lm_lane + (uint32_t)ln->widx);
                    float v = ln->wsum / (float)ln->wcnt + lm_dec[(uint8_t)*p] * keep;
                    *p = (int8_t)lm_enc(v);
                }
                ln->widx = w;
                ln->wsum = 0.0f;
                ln->wcnt = 0;
            }
            ln->wsum += in[i];
            ln->wcnt++;
        }
    }
    (void)any;
    if (!playing || sq[t].step[0] < 0) {              /* stopped: the loop's clock runs on by itself */
        loop_pos = p0 + dstep * (float)n;
        while (loop_pos >= (float)L)
            loop_pos -= (float)L;
    }
}

/* for the picture (rv_ui.c) */
uint32_t rv_loop_lane_len(void) { return loop_mem ? lm_lane : 0; }
int rv_loop_peek(int l, uint32_t i) { return loop_mem && l >= 0 && l < LP_LANES && i < lm_lane ? *lm_at((uint32_t)l * lm_lane + i) : 0; }
