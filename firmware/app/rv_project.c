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

/* older formats, by what they lack: 80 track parameters (formats 2-5), 96 globals (2-4), no CZ
 * patches (2 and 3). Everything else is laid out the same, in the same order. */
#define RV_TP_OLD 80
static int old_gp(int f) { return f >= 5 ? 128 : 96; }
static int old_size(int f)
{
    int n;
    if (f >= 10)
        return (int)sizeof(rv);
    n = (int)sizeof(rv) - (int)sizeof(rv.scene_tempo);   /* no scene tempos before format 10 */
    if (f >= 8)                                       /* formats 8 and 9: the same layout */
        return n;
    n -= (int)sizeof(rv.kitlk);                       /* no kit locks before format 8 */
    if (f >= 7)
        return n;
    n -= (int)sizeof(rv.kit);                         /* no kits before format 7 */
    if (f >= 6)
        return n;
    n -= (RV_TP - RV_TP_OLD) * RV_TRACKS * 2 + (RV_GP - old_gp(f)) * 2;
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

static uint16_t remap_pid(uint16_t pid, int gpo)
{
    int gpid_old = RV_TRACKS * RV_TP_OLD;
    (void)gpo;
    if (pid == 0xFFFF)
        return pid;
    if (pid < gpid_old)
        return (uint16_t)PID_T(pid / RV_TP_OLD, pid % RV_TP_OLD);
    return (uint16_t)PID_G(pid - gpid_old);
}

/* an older project, loaded as it was stored, is spread out into today's layout: what follows
 * the globals moves up, then the globals, then each track's row, from the end so nothing is
 * overwritten before it moves. The new places start at their defaults, and the parameter ids
 * the matrix and macros hold are renumbered. */
static void upgrade_layout(int f, int loaded)
{
    uint8_t *base = (uint8_t *)&rv;
    int tp_off = (int)((uint8_t *)&rv.tp[0][0] - base), gpo = old_gp(f);
    int gp_old = tp_off + RV_TRACKS * RV_TP_OLD * 2, rest_old = gp_old + gpo * 2;
    int gp_new = (int)((uint8_t *)&rv.gp[0] - base), rest_new = gp_new + RV_GP * 2;
    int t, p, g, i, k;
    __builtin_memmove(base + rest_new, base + rest_old, (unsigned)(loaded - rest_old));
    __builtin_memmove(base + gp_new, base + gp_old, (unsigned)(gpo * 2));
    for (t = RV_TRACKS - 1; t > 0; t--)
        __builtin_memmove(base + tp_off + t * RV_TP * 2, base + tp_off + t * RV_TP_OLD * 2, RV_TP_OLD * 2);
    for (t = 0; t < RV_TRACKS; t++) {
        for (p = RV_TP_OLD; p < RV_TP; p++)
            rv.tp[t][p] = (int16_t)(p < TP_N ? rv_pinfo_track(t, p)->def : 0);
        if (f < 5)
            rv.tp[t][TP_BANDS] = 0;
    }
    for (g = gpo; g < RV_GP; g++)
        rv.gp[g] = (int16_t)(g < GP_N ? rv_pinfo_global(g)->def : 0);
    for (i = 0; i < RV_MATRIX; i++)
        rv.mat[i].dst = remap_pid(rv.mat[i].dst, gpo);
    for (i = 0; i < RV_MACROS; i++)
        for (k = 0; k < RV_MACRO_DESTS; k++)
            rv.macro[i].d[k].dst = remap_pid(rv.macro[i].d[k].dst, gpo);
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
    if (f <= 5)
        upgrade_layout(f, n);
    if (f <= 8) {
        rv.gp[GP_SWING] = (int16_t)clampi(50 + (rv.gp[GP_SWING] + 2) / 4, 50, 100);   /* was a delay 0-75 (0.5% of a step each): now % */
        rv.gp[GP_METRO] = 0;
        rv.gp[GP_COUNTIN] = 1;
    }
    if (f <= 9)
        __builtin_memset(rv.scene_tempo, 0, sizeof(rv.scene_tempo));   /* every scene keeps the tempo */
    {
        int t;
        for (t = 0; t < RV_TRACKS; t++)                    /* (format 11) the first oscillator at full, as it was */
            rv.tp[t][TP_OSC1] = 127;
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
        }
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
