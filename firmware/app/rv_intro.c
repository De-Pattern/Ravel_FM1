/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the start screen. An animation (docs/brand/boot: ten of its frames, each blended into the next as it
 * plays): smoke curling in from the left, RAVEL forming
 * in it, tearing, dripping. Its frames are packed (tools/make_boot.py, rv_boot.h) and unpacked as the screen is
 * drawn, a row at a time, made half as big again; the last held a moment. The version, small, bottom right, all
 * through. Any key, button or knob skips it.
 * Part of the engine unit (ravel_app.c), after rv_ui.c (its drawing and its input). */

#include "rv_boot.h"
#define IN_HOLD 350u                     /* ms: the last frame, held on a little longer */

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

/* unpacking a frame (the other half of tools/make_boot.py: they must agree bit for bit): row by row, keeping the
 * row before (to predict from, and to draw between). Two at once: the frame and the next, to blend */
struct in_bd {
    uint32_t pos;                        /* the next bit, from the frame's first */
    const uint8_t *p;
    int a, n, row;                       /* the Rice code's running size; the row now in cur (-1: none yet) */
    uint8_t prev[BOOT_W + 1], cur[BOOT_W + 1];
};
static struct in_bd bd[2] RV_NOINIT;          /* (in .noinit: only while the start screen plays, each written before it is read) */

static void in_bd_start(struct in_bd *d, int f)
{
    d->p = boot_bits + boot_off[f];
    d->pos = 0;
    d->a = 4, d->n = 1, d->row = -1;
    __builtin_memset(d->cur, 0, sizeof d->cur);
}
static int in_bd_bit(struct in_bd *d)
{
    int v = (d->p[d->pos >> 3] >> (7u - (d->pos & 7u))) & 1;
    d->pos++;
    return v;
}
static int in_bd_get(struct in_bd *d, int n)
{
    int v = 0;
    while (n-- > 0)
        v = v << 1 | in_bd_bit(d);
    return v;
}
static int in_med(int a, int b, int c)
{
    int lo = a < b ? a : b, hi = a < b ? b : a;
    return c >= hi ? lo : c <= lo ? hi : a + b - c;
}
static __attribute__((noinline)) RV_SMALL void in_bd_row(struct in_bd *d)   /* the next row into cur (cur into prev) */
{
    int x = 0;
    __builtin_memcpy(d->prev, d->cur, sizeof d->prev);
    while (x < BOOT_W) {
        int a = x ? d->cur[x - 1] : 0, b = d->prev[x], c = x ? d->prev[x - 1] : 0, e4 = x + 1 < BOOT_W ? d->prev[x + 1] : 0;
        if (!(a | b | c | e4)) {                      /* (all around black: a run of black) */
            int l = 1, n, q = 0;
            while (!in_bd_bit(d))
                l++;
            n = 1 << (l - 1) | in_bd_get(d, l - 1);
            while (--n > 0 && x < BOOT_W)
                d->cur[x++] = 0;
            if (x < BOOT_W) {                         /* (and the pixel that ends it, not black) */
                while (!in_bd_bit(d))
                    q++;
                d->cur[x++] = (uint8_t)((q << 1 | in_bd_bit(d)) + 1);
            }
            continue;
        }
        {                                             /* (what's left over from the prediction) */
            int k = 0, q = 0, m, e;
            while ((d->n << k) < d->a)
                k++;
            while (!in_bd_bit(d))
                q++;
            m = q << k | in_bd_get(d, k);
            e = (m & 1) ? -((m + 1) >> 1) : m >> 1;
            d->cur[x++] = (uint8_t)((in_med(a, b, c) + e) & (BOOT_LV - 1));
            d->a += e < 0 ? -e : e;
            if (++d->n == 16)
                d->a = (d->a + 1) >> 1, d->n = 8;
        }
    }
    d->row++;
}
/* frame d's level at (fx, i0 + wy) in sixteenths (fx in sixteenths of its pixels), in sixteenths of a level */
static uint32_t in_bd_at(struct in_bd *d, uint32_t x0, uint32_t x1, uint32_t wx, uint32_t i0, uint32_t wy)
{
    const uint8_t *r0 = d->row > (int)i0 ? d->prev : d->cur, *r1 = d->cur;
    uint32_t top = r0[x0] * (16u - wx) + r0[x1] * wx, bot = r1[x0] * (16u - wx) + r1[x1] * wx;
    return (top * (16u - wy) + bot * wy) >> 4;
}

/* frame f on rows b .. b+GB_H, blended k (0..16) toward frame g: black above and below their band; within it,
 * between their pixels, then to a grey */
static __attribute__((noinline)) RV_SMALL void in_frame(int f, int g, uint32_t k, int b)
{
    int j, x;
    if (b == 0) {
        in_bd_start(&bd[0], f);
        if (k)
            in_bd_start(&bd[1], g);
    }
    for (j = 0; j < GB_H; j++) {
        int y = b + j;
        uint16_t *row = gb + j * PLAT_W;
        uint32_t fy, i0, wy;
        if (y < BOOT_Y0 || y > BOOT_Y1) {
            __builtin_memset(row, 0, PLAT_W * sizeof row[0]);
            continue;
        }
        fy = (uint32_t)(y - BOOT_Y0) * (uint32_t)((BOOT_H - 1) * 16) / (uint32_t)(BOOT_Y1 - BOOT_Y0);   /* (in sixteenths) */
        i0 = fy >> 4, wy = fy & 15u;
        while (bd[0].row < (int)i0 + 1 && bd[0].row < BOOT_H - 1)
            in_bd_row(&bd[0]);
        while (k && bd[1].row < (int)i0 + 1 && bd[1].row < BOOT_H - 1)
            in_bd_row(&bd[1]);
        for (x = 0; x < PLAT_W; x++) {
            uint32_t fx = (uint32_t)x * (uint32_t)((BOOT_W - 1) * 16) / (uint32_t)(PLAT_W - 1), x0 = fx >> 4, wx = fx & 15u, x1 = x0 + 1 < BOOT_W ? x0 + 1 : x0;
            uint32_t v = in_bd_at(&bd[0], x0, x1, wx, i0, wy);
            if (k)
                v = (v * (16u - k) + in_bd_at(&bd[1], x0, x1, wx, i0, wy) * k) >> 4;
            row[x] = boot_lut[v];
        }
    }
}

static int intro_frame(void)
{
    uint32_t t = plat_ms() - in.t0, acc = 0, lk = 0, end = IN_HOLD, k = 0;
    int b, f;
    if (!in.on)
        return 0;
    for (f = 0; f < BOOT_N; f++)
        end += boot_ms[f];
    if (t >= end) {
        in.on = 0;
        return 0;
    }
    for (f = 0; f < BOOT_N - 1 && t >= acc + boot_ms[f]; f++)
        acc += boot_ms[f];
    if (f + 1 < BOOT_N)                               /* (each frame melting into the next all the way) */
        k = (uint32_t)((t - acc) * 16u / boot_ms[f]);
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        in_frame(f, f + 1 < BOOT_N ? f + 1 : f, k > 16u ? 16u : k, b);
        g_text_r(PLAT_W - 4, PLAT_H - 11, &FONT_XS, "V" RAVEL_BUILD, RGB(96, 96, 96));   /* (the version, small) */
        g_send();
    }
    /* the keys' lights: filling from the left as the smoke comes in; torn as RAVEL tears */
    if (f < 5) {
        uint32_t n = (uint32_t)((t * 27u) / (acc + boot_ms[f] + 1u)) + 1u;
        lk = (1u << (n > 27u ? 27u : n)) - 1u;
    } else
        lk = in_hash((uint32_t)f * 7u + t / 120u) | in_hash(t / 90u + 3u);
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
