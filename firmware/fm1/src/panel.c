/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from Felucca's panel.c by way of X0X, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* Physical panel: which matrix button / encoder carries which printed label (plat.h's
 * B_* / EN_*, in Ravel's order). HARDWARE CALIBRATION (hold OCT- and OCT+ while powering on)
 * asks for each label in turn; the table is kept in flash (OBJ_PANEL) and in .noinit.
 * Ravel: its own magic, since its label order is not Felucca's. */
static const char *const B_NAME[NB] = {"OCT-", "OCT+", "FX", "SCL", "ENV", "LFO", "EDIT", "GLO",
                                        "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3",
                                        "KNOB 4", "PRESETS"};
#define PANEL_MAGIC 0x31505652u          /* "RVP1" */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13},   /* Felucca's map, in Ravel's label order */
    {0, 1, 2, 3, 4, 5, 6},
    {1, 1, 1, 1, 1, 1, 1},
};

static int panel_valid(const panel_t *p)
{
    uint32_t i, ok = p->magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++)
        ok = p->btn[i] < 14u;
    for (i = 0; ok && i < NE; i++)
        ok = p->enc[i] < 7u && (p->dir[i] == 1 || p->dir[i] == -1);
    return (int)ok;
}

static void panel_init(void)
{
    panel_t p;
    if (plat_store_load(OBJ_PANEL, &p, sizeof p) == (int)sizeof p && panel_valid(&p))
        panel = p;
    if (!panel_valid(&panel))
        panel = PANEL_DEFAULT;
}

#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    ui_notice("CALIBRATION", "TEACH EACH BUTTON AND KNOB", 0, 0, 0);
    while (fm1_in.buttons) {
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        ui_notice("CALIBRATION", "PRESS", B_NAME[i], 0, 0);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        ui_notice("CALIBRATION", "TURN CLOCKWISE", E_NAME[i], 0, 0);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    plat_store_save(OBJ_PANEL, &panel, sizeof panel);
    return;
timeout:
    panel = old;
}
