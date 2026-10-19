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

static int head_ok(int slot, rv_head *h)
{
    int n = plat_load(slot, h, sizeof(*h));
    return n == (int)sizeof(rv) && h->magic == RV_MAGIC && (h->format == RV_FORMAT || h->format == 2);
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
    if (plat_load(slot, &rv, sizeof(rv)) != (int)sizeof(rv))
        return -1;
    if (rv.format == 2)
        upgrade_from_2();
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
