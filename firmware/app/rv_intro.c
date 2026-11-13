/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the start screen. The RAVEL wordmark, white on black: it comes in band by band, settles, holds, then its
 * rows slide and tear apart and it's gone; thin grey lines flash now and then. 72 frames, 24 a second: three
 * seconds. Drawn from the wordmark and each frame's rules (the smear's shifts worked out ahead: tools/make_boot.py,
 * rv_boot.h). The version, small, bottom right, all through. Any key, button or knob skips it.
 * Part of the engine unit (ravel_app.c), after rv_ui.c (its drawing and its input). */

#include "rv_boot.h"
#define IN_N 72                          /* frames */
#define IN_FMS 24u                       /* a second */
#define IN_LX 8                          /* the wordmark's top left */
#define IN_LY 94

static struct {
    uint8_t on;
    uint32_t t0;
} in;

static uint32_t in_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void intro_start(void)
{
    in.t0 = plat_ms();
    in.on = 1;
}

/* frame i's screen row y into row: black; the wordmark's row (in, cut, or slid as the frame says); a flash line */
static void in_row(int i, int y, uint16_t *row)
{
    int x, ly = y - IN_LY;
    __builtin_memset(row, 0, PLAT_W * sizeof row[0]);
    if (i >= IN_N - 1)
        return;
    if (ly >= 0 && ly < BOOT_LH) {
        int show = 1, cut = 0, sh = 0;
        if (i < 15) {                                 /* coming in: band by band from the top, its left cut away at first */
            show = ly < BOOT_LH * (i + 1) / 15;
            if (i < 8)
                cut = BOOT_LW * (8 - i) / 10;
            if (i % 4 == 0 && i > 0 && (ly == 12 || ly == 13))
                show = 0;
        } else if (i == 16 && (ly == 24 || ly == 25))  /* settling: a gap once */
            show = 0;
        else if (i >= BOOT_S0 && i < BOOT_S1)         /* going: its rows slid */
            sh = boot_shift[i - BOOT_S0][ly];
        if (show)
            for (x = cut; x < BOOT_LW; x++) {
                int sx = x - sh;
                sx = sx < 0 ? sx + BOOT_LW : sx >= BOOT_LW ? sx - BOOT_LW : sx;
                if ((boot_logo[ly * (BOOT_LW / 8) + (sx >> 3)] >> (7 - (sx & 7))) & 1)
                    row[IN_LX + x] = RGB(255, 255, 255);
            }
    }
    if ((i == 6 || i == 9 || i == 12 || i == 17 || i == 59 || i == 63 || i == 66 || i == 68) && y == 92 + (i * 17) % 55)
        for (x = 15; x < 60 + (i * 7) % 70; x++)     /* (a thin grey line, a frame) */
            row[x] = RGB(220, 220, 220);
}

static int intro_frame(void)
{
    uint32_t t = plat_ms() - in.t0, lk;
    int b, j, i;
    if (!in.on)
        return 0;
    i = (int)(t * IN_FMS / 1000u);
    if (i >= IN_N) {
        in.on = 0;
        return 0;
    }
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        for (j = 0; j < GB_H; j++)
            in_row(i, b + j, gb + j * PLAT_W);
        g_text_r(PLAT_W - 4, PLAT_H - 11, &FONT_XS, "V" RAVEL_BUILD, RGB(96, 96, 96));   /* (the version, small) */
        g_send();
    }
    /* the keys' lights: filling from the left as the wordmark comes in; all on while it holds; torn as it goes */
    if (i < 15)
        lk = (1u << (27 * (i + 1) / 15)) - 1u;
    else if (i < BOOT_S0)
        lk = 0x07FFFFFFu;
    else if (i < IN_N - 1)
        lk = in_hash((uint32_t)i * 7u) | in_hash((uint32_t)i + 99u);
    else
        lk = 0;
    plat_leds(0, lk & 0x07FFFFFFu);
    return 1;
}

/* any key, button or knob ends it (and does nothing else) */
static int intro_skip(void)
{
    uint32_t k = plat_keys(), bt = plat_buttons();
    int i, turned = 0;
    for (i = 0; i < NE; i++)
        turned |= plat_enc(i) != 0;
    if ((k & ~ui.prev_keys) || (bt & ~ui.prev_btn) || turned) {
        for (i = 0; i < NB; i++)
            if ((bt >> i) & 1u)
                ui.layer_used[i] = 1;                 /* (its let-go switches no page) */
        in.on = 0;
    }
    ui.prev_keys = k;
    ui.prev_btn = bt;
    return in.on;
}

/* the platform starts it (the FM-1 at power-on, the emulator page as it opens) */
void ui_intro(void) { intro_start(); }
int ui_intro_on(void) { return in.on; }
