/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: projects in the platform's slots. A project is the whole rv_project, about 40 KB.
 * plat_load copies up to max bytes and returns the slot's stored length, so the header can be
 * checked before anything is copied over the playing project. */
#include "rv.h"

typedef struct { uint32_t magic, format; char name[12]; } rv_head;

int rv_save(int slot)
{
    rv.magic = RV_MAGIC;
    rv.format = RV_FORMAT;
    return plat_save(slot, &rv, sizeof(rv));
}

/* older formats, by what they lack: 96 track parameters (formats 6-14) or 80 (2-5), 96 globals
 * (2-4), no CZ patches (2 and 3). Everything else is laid out the same, in the same order. */
static int old_tp(int f) { return f >= 15 ? RV_TP : f >= 6 ? 96 : 80; }
static int old_gp(int f) { return f >= 5 ? 128 : 96; }
static int old_size(int f)
{
    int n = (int)sizeof(rv) - (RV_TP - old_tp(f)) * RV_TRACKS * 2 - (RV_GP - old_gp(f)) * 2;
    if (f < 16)
        n -= (int)sizeof(rv.kit_rate);                /* no lane rates before format 16 */
    if (f < 17)
        n -= (int)(sizeof(rv_step) - RV_STEP_OLD) * RV_TRACKS * RV_PATTERNS * RV_STEPS;   /* steps without chords */
    if (f >= 10)
        return n;
    n -= (int)sizeof(rv.scene_tempo);                 /* no scene tempos before format 10 */
    if (f >= 8)                                       /* formats 8 and 9: the same layout */
        return n;
    n -= (int)sizeof(rv.kitlk);                       /* no kit locks before format 8 */
    if (f >= 7)
        return n;
    n -= (int)sizeof(rv.kit);                         /* no kits before format 7 */
    return f >= 4 ? n : n - (int)sizeof(rv.cz);
}

static int head_ok(int slot, rv_head *h)
{
    int n = plat_load(slot, h, sizeof(*h));
    if (h->magic != RV_MAGIC)
        return 0;
    if (h->format == RV_FORMAT)
        return n == (int)sizeof(rv);
    return h->format >= 2 && h->format < RV_FORMAT && n == old_size((int)h->format);
}

static uint16_t remap_pid(uint16_t pid, int tpo, int gpo)
{
    int gpid_old = RV_TRACKS * tpo;
    if (pid == 0xFFFF)
        return pid;
    if (pid < gpid_old)
        return (uint16_t)PID_T(pid / tpo, pid % tpo);
    if (pid < gpid_old + gpo)
        return (uint16_t)PID_G(pid - gpid_old);
    if (pid >= gpid_old + RV_GP && pid < gpid_old + RV_GP + RV_MATRIX)   /* a matrix row's AMOUNT */
        return (uint16_t)PID_MAT(pid - gpid_old - RV_GP);
    return 0xFFFF;
}

/* an older project, loaded as it was stored, is spread out into today's layout: what follows
 * the globals moves up, then the globals, then each track's row, from the end so nothing is
 * overwritten before it moves. The new places start at their defaults, and the parameter ids
 * the matrix and macros hold are renumbered. */
static void upgrade_layout(int f, int loaded)
{
    uint8_t *base = (uint8_t *)&rv;
    int tp_off = (int)((uint8_t *)&rv.tp[0][0] - base), gpo = old_gp(f), tpo = old_tp(f);
    int gp_old = tp_off + RV_TRACKS * tpo * 2, rest_old = gp_old + gpo * 2;
    int gp_new = (int)((uint8_t *)&rv.gp[0] - base), rest_new = gp_new + RV_GP * 2;
    int t, p, g, i, k;
    __builtin_memmove(base + rest_new, base + rest_old, (unsigned)(loaded - rest_old));
    __builtin_memmove(base + gp_new, base + gp_old, (unsigned)(gpo * 2));
    for (t = RV_TRACKS - 1; t > 0; t--)
        __builtin_memmove(base + tp_off + t * RV_TP * 2, base + tp_off + t * tpo * 2, (unsigned)(tpo * 2));
    for (t = 0; t < RV_TRACKS; t++) {
        for (p = tpo; p < RV_TP; p++)
            rv.tp[t][p] = (int16_t)(p < TP_N ? rv_pinfo_track(t, p)->def : 0);
        if (f < 5)
            rv.tp[t][TP_BANDS] = 0;
    }
    for (g = gpo; g < RV_GP; g++)
        rv.gp[g] = (int16_t)(g < GP_N ? rv_pinfo_global(g)->def : 0);
    for (i = 0; i < RV_MATRIX; i++)
        rv.mat[i].dst = remap_pid(rv.mat[i].dst, tpo, gpo);
    for (i = 0; i < RV_MACROS; i++)
        for (k = 0; k < RV_MACRO_DESTS; k++)
            rv.macro[i].d[k].dst = remap_pid(rv.macro[i].d[k].dst, tpo, gpo);
}

/* steps from before format 17 (no chords): what follows the patterns moves up, then each step
 * spreads out from the last, its chord empty. have: the bytes loaded, in today's layout up to the patterns */
static void upgrade_steps(int have)
{
    uint8_t *base = (uint8_t *)&rv;
    int pat_off = (int)((uint8_t *)&rv.pat[0][0].s[0] - base), nst = RV_TRACKS * RV_PATTERNS * RV_STEPS, i;
    int tail_old = pat_off + nst * RV_STEP_OLD, tail_new = pat_off + nst * (int)sizeof(rv_step);
    if (have > tail_old)
        __builtin_memmove(base + tail_new, base + tail_old, (unsigned)(have - tail_old));
    for (i = nst - 1; i >= 0; i--) {
        uint8_t *dst = base + pat_off + i * (int)sizeof(rv_step);
        __builtin_memmove(dst, base + pat_off + i * RV_STEP_OLD, RV_STEP_OLD);
        __builtin_memset(dst + RV_STEP_OLD, 0, sizeof(rv_step) - RV_STEP_OLD);
    }
}

/* formats 2 and 3 had the PD engine where CZ is now: its tracks start from the CZ defaults */
static void upgrade_cz(void)
{
    int t;
    for (t = 0; t < RV_TRACKS; t++) {
        rv_cz_init(t);
        if (rv.tp[t][TP_KIND] == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ)
            rv_track_src_defaults(t);
    }
}

/* a format 2 project: the new parameters at their defaults, old graph settings carried over */
static void upgrade_from_2(void)
{
    int t, p, g;
    for (g = GP_DLYSPREAD; g < GP_N; g++)
        rv.gp[g] = (int16_t)rv_pinfo_global(g)->def;
    for (t = 0; t < RV_TRACKS; t++) {
        int heads = rv.tp[t][TP_H2ROLE];             /* was HEADS, 1 or 2 */
        for (p = TP_SEQMODE; p < TP_N; p++)
            rv.tp[t][p] = (int16_t)rv_pinfo_track(t, p)->def;
        rv.tp[t][TP_H2ROLE] = (int16_t)(heads == 2 ? ROLE_NOTES : ROLE_OFF);
        if (rv.tp[t][TP_DIR] == DIR_GRAPH) {
            rv.tp[t][TP_SEQMODE] = SEQ_GRAPH;
            rv.tp[t][TP_DIR] = DIR_FWD;
        }
    }
    rv.format = RV_FORMAT;
}

int rv_load(int slot)
{
    rv_head h;
    int f, n;
    if (!head_ok(slot, &h))
        return -1;
    if (h.format == RV_FORMAT) {
        if (plat_load(slot, &rv, sizeof(rv)) != (int)sizeof(rv))
            return -1;
        return 0;
    }
    f = (int)h.format;
    n = old_size(f);
    if (plat_load(slot, &rv, (uint32_t)n) != n)
        return -1;
    if (f <= 14)                                       /* (fewer track parameters, or globals): spread out */
        upgrade_layout(f, n);
    if (f <= 16)                                       /* (steps without chords): spread out */
        upgrade_steps(n + (RV_TP - old_tp(f)) * RV_TRACKS * 2 + (RV_GP - old_gp(f)) * 2);
    if (f <= 15)                                       /* (format 16) every kit lane at the track's rate */
        __builtin_memset(rv.kit_rate, 0, sizeof(rv.kit_rate));
    if (f <= 8) {
        rv.gp[GP_SWING] = (int16_t)clampi(50 + (rv.gp[GP_SWING] + 2) / 4, 50, 100);   /* was a delay 0-75 (0.5% of a step each): now % */
        rv.gp[GP_METRO] = 0;
        rv.gp[GP_COUNTIN] = 1;
    }
    if (f <= 9)
        __builtin_memset(rv.scene_tempo, 0, sizeof(rv.scene_tempo));   /* every scene keeps the tempo */
    {
        int t;
        for (t = 0; t < RV_TRACKS; t++) {
            if (f <= 10)                               /* (format 11) the first oscillator at full, as it was */
                rv.tp[t][TP_OSC1] = 127;
            if (f <= 11)                               /* (format 12) SHAPE's slots: the FILTER ENV, as the one envelope was */
                rv_fenv_from_env(rv.tp[t]);
            if (f <= 13 && rv.tp[t][TP_DRIVE] == 10)  /* (format 14) the old default DRIVE, which broke chords up: clean */
                rv.tp[t][TP_DRIVE] = 0;
            if (f <= 12) {                             /* (format 13) STEP ENV: off; the steps' old triplet numbers go */
                int p, s;
                rv.tp[t][TP_SEDST] = TP_CUT;
                rv.tp[t][TP_SEAMT] = 0;
                rv.tp[t][TP_SEDEF] = 0;
                if (!(rv.tp[t][TP_KIND] == KIND_PERC && rv.tp[t][TP_ENGINE] == 1))   /* (a kit's env bits are its accents) */
                    for (p = 0; p < RV_PATTERNS; p++)
                        for (s = 0; s < RV_STEPS; s++)
                            rv.pat[t][p].s[s].env = 0;
            }
        }
        if (f <= 12)                                   /* the triplets' room: the LIBRARY's factory envelopes */
            rv_senv_factory();
    }
    if (f == 2)
        upgrade_from_2();
    if (f <= 3)
        upgrade_cz();
    {
        int t;
        for (t = 0; t < RV_TRACKS; t++) {
            if (f <= 6)
                rv_kit_init(t);
            if (f <= 7)
                __builtin_memset(rv.kitlk[t], 0xFF, sizeof(rv.kitlk[t]));
            if (f <= 18 && t == 0 && rv.gp[GP_REVMODE] >= 2)   /* (format 19) BLACKHOLE left the space: CLOUDS, the nearest */
                rv.gp[GP_REVMODE] = (int16_t)(rv.gp[GP_REVMODE] == 2 ? 1 : 2);
            if (f <= 20) {                               /* (format 21) the SQ engine and the SQ-80 filter gone */
                int was_sq = rv.tp[t][TP_KIND] == KIND_SYNTH && rv.tp[t][TP_ENGINE] == 7, pt, st, k;
                if (rv.tp[t][TP_KIND] == KIND_SYNTH)
                    rv.tp[t][TP_ENGINE] = (int16_t)rv_up21_engine(rv.tp[t][TP_ENGINE]);
                rv.tp[t][TP_FTYPE] = (int16_t)rv_up21_ftype(rv.tp[t][TP_FTYPE]);
                if (was_sq)
                    rv_track_src_defaults(t);            /* (the SQ's knobs mean nothing to VA) */
                for (pt = 0; pt < RV_PATTERNS; pt++)     /* locks of ENGINE or FILTER on steps */
                    for (st = 0; st < RV_STEPS; st++)
                        for (k = 0; k < 2; k++) {
                            rv_step *s = &rv.pat[t][pt].s[st];
                            if (s->lp[k] == TP_ENGINE + 1 && rv.tp[t][TP_KIND] == KIND_SYNTH)
                                s->lv[k] = (uint8_t)rv_up21_engine(s->lv[k]);
                            else if (s->lp[k] == TP_FTYPE + 1)
                                s->lv[k] = (uint8_t)rv_up21_ftype(s->lv[k]);
                        }
            }
            if (f <= 19) {                               /* (format 20) the FM-1 build's cuts */
                rv.tp[t][TP_VOC] = rv.tp[t][TP_PAIRS] = rv.tp[t][TP_BANDS] = 0;
                if (t == 0) {
                    if (rv.gp[GP_REVMODE] != 2)          /* ROOM and CLOUDS became 12-BIT: DIFFUSE is CRUNCH now, so none */
                        rv.gp[GP_REVDIFF] = 0;
                    rv.gp[GP_REVMODE] = 0;
                    rv.gp[GP_PAIRF1] = 0;                /* the feedback loop's band open */
                    rv.gp[GP_PAIRF2] = 127;
                }
            }
            if (f <= 17 && t == 0)                       /* the delay's MODE (TAPE, GRAINS) became how much is grains */
                rv.gp[GP_DLYMODE] = (int16_t)(rv.gp[GP_DLYMODE] == 1 ? 110 : 0);
            if (f <= 17 && rv.tp[t][TP_KIND] == KIND_TAPE) {   /* Beads left the tape track: its knobs became the sampler's */
                int p;
                if (rv.tp[t][TP_S0] == 1)
                    rv.tp[t][TP_S0] = TAPE_KEYS;
                for (p = TP_S6; p <= TP_S13; p++) {
                    const rv_pinfo *pi = rv_pinfo_track(t, p);
                    rv.tp[t][p] = pi->name ? pi->def : 0;
                }
                rv.tp[t][TP_ENGINE] = 0;
                rv.tp[t][TP_POLY] = 3;
            }
        }
    }
    if (f <= 21) {                                   /* (22) the tape left: a tape track loads as a blank VA synth (its
                                                      * steps were places on the tape: cleared), a route that jumped
                                                      * a playhead is off, a scene no longer resamples */
        int t, a, pt;
        for (t = 0; t < RV_TRACKS; t++) {
            if (rv.tp[t][TP_KIND] == KIND_TAPE) {
                rv_track_defaults(t, KIND_SYNTH, ENG_VA);
                pt = rv.pat_cur[t];
                for (rv.pat_cur[t] = 0; rv.pat_cur[t] < RV_PATTERNS; rv.pat_cur[t]++)
                    rv_pattern_clear(t);
                rv.pat_cur[t] = (uint8_t)pt;
            }
            for (a = 0; a < RV_TRACKS; a++)
                if (rv.infl[t][a].type == RT_TAPE)
                    rv.infl[t][a].type = RT_OFF;
        }
        for (t = 0; t < RV_SCENES; t++) {
            rv.scene[t].resample = 0;
            for (a = 0; a < RV_TRACKS * RV_TRACKS; a++)
                if (rv.scene[t].infl[a / RV_TRACKS][a % RV_TRACKS].type == RT_TAPE)
                    rv.scene[t].infl[a / RV_TRACKS][a % RV_TRACKS].type = RT_OFF;
        }
    }
    if (f <= 22) {                                   /* (23) the arpeggiator: off, its settings at their defaults */
        int t, p;
        for (t = 0; t < RV_TRACKS; t++)
            for (p = TP_ARPMODE; p <= TP_ARPCHNC; p++)
                rv.tp[t][p] = (int16_t)rv_pinfo_track(t, p)->def;
    }
    rv.format = RV_FORMAT;
    return 0;
}

int rv_slot_used(int slot, char name[13])
{
    rv_head h;
    int i;
    name[0] = 0;
    if (!head_ok(slot, &h))
        return 0;
    for (i = 0; i < 11 && h.name[i]; i++)
        name[i] = h.name[i];
    name[i] = 0;
    return 1;
}
