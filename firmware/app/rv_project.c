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

/* older formats: 96 globals (format 4), and no CZ patches either (2 and 3) */
#define RV_GP_OLD 96
#define RV_GROW ((int)((RV_GP - RV_GP_OLD) * sizeof(int16_t)))
#define RV_SIZE_4 ((int)sizeof(rv) - RV_GROW)
#define RV_SIZE_OLD (RV_SIZE_4 - (int)sizeof(rv.cz))

static int head_ok(int slot, rv_head *h)
{
    int n = plat_load(slot, h, sizeof(*h));
    if (h->magic != RV_MAGIC)
        return 0;
    if (h->format == RV_FORMAT)
        return n == (int)sizeof(rv);
    if (h->format == 4)
        return n == RV_SIZE_4;
    return (h->format == 2 || h->format == 3) && n == RV_SIZE_OLD;
}

/* formats 2-4 had 96 globals: what followed them moves up to make room, and the new globals
 * (BANDS) and the BANDS send start at their defaults */
static void upgrade_globals(int loaded)
{
    uint8_t *base = (uint8_t *)&rv, *from = (uint8_t *)&rv.gp[RV_GP_OLD];
    int keep = loaded - (int)(from - base), t, g;
    __builtin_memmove(from + RV_GROW, from, (unsigned)keep);
    for (g = RV_GP_OLD; g < RV_GP; g++)
        rv.gp[g] = (int16_t)(g < GP_N ? rv_pinfo_global(g)->def : 0);
    for (t = 0; t < RV_TRACKS; t++)
        rv.tp[t][TP_BANDS] = 0;
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
    if (!head_ok(slot, &h))
        return -1;
    if (h.format == RV_FORMAT) {
        if (plat_load(slot, &rv, sizeof(rv)) != (int)sizeof(rv))
            return -1;
        return 0;
    }
    if (h.format == 4) {
        if (plat_load(slot, &rv, (uint32_t)RV_SIZE_4) != RV_SIZE_4)
            return -1;
        upgrade_globals(RV_SIZE_4);
        rv.format = RV_FORMAT;
        return 0;
    }
    if (plat_load(slot, &rv, (uint32_t)RV_SIZE_OLD) != RV_SIZE_OLD)
        return -1;
    upgrade_globals(RV_SIZE_OLD);
    if (rv.format == 2)
        upgrade_from_2();
    upgrade_cz();
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
